#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

SOCKET_PATH=${LNHV1_SOCKET_PATH:-"/tmp/linux_native_hook_v1_ebpf_compare_$$.sock"}
FLUSH_THRESHOLD=${LNHV1_FLUSH_THRESHOLD:-20}
SAMPLE_INTERVAL=${LNHV1_SAMPLE_INTERVAL:-1}
FILTER_SIZE=${LNHV1_FILTER_SIZE:--1}
BLOCKED=${LNHV1_BLOCKED:-0}
CAPACITY=${LNHV1_CAPACITY:-4096}
THREADS_LIST=${LNHV1_THREADS_LIST:-1,4}
SIZE_LIST=${LNHV1_SIZE_LIST:-32}
DURATION=${LNHV1_DURATION:-5}
EBPF_MODE_LIST=${LNHV1_EBPF_MODE_LIST:-ebpf_count_only,ebpf_sample_filter,ebpf_tracking,ebpf_ring_output}
OPT_TRACKING_MODE=${LNHV1_OPT_TRACKING_MODE:-sharded}
CSV_PATH=${LNHV1_CSV_PATH:-"${ROOT_DIR}/results/ebpf_uprobe_comparison_server_$(date +%F).csv"}
AUTO_BUILD=${LNHV1_AUTO_BUILD:-1}
BPFTRACE_BIN=${LNHV1_BPFTRACE_BIN:-bpftrace}
BPFTRACE_SUDO=${LNHV1_BPFTRACE_SUDO-sudo}
LIBC_PATH=${LNHV1_LIBC_PATH:-}

CONSUMER_BIN="${ROOT_DIR}/build/consumer"
HOOK_LIB="${ROOT_DIR}/build/hook_preload.so"
WORKLOAD="${ROOT_DIR}/build/perf_test_data_linux"
EBPF_DIR="${ROOT_DIR}/ebpf_probe"
LOG_DIR="${ROOT_DIR}/results/logs"

consumer_pid=""
declare -a BPFTRACE_PREFIX=()
RUN_EBPF=1

cleanup()
{
    if [[ -n "${consumer_pid}" ]]; then
        stop_consumer
    fi
    rm -f "${SOCKET_PATH}"
}

trap cleanup EXIT

require_binary()
{
    local path=$1
    if [[ ! -x "${path}" ]]; then
        echo "missing executable: ${path}" >&2
        exit 1
    fi
}

validate_ebpf_mode()
{
    case "$1" in
        ebpf_count_only|ebpf_sample_filter|ebpf_tracking|ebpf_ring_output) return 0 ;;
        *) return 1 ;;
    esac
}

template_for_mode()
{
    case "$1" in
        ebpf_count_only) printf '%s\n' "${EBPF_DIR}/uprobe_count_only.bt.in" ;;
        ebpf_sample_filter) printf '%s\n' "${EBPF_DIR}/uprobe_sample_filter.bt.in" ;;
        ebpf_tracking) printf '%s\n' "${EBPF_DIR}/uprobe_tracking.bt.in" ;;
        ebpf_ring_output) printf '%s\n' "${EBPF_DIR}/uprobe_ring_output.bt.in" ;;
        *) return 1 ;;
    esac
}

extract_metric()
{
    local line=$1
    local key=$2
    local default_value=$3
    local value

    value=$(printf '%s\n' "${line}" | awk -v target="${key}" '
        {
            for (i = 1; i <= NF; ++i) {
                split($i, kv, "=")
                if (kv[1] == target) {
                    print kv[2]
                    exit
                }
            }
        }')

    if [[ -z "${value:-}" ]]; then
        printf '%s\n' "${default_value}"
    else
        printf '%s\n' "${value}"
    fi
}

extract_bpftrace_counter()
{
    local log_path=$1
    local counter_name=$2
    local default_value=${3:-0}
    local value

    value=$(awk -v name="@${counter_name}:" '$1 == name { value = $2 } END { if (value != "") print value }' "${log_path}" 2>/dev/null || true)
    if [[ -z "${value:-}" ]]; then
        printf '%s\n' "${default_value}"
    else
        printf '%s\n' "${value}"
    fi
}

compute_overhead_pct()
{
    local baseline_ops=$1
    local probe_ops=$2
    awk -v baseline="${baseline_ops}" -v probe="${probe_ops}" 'BEGIN {
        if (baseline == 0) {
            printf "0.0000"
        } else {
            printf "%.4f", ((baseline - probe) * 100.0) / baseline
        }
    }'
}

