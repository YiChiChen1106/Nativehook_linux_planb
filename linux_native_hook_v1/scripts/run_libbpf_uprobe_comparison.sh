#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

SOCKET_PATH=${LNHV1_SOCKET_PATH:-"/tmp/linux_native_hook_v1_libbpf_compare_$$.sock"}
FLUSH_THRESHOLD=${LNHV1_FLUSH_THRESHOLD:-20}
SAMPLE_INTERVAL=${LNHV1_SAMPLE_INTERVAL:-1}
FILTER_SIZE=${LNHV1_FILTER_SIZE:--1}
BLOCKED=${LNHV1_BLOCKED:-0}
CAPACITY=${LNHV1_CAPACITY:-4096}
THREADS_LIST=${LNHV1_THREADS_LIST:-1,4}
SIZE_LIST=${LNHV1_SIZE_LIST:-32}
DURATION=${LNHV1_DURATION:-5}
ALLOC_PATTERN_LIST=${LNHV1_ALLOC_PATTERN_LIST:-malloc_only}
REPEAT_COUNT=${LNHV1_REPEAT:-1}
LIBBPF_MODE_LIST=${LNHV1_LIBBPF_MODE_LIST:-libbpf_count_only,libbpf_sample_filter,libbpf_tracking,libbpf_ring_output}
OPT_TRACKING_MODE=${LNHV1_OPT_TRACKING_MODE:-sharded}
CSV_PATH=${LNHV1_CSV_PATH:-"${ROOT_DIR}/results/libbpf_uprobe_comparison_server_$(date +%F).csv"}
AUTO_BUILD=${LNHV1_AUTO_BUILD:-1}
LIBC_PATH=${LNHV1_LIBC_PATH:-}

CONSUMER_BIN="${ROOT_DIR}/build/consumer"
HOOK_LIB="${ROOT_DIR}/build/hook_preload.so"
WORKLOAD="${ROOT_DIR}/build/perf_test_data_linux"
LOADER_BIN="${ROOT_DIR}/build/libbpf_uprobe_loader"
LOG_DIR="${ROOT_DIR}/results/logs"

consumer_pid=""
RUN_LIBBPF=1

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

validate_libbpf_mode()
{
    case "$1" in
        libbpf_count_only|libbpf_sample_filter|libbpf_tracking|libbpf_ring_output) return 0 ;;
        *) return 1 ;;
    esac
}

validate_alloc_pattern()
{
    case "$1" in
        malloc_only|mixed3) return 0 ;;
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
    local alloc_pattern=$4

    "${WORKLOAD}" --threads "${threads}" --duration "${duration}" --size "${size}" --pattern "${alloc_pattern}"
}