start_consumer()
{
    local consumer_log=$1
    local -a consumer_args=(
        --socket "${SOCKET_PATH}"
        --capacity "${CAPACITY}"
        --flush-threshold "${FLUSH_THRESHOLD}"
        --sample-interval "${SAMPLE_INTERVAL}"
        --filter-size "${FILTER_SIZE}"
    )
    if [[ "${BLOCKED}" != "0" ]]; then
        consumer_args+=(--blocked)
    fi

    rm -f "${SOCKET_PATH}"
    "${CONSUMER_BIN}" "${consumer_args[@]}" >"${consumer_log}" 2>&1 &
    consumer_pid=$!
    sleep 1

    if ! kill -0 "${consumer_pid}" >/dev/null 2>&1; then
        echo "consumer failed to start" >&2
        cat "${consumer_log}" >&2 || true
        exit 1
    fi
}

stop_consumer()
{
    if [[ -n "${consumer_pid}" ]]; then
        kill "${consumer_pid}" >/dev/null 2>&1 || true
        for _ in $(seq 1 20); do
            if ! kill -0 "${consumer_pid}" >/dev/null 2>&1; then
                wait "${consumer_pid}" >/dev/null 2>&1 || true
                consumer_pid=""
                return
            fi
            sleep 0.1
        done

        kill -9 "${consumer_pid}" >/dev/null 2>&1 || true
        wait "${consumer_pid}" >/dev/null 2>&1 || true
        consumer_pid=""
    fi
}

run_workload()
{
    local threads=$1
    local duration=$2
    local size=$3

    "${WORKLOAD}" --threads "${threads}" --duration "${duration}" --size "${size}"
}

run_stage6_workload()
{
    local pid_tid_cache=$1
    local tracking_mode=$2
    local threads=$3
    local duration=$4
    local size=$5

    env -u LNHV1_SUBABLATION_STAGE \
        LNHV1_ABLATION_STAGE=6 \
        LNHV1_PID_TID_CACHE="${pid_tid_cache}" \
        LNHV1_TRACKING_MODE="${tracking_mode}" \
        LNHV1_SOCKET_PATH="${SOCKET_PATH}" \
        LD_PRELOAD="${HOOK_LIB}" \
        "${WORKLOAD}" --threads "${threads}" --duration "${duration}" --size "${size}"
}

build_with_gxx()
{
    if ! command -v g++ >/dev/null 2>&1; then
        echo "missing cmake and g++; cannot build" >&2
        exit 1
    fi

    mkdir -p "${ROOT_DIR}/build"
    g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I"${ROOT_DIR}" \
        "${ROOT_DIR}/consumer/server_main.cpp" \
        "${ROOT_DIR}/consumer/control_server.cpp" \
        "${ROOT_DIR}/consumer/shm_consumer.cpp" \
        "${ROOT_DIR}/consumer/metrics.cpp" \
        "${ROOT_DIR}/common/socket_fd.cpp" \
        -o "${CONSUMER_BIN}" -pthread -lrt
    g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I"${ROOT_DIR}" \
        "${ROOT_DIR}/tests/perf_test_data_linux.cpp" \
        -o "${WORKLOAD}" -pthread
    g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -fPIC -shared -I"${ROOT_DIR}" \
        "${ROOT_DIR}/producer_hook/ablation_config.cpp" \
        "${ROOT_DIR}/producer_hook/hook_preload.cpp" \
        "${ROOT_DIR}/producer_hook/hook_guard.cpp" \
        "${ROOT_DIR}/producer_hook/hotpath_profile.cpp" \
        "${ROOT_DIR}/producer_hook/hook_writer.cpp" \
        "${ROOT_DIR}/common/socket_fd.cpp" \
        -o "${HOOK_LIB}" -pthread -ldl -lrt
}

detect_libc_path()
{
    if [[ -n "${LIBC_PATH}" ]]; then
        printf '%s\n' "${LIBC_PATH}"
        return
    fi

    local detected
    detected=$(ldd "${WORKLOAD}" | awk '/libc\.so\.6/ { print $3; exit }')
    if [[ -z "${detected:-}" || ! -f "${detected}" ]]; then
        detected="/lib64/libc.so.6"
    fi
    printf '%s\n' "${detected}"
}

prepare_bpftrace()
{
    if ! command -v "${BPFTRACE_BIN}" >/dev/null 2>&1; then
        echo "missing bpftrace binary: ${BPFTRACE_BIN}" >&2
        exit 1
    fi

    if [[ "${EUID}" -ne 0 ]]; then
        if [[ -z "${BPFTRACE_SUDO}" ]]; then
            echo "bpftrace requires root on pink; set LNHV1_BPFTRACE_SUDO=sudo or run as root" >&2
            exit 1
        fi
        read -r -a BPFTRACE_PREFIX <<< "${BPFTRACE_SUDO}"
        "${BPFTRACE_PREFIX[@]}" -v
    fi
}

render_bpftrace_template()
{
    local mode=$1
    local libc_path=$2
    local rendered_path=$3
    local template_path

    template_path=$(template_for_mode "${mode}")
    if [[ ! -f "${template_path}" ]]; then
        echo "missing bpftrace template: ${template_path}" >&2
        exit 1
    fi

    sed \
        -e "s|@@LIBC_PATH@@|${libc_path}|g" \
        -e "s|@@SAMPLE_INTERVAL@@|${SAMPLE_INTERVAL}|g" \
        -e "s|@@FILTER_SIZE@@|${FILTER_SIZE}|g" \
        "${template_path}" >"${rendered_path}"
}

run_ebpf_workload()
{
    local mode=$1
    local threads=$2
    local duration=$3
    local size=$4
    local log_path=$5
    local libc_path=$6
    local rendered_bt="${LOG_DIR}/ebpf_${mode}_t${threads}_s${size}_d${duration}_$$.bt"
    local workload_command

    render_bpftrace_template "${mode}" "${libc_path}" "${rendered_bt}"
    workload_command="${WORKLOAD} --threads ${threads} --duration ${duration} --size ${size}"

    "${BPFTRACE_PREFIX[@]}" "${BPFTRACE_BIN}" -q -c "${workload_command}" "${rendered_bt}" >"${log_path}" 2>&1
}

write_csv_row()
{
    local row_kind=$1
    local mode=$2
    local mode_name=$3
    local tracking_mode=$4
    local pid_tid_cache=$5
    local threads=$6
    local size=$7
    local baseline_ops=$8
    local with_probe_ops=$9
    local overhead_pct=${10}
    local records_or_events=${11}
    local malloc_events=${12}
    local free_events=${13}
    local alloc_records=${14}
    local free_records=${15}
    local unmatched_free=${16}
    local probe_log=${17}
    local consumer_log=${18}

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "${row_kind}" \
        "${mode}" \
        "${mode_name}" \
        "${tracking_mode}" \
        "${pid_tid_cache}" \
        "${threads}" \
        "${size}" \
        "${DURATION}" \
        "${FLUSH_THRESHOLD}" \
        "${SAMPLE_INTERVAL}" \
        "${FILTER_SIZE}" \
        "${BLOCKED}" \
        "${baseline_ops}" \
        "${with_probe_ops}" \
        "${overhead_pct}" \
        "${records_or_events}" \
        "${malloc_events}" \
        "${free_events}" \
        "${alloc_records}" \
        "${free_records}" \
        "${unmatched_free}" \
        "${probe_log};${consumer_log}" \
        >>"${CSV_PATH}"
}