run_stage6_workload()
{
    local pid_tid_cache=$1
    local tracking_mode=$2
    local threads=$3
    local duration=$4
    local size=$5
    local alloc_pattern=$6

    env -u LNHV1_SUBABLATION_STAGE \
        LNHV1_ABLATION_STAGE=6 \
        LNHV1_PID_TID_CACHE="${pid_tid_cache}" \
        LNHV1_TRACKING_MODE="${tracking_mode}" \
        LNHV1_SOCKET_PATH="${SOCKET_PATH}" \
        LD_PRELOAD="${HOOK_LIB}" \
        "${WORKLOAD}" --threads "${threads}" --duration "${duration}" --size "${size}" --pattern "${alloc_pattern}"
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

write_csv_row()
{
    local row_kind=$1
    local backend=$2
    local mode=$3
    local mode_name=$4
    local tracking_mode=$5
    local pid_tid_cache=$6
    local alloc_pattern=$7
    local repeat_index=$8
    local threads=$9
    local size=${10}
    local baseline_ops=${11}
    local with_probe_ops=${12}
    local overhead_pct=${13}
    local records_or_events=${14}
    local malloc_events=${15}
    local calloc_events=${16}
    local realloc_events=${17}
    local free_events=${18}
    local alloc_records=${19}
    local free_records=${20}
    local unmatched_free=${21}
    local ringbuf_drops=${22}
    local observed_events=${23}
    local probe_log=${24}
    local consumer_log=${25}

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "${row_kind}" \
        "${backend}" \
        "${mode}" \
        "${mode_name}" \
        "${tracking_mode}" \
        "${pid_tid_cache}" \
        "${alloc_pattern}" \
        "${repeat_index}" \
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
        "${calloc_events}" \
        "${realloc_events}" \
        "${free_events}" \
        "${alloc_records}" \
        "${free_records}" \
        "${unmatched_free}" \
        "${ringbuf_drops}" \
        "${observed_events}" \
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
    local alloc_pattern=$7
    local repeat_index=$8
    local consumer_log="${LOG_DIR}/libbpf_compare_${mode}_${tracking_mode}_cache${pid_tid_cache}_${alloc_pattern}_r${repeat_index}_t${threads}_s${size}_d${DURATION}.consumer.log"
    local hook_output hook_line with_probe_ops overhead_pct wake_line records alloc free

    start_consumer "${consumer_log}"
    hook_output=$(run_stage6_workload "${pid_tid_cache}" "${tracking_mode}" "${threads}" "${DURATION}" "${size}" "${alloc_pattern}")
    sleep 1
    stop_consumer

    hook_line=$(printf '%s\n' "${hook_output}" | tail -n1)
    with_probe_ops=$(extract_metric "${hook_line}" "throughput_ops" "0")
    overhead_pct=$(compute_overhead_pct "${baseline_ops}" "${with_probe_ops}")
    wake_line=$(grep '^wake=' "${consumer_log}" | tail -n1 || true)
    records=$(extract_metric "${wake_line}" "records" "0")
    alloc=$(extract_metric "${wake_line}" "alloc" "0")
    free=$(extract_metric "${wake_line}" "free" "0")

    write_csv_row "ld_preload" "ld_preload" "${mode}" "stage6_notify" "${tracking_mode}" "${pid_tid_cache}" \
        "${alloc_pattern}" "${repeat_index}" "${threads}" "${size}" "${baseline_ops}" "${with_probe_ops}" "${overhead_pct}" \
        "${records}" "${alloc}" 0 0 "${free}" "${alloc}" "${free}" 0 0 0 "" "${consumer_log}"

    printf '[ld_preload %s mode=%s cache=%s pattern=%s repeat=%s] threads=%s size=%s baseline_ops=%s with_probe_ops=%s overhead_pct=%s records=%s\n' \
        "${mode}" "${tracking_mode}" "${pid_tid_cache}" "${alloc_pattern}" "${repeat_index}" "${threads}" "${size}" \
        "${baseline_ops}" "${with_probe_ops}" "${overhead_pct}" "${records}"
}

record_libbpf_mode()
{
    local mode=$1
    local threads=$2
    local size=$3
    local baseline_ops=$4
    local libc_path=$5
    local alloc_pattern=$6
    local repeat_index=$7
    local probe_log="${LOG_DIR}/libbpf_compare_${mode}_${alloc_pattern}_r${repeat_index}_t${threads}_s${size}_d${DURATION}.loader.log"
    local probe_output probe_line with_probe_ops overhead_pct
    local malloc_events calloc_events realloc_events free_events alloc_records free_records unmatched_free output_records ringbuf_drops observed_events records_or_events

    probe_output=$("${LOADER_BIN}" \
        --mode "${mode}" \
        --libc "${libc_path}" \
        --sample-interval "${SAMPLE_INTERVAL}" \
        --filter-size "${FILTER_SIZE}" \
        -- "${WORKLOAD}" --threads "${threads}" --duration "${DURATION}" --size "${size}" --pattern "${alloc_pattern}" 2>&1)
    printf '%s\n' "${probe_output}" >"${probe_log}"

    probe_line=$(printf '%s\n' "${probe_output}" | grep '^lnhv1_libbpf_summary' | tail -n1 || true)
    with_probe_ops=$(extract_metric "${probe_line}" "throughput_ops" "0")
    overhead_pct=$(compute_overhead_pct "${baseline_ops}" "${with_probe_ops}")
    malloc_events=$(extract_metric "${probe_line}" "malloc_calls" "0")
    calloc_events=$(extract_metric "${probe_line}" "calloc_calls" "0")
    realloc_events=$(extract_metric "${probe_line}" "realloc_calls" "0")
    free_events=$(extract_metric "${probe_line}" "free_calls" "0")
    alloc_records=$(extract_metric "${probe_line}" "alloc_records" "0")
    free_records=$(extract_metric "${probe_line}" "matched_frees" "0")
    unmatched_free=$(extract_metric "${probe_line}" "unmatched_frees" "0")
    output_records=$(extract_metric "${probe_line}" "output_records" "0")
    ringbuf_drops=$(extract_metric "${probe_line}" "ringbuf_drops" "0")
    observed_events=$(extract_metric "${probe_line}" "observed_events" "0")

    case "${mode}" in
        libbpf_count_only) records_or_events=$((malloc_events + calloc_events + realloc_events + free_events)) ;;
        libbpf_sample_filter) records_or_events=$(extract_metric "${probe_line}" "sampled_alloc_returns" "0") ;;
        libbpf_tracking) records_or_events=$((alloc_records + free_records)) ;;
        libbpf_ring_output) records_or_events="${output_records}" ;;
        *) records_or_events=0 ;;
    esac

    write_csv_row "ebpf_uprobe" "libbpf" "${mode}" "${mode}" "none" "n/a" \
        "${alloc_pattern}" "${repeat_index}" "${threads}" "${size}" "${baseline_ops}" "${with_probe_ops}" "${overhead_pct}" \
        "${records_or_events}" "${malloc_events}" "${calloc_events}" "${realloc_events}" "${free_events}" "${alloc_records}" \
        "${free_records}" "${unmatched_free}" "${ringbuf_drops}" "${observed_events}" "${probe_log}" ""

    printf '[libbpf %s pattern=%s repeat=%s] threads=%s size=%s baseline_ops=%s with_probe_ops=%s overhead_pct=%s events=%s malloc=%s calloc=%s realloc=%s free=%s drops=%s observed=%s\n' \
        "${mode}" "${alloc_pattern}" "${repeat_index}" "${threads}" "${size}" "${baseline_ops}" "${with_probe_ops}" \
        "${overhead_pct}" "${records_or_events}" "${malloc_events}" "${calloc_events}" "${realloc_events}" "${free_events}" \
        "${ringbuf_drops}" "${observed_events}"
}

if [[ "${AUTO_BUILD}" != "0" ]]; then
    if ! command -v cmake >/dev/null 2>&1; then
        echo "cmake is required for the libbpf loader build" >&2
        exit 1
    fi
    cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build" -DLNHV1_ENABLE_LIBBPF=ON
    cmake --build "${ROOT_DIR}/build" -j
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
IFS=',' read -r -a ALLOC_PATTERN_VALUES <<< "${ALLOC_PATTERN_LIST}"
if [[ -z "${LIBBPF_MODE_LIST}" || "${LIBBPF_MODE_LIST}" == "none" ]]; then
    RUN_LIBBPF=0
    LIBBPF_MODE_VALUES=()
else
    IFS=',' read -r -a LIBBPF_MODE_VALUES <<< "${LIBBPF_MODE_LIST}"
fi

for mode in "${LIBBPF_MODE_VALUES[@]}"; do
    if ! validate_libbpf_mode "${mode}"; then
        echo "invalid libbpf mode: ${mode}" >&2
        exit 1
    fi
done

for alloc_pattern in "${ALLOC_PATTERN_VALUES[@]}"; do
    if ! validate_alloc_pattern "${alloc_pattern}"; then
        echo "invalid allocator pattern: ${alloc_pattern}" >&2
        exit 1
    fi
done

if ! [[ "${REPEAT_COUNT}" =~ ^[1-9][0-9]*$ ]]; then
    echo "LNHV1_REPEAT must be a positive integer" >&2
    exit 1
fi

if [[ "${RUN_LIBBPF}" == "1" && "${EUID}" -ne 0 ]]; then
    echo "libbpf uProbe loader requires root or equivalent BPF/uProbe capabilities; run this script with sudo on pink" >&2
    exit 1
fi

if [[ "${RUN_LIBBPF}" == "1" ]]; then
    require_binary "${LOADER_BIN}"
    LIBC_PATH=$(detect_libc_path)
    if [[ ! -f "${LIBC_PATH}" ]]; then
        echo "libc path does not exist: ${LIBC_PATH}" >&2
        exit 1
    fi
fi

cat >"${CSV_PATH}" <<'EOF'
row_kind,backend,mode,mode_name,tracking_mode,pid_tid_cache,alloc_pattern,repeat_index,threads,size,duration,flush_threshold,sample_interval,filter_size,blocked,baseline_ops,with_probe_ops,overhead_pct,records_or_events,malloc_events,calloc_events,realloc_events,free_events,alloc_records,free_records,unmatched_free,ringbuf_drops,observed_events,logs
EOF

for repeat_index in $(seq 1 "${REPEAT_COUNT}"); do
    for alloc_pattern in "${ALLOC_PATTERN_VALUES[@]}"; do
        for threads in "${THREAD_VALUES[@]}"; do
            for size in "${SIZE_VALUES[@]}"; do
                baseline_output=$(run_workload "${threads}" "${DURATION}" "${size}" "${alloc_pattern}")
                baseline_line=$(printf '%s\n' "${baseline_output}" | tail -n1)
                baseline_ops=$(extract_metric "${baseline_line}" "throughput_ops" "0")
                write_csv_row "baseline" "none" "baseline_no_hook" "baseline_no_hook" "none" "n/a" \
                    "${alloc_pattern}" "${repeat_index}" "${threads}" "${size}" "${baseline_ops}" "${baseline_ops}" "0.0000" \
                    0 0 0 0 0 0 0 0 0 0 "" ""
                printf '[baseline pattern=%s repeat=%s] threads=%s size=%s baseline_ops=%s\n' \
                    "${alloc_pattern}" "${repeat_index}" "${threads}" "${size}" "${baseline_ops}"

                record_ld_preload_stage6 "ld_preload_stage6_default" "global" 0 "${threads}" "${size}" "${baseline_ops}" "${alloc_pattern}" "${repeat_index}"
                record_ld_preload_stage6 "ld_preload_stage6_optimized" "${OPT_TRACKING_MODE}" 1 "${threads}" "${size}" "${baseline_ops}" "${alloc_pattern}" "${repeat_index}"

                if [[ "${RUN_LIBBPF}" == "1" ]]; then
                    for mode in "${LIBBPF_MODE_VALUES[@]}"; do
                        record_libbpf_mode "${mode}" "${threads}" "${size}" "${baseline_ops}" "${LIBC_PATH}" "${alloc_pattern}" "${repeat_index}"
                    done
                fi
            done
        done
    done
done

echo "csv written to ${CSV_PATH}"