record_ld_preload_stage6()
{
    local mode=$1
    local tracking_mode=$2
    local pid_tid_cache=$3
    local threads=$4
    local size=$5
    local baseline_ops=$6
    local consumer_log="${LOG_DIR}/ebpf_compare_${mode}_${tracking_mode}_cache${pid_tid_cache}_t${threads}_s${size}_d${DURATION}.consumer.log"
    local hook_output hook_line with_probe_ops overhead_pct wake_line records alloc free

    start_consumer "${consumer_log}"
    hook_output=$(run_stage6_workload "${pid_tid_cache}" "${tracking_mode}" "${threads}" "${DURATION}" "${size}")
    sleep 1
    stop_consumer

    hook_line=$(printf '%s\n' "${hook_output}" | tail -n1)
    with_probe_ops=$(extract_metric "${hook_line}" "throughput_ops" "0")
    overhead_pct=$(compute_overhead_pct "${baseline_ops}" "${with_probe_ops}")
    wake_line=$(grep '^wake=' "${consumer_log}" | tail -n1 || true)
    records=$(extract_metric "${wake_line}" "records" "0")
    alloc=$(extract_metric "${wake_line}" "alloc" "0")
    free=$(extract_metric "${wake_line}" "free" "0")

    write_csv_row "ld_preload" "${mode}" "stage6_notify" "${tracking_mode}" "${pid_tid_cache}" \
        "${threads}" "${size}" "${baseline_ops}" "${with_probe_ops}" "${overhead_pct}" \
        "${records}" "${alloc}" "${free}" "${alloc}" "${free}" 0 "" "${consumer_log}"

    printf '[ld_preload %s mode=%s cache=%s] threads=%s size=%s baseline_ops=%s with_probe_ops=%s overhead_pct=%s records=%s\n' \
        "${mode}" "${tracking_mode}" "${pid_tid_cache}" "${threads}" "${size}" \
        "${baseline_ops}" "${with_probe_ops}" "${overhead_pct}" "${records}"
}

record_ebpf_mode()
{
    local mode=$1
    local threads=$2
    local size=$3
    local baseline_ops=$4
    local libc_path=$5
    local probe_log="${LOG_DIR}/ebpf_compare_${mode}_t${threads}_s${size}_d${DURATION}.bpftrace.log"
    local probe_line with_probe_ops overhead_pct
    local malloc_events free_events alloc_records free_records unmatched_free records_or_events output_records

    run_ebpf_workload "${mode}" "${threads}" "${DURATION}" "${size}" "${probe_log}" "${libc_path}"

    probe_line=$(grep 'throughput_ops=' "${probe_log}" | tail -n1 || true)
    with_probe_ops=$(extract_metric "${probe_line}" "throughput_ops" "0")
    overhead_pct=$(compute_overhead_pct "${baseline_ops}" "${with_probe_ops}")
    malloc_events=$(extract_bpftrace_counter "${probe_log}" "malloc_calls" "0")
    free_events=$(extract_bpftrace_counter "${probe_log}" "free_calls" "0")
    alloc_records=$(extract_bpftrace_counter "${probe_log}" "alloc_records" "0")
    free_records=$(extract_bpftrace_counter "${probe_log}" "matched_frees" "0")
    unmatched_free=$(extract_bpftrace_counter "${probe_log}" "unmatched_frees" "0")
    output_records=$(extract_bpftrace_counter "${probe_log}" "output_records" "0")

    case "${mode}" in
        ebpf_count_only) records_or_events=$((malloc_events + free_events)) ;;
        ebpf_sample_filter) records_or_events=$(extract_bpftrace_counter "${probe_log}" "sampled_alloc_returns" "0") ;;
        ebpf_tracking) records_or_events=$((alloc_records + free_records)) ;;
        ebpf_ring_output) records_or_events="${output_records}" ;;
        *) records_or_events=0 ;;
    esac

    write_csv_row "ebpf_uprobe" "${mode}" "${mode}" "none" "n/a" \
        "${threads}" "${size}" "${baseline_ops}" "${with_probe_ops}" "${overhead_pct}" \
        "${records_or_events}" "${malloc_events}" "${free_events}" "${alloc_records}" \
        "${free_records}" "${unmatched_free}" "${probe_log}" ""

    printf '[ebpf %s] threads=%s size=%s baseline_ops=%s with_probe_ops=%s overhead_pct=%s events=%s malloc=%s free=%s\n' \
        "${mode}" "${threads}" "${size}" "${baseline_ops}" "${with_probe_ops}" \
        "${overhead_pct}" "${records_or_events}" "${malloc_events}" "${free_events}"
}

if [[ "${AUTO_BUILD}" != "0" ]]; then
    if command -v cmake >/dev/null 2>&1; then
        cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build"
        cmake --build "${ROOT_DIR}/build" -j
    else
        echo "cmake not found; falling back to direct g++ build"
        build_with_gxx
    fi
fi

mkdir -p "$(dirname "${CSV_PATH}")"
mkdir -p "${LOG_DIR}"

require_binary "${CONSUMER_BIN}"
require_binary "${WORKLOAD}"
if [[ ! -f "${HOOK_LIB}" ]]; then
    echo "missing hook library: ${HOOK_LIB}" >&2
    exit 1
fi

IFS=',' read -r -a THREAD_VALUES <<< "${THREADS_LIST}"
IFS=',' read -r -a SIZE_VALUES <<< "${SIZE_LIST}"
if [[ -z "${EBPF_MODE_LIST}" || "${EBPF_MODE_LIST}" == "none" ]]; then
    RUN_EBPF=0
    EBPF_MODE_VALUES=()
else
    IFS=',' read -r -a EBPF_MODE_VALUES <<< "${EBPF_MODE_LIST}"
fi

for mode in "${EBPF_MODE_VALUES[@]}"; do
    if ! validate_ebpf_mode "${mode}"; then
        echo "invalid eBPF mode: ${mode}" >&2
        exit 1
    fi
done

if [[ "${RUN_EBPF}" == "1" ]]; then
    prepare_bpftrace
    LIBC_PATH=$(detect_libc_path)
    if [[ ! -f "${LIBC_PATH}" ]]; then
        echo "libc path does not exist: ${LIBC_PATH}" >&2
        exit 1
    fi
fi

cat >"${CSV_PATH}" <<'EOF'
row_kind,mode,mode_name,tracking_mode,pid_tid_cache,threads,size,duration,flush_threshold,sample_interval,filter_size,blocked,baseline_ops,with_probe_ops,overhead_pct,records_or_events,malloc_events,free_events,alloc_records,free_records,unmatched_free,logs
EOF

for threads in "${THREAD_VALUES[@]}"; do
    for size in "${SIZE_VALUES[@]}"; do
        baseline_output=$(run_workload "${threads}" "${DURATION}" "${size}")
        baseline_line=$(printf '%s\n' "${baseline_output}" | tail -n1)
        baseline_ops=$(extract_metric "${baseline_line}" "throughput_ops" "0")
        write_csv_row "baseline" "baseline_no_hook" "baseline_no_hook" "none" "n/a" \
            "${threads}" "${size}" "${baseline_ops}" "${baseline_ops}" "0.0000" \
            0 0 0 0 0 0 "" ""
        printf '[baseline] threads=%s size=%s baseline_ops=%s\n' "${threads}" "${size}" "${baseline_ops}"

        record_ld_preload_stage6 "ld_preload_stage6_default" "global" 0 "${threads}" "${size}" "${baseline_ops}"
        record_ld_preload_stage6 "ld_preload_stage6_optimized" "${OPT_TRACKING_MODE}" 1 "${threads}" "${size}" "${baseline_ops}"

        if [[ "${RUN_EBPF}" == "1" ]]; then
            for mode in "${EBPF_MODE_VALUES[@]}"; do
                record_ebpf_mode "${mode}" "${threads}" "${size}" "${baseline_ops}" "${LIBC_PATH}"
            done
        fi
    done
done

echo "csv written to ${CSV_PATH}"
