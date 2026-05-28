#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

SOCKET_PATH=${LNHV1_SOCKET_PATH:-"/tmp/linux_native_hook_v1_libbpf_fixed_work_$$.sock"}
FLUSH_THRESHOLD=${LNHV1_FLUSH_THRESHOLD:-20}
SAMPLE_INTERVAL=${LNHV1_SAMPLE_INTERVAL:-1}
FILTER_SIZE=${LNHV1_FILTER_SIZE:--1}
BLOCKED=${LNHV1_BLOCKED:-0}
CAPACITY=${LNHV1_CAPACITY:-4096}
THREADS_LIST=${LNHV1_THREADS_LIST:-1,4}
SIZE_LIST=${LNHV1_SIZE_LIST:-32}
FIXED_TOTAL_OPS=${LNHV1_FIXED_TOTAL_OPS:-1000000}
LIBBPF_MODE_LIST=${LNHV1_LIBBPF_MODE_LIST:-libbpf_count_only,libbpf_sample_filter,libbpf_tracking,libbpf_ring_output}
OPT_TRACKING_MODE=${LNHV1_OPT_TRACKING_MODE:-thread_local_fallback}
CSV_PATH=${LNHV1_CSV_PATH:-"${ROOT_DIR}/results/libbpf_fixed_work_comparison_server_$(date +%F).csv"}
AUTO_BUILD=${LNHV1_AUTO_BUILD:-1}
LIBC_PATH=${LNHV1_LIBC_PATH:-}

CONSUMER_BIN="${ROOT_DIR}/build/consumer"
HOOK_LIB="${ROOT_DIR}/build/hook_preload.so"
WORKLOAD="${ROOT_DIR}/build/perf_test_data_linux"
LOADER_BIN="${ROOT_DIR}/build/libbpf_uprobe_loader"
LOG_DIR="${ROOT_DIR}/results/logs"

consumer_pid=""
RUN_LIBBPF=1

declare -A optimized_elapsed_by_key

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

subtract_seconds()
{
    local left=$1
    local right=$2
    awk -v left="${left}" -v right="${right}" 'BEGIN { printf "%.9f", left - right }'
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

run_workload()
{
    local threads=$1
    local iters_per_thread=$2
    local size=$3

    "${WORKLOAD}" --threads "${threads}" --iters-per-thread "${iters_per_thread}" --size "${size}"
}

run_stage6_workload()
{
    local pid_tid_cache=$1
    local tracking_mode=$2
    local threads=$3
    local iters_per_thread=$4
    local size=$5

    env -u LNHV1_SUBABLATION_STAGE \
        LNHV1_ABLATION_STAGE=6 \
        LNHV1_PID_TID_CACHE="${pid_tid_cache}" \
        LNHV1_TRACKING_MODE="${tracking_mode}" \
        LNHV1_SOCKET_PATH="${SOCKET_PATH}" \
        LD_PRELOAD="${HOOK_LIB}" \
        "${WORKLOAD}" --threads "${threads}" --iters-per-thread "${iters_per_thread}" --size "${size}"
}

write_csv_row()
{
    local row_kind=$1
    local backend=$2
    local mode=$3
    local tracking_mode=$4
    local pid_tid_cache=$5
    local threads=$6
    local size=$7
    local total_ops=$8
    local iters_per_thread=$9
    local baseline_elapsed=${10}
    local baseline_ops=${11}
    local with_probe_elapsed=${12}
    local with_probe_ops=${13}
    local elapsed_over_baseline=${14}
    local optimized_elapsed=${15}
    local saved_vs_optimized=${16}
    local records_or_events=${17}
    local malloc_events=${18}
    local calloc_events=${19}
    local realloc_events=${20}
    local free_events=${21}
    local alloc_records=${22}
    local free_records=${23}
    local unmatched_free=${24}
    local ringbuf_drops=${25}
    local observed_events=${26}
    local logs=${27}

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "${row_kind}" \
        "${backend}" \
        "${mode}" \
        "${tracking_mode}" \
        "${pid_tid_cache}" \
        "${threads}" \
        "${size}" \
        "${total_ops}" \
        "${iters_per_thread}" \
        "${baseline_elapsed}" \
        "${baseline_ops}" \
        "${with_probe_elapsed}" \
        "${with_probe_ops}" \
        "${elapsed_over_baseline}" \
        "${optimized_elapsed}" \
        "${saved_vs_optimized}" \
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
        "${logs}" \
        >>"${CSV_PATH}"
}

record_ld_preload_stage6()
{
    local mode=$1
    local tracking_mode=$2
    local pid_tid_cache=$3
    local threads=$4
    local size=$5
    local iters_per_thread=$6
    local total_ops=$7
    local baseline_elapsed=$8
    local baseline_ops=$9
    local consumer_log="${LOG_DIR}/libbpf_fixed_${mode}_${tracking_mode}_cache${pid_tid_cache}_t${threads}_s${size}_ops${total_ops}.consumer.log"
    local hook_output hook_line with_probe_elapsed with_probe_ops elapsed_over_baseline
    local wake_line records alloc free key optimized_elapsed saved_vs_optimized

    start_consumer "${consumer_log}"
    hook_output=$(run_stage6_workload "${pid_tid_cache}" "${tracking_mode}" "${threads}" "${iters_per_thread}" "${size}")
    sleep 1
    stop_consumer

    hook_line=$(printf '%s\n' "${hook_output}" | tail -n1)
    with_probe_elapsed=$(extract_metric "${hook_line}" "elapsed_seconds" "0")
    with_probe_ops=$(extract_metric "${hook_line}" "throughput_ops" "0")
    elapsed_over_baseline=$(subtract_seconds "${with_probe_elapsed}" "${baseline_elapsed}")
    wake_line=$(grep '^wake=' "${consumer_log}" | tail -n1 || true)
    records=$(extract_metric "${wake_line}" "records" "0")
    alloc=$(extract_metric "${wake_line}" "alloc" "0")
    free=$(extract_metric "${wake_line}" "free" "0")

    key="${threads},${size}"
    if [[ "${mode}" == "ld_preload_stage6_optimized" ]]; then
        optimized_elapsed_by_key["${key}"]="${with_probe_elapsed}"
        optimized_elapsed="${with_probe_elapsed}"
        saved_vs_optimized="0.000000000"
    else
        optimized_elapsed=""
        saved_vs_optimized=""
    fi

    write_csv_row "ld_preload" "ld_preload" "${mode}" "${tracking_mode}" "${pid_tid_cache}" \
        "${threads}" "${size}" "${total_ops}" "${iters_per_thread}" \
        "${baseline_elapsed}" "${baseline_ops}" "${with_probe_elapsed}" "${with_probe_ops}" \
        "${elapsed_over_baseline}" "${optimized_elapsed}" "${saved_vs_optimized}" \
        "${records}" "${alloc}" 0 0 "${free}" "${alloc}" "${free}" 0 0 0 "${consumer_log}"

    printf '[ld_preload %s mode=%s cache=%s] threads=%s size=%s total_ops=%s elapsed=%s over_baseline=%s records=%s\n' \
        "${mode}" "${tracking_mode}" "${pid_tid_cache}" "${threads}" "${size}" "${total_ops}" \
        "${with_probe_elapsed}" "${elapsed_over_baseline}" "${records}"
}

record_libbpf_mode()
{
    local mode=$1
    local threads=$2
    local size=$3
    local iters_per_thread=$4
    local total_ops=$5
    local baseline_elapsed=$6
    local baseline_ops=$7
    local libc_path=$8
    local probe_log="${LOG_DIR}/libbpf_fixed_${mode}_t${threads}_s${size}_ops${total_ops}.loader.log"
    local probe_output workload_line probe_line with_probe_elapsed with_probe_ops elapsed_over_baseline
    local malloc_events calloc_events realloc_events free_events alloc_records free_records unmatched_free
    local output_records ringbuf_drops observed_events records_or_events key optimized_elapsed saved_vs_optimized

    probe_output=$("${LOADER_BIN}" \
        --mode "${mode}" \
        --libc "${libc_path}" \
        --sample-interval "${SAMPLE_INTERVAL}" \
        --filter-size "${FILTER_SIZE}" \
        -- "${WORKLOAD}" --threads "${threads}" --iters-per-thread "${iters_per_thread}" --size "${size}" 2>&1)
    printf '%s\n' "${probe_output}" >"${probe_log}"

    workload_line=$(printf '%s\n' "${probe_output}" | grep '^mode=' | tail -n1 || true)
    probe_line=$(printf '%s\n' "${probe_output}" | grep '^lnhv1_libbpf_summary' | tail -n1 || true)
    with_probe_elapsed=$(extract_metric "${workload_line}" "elapsed_seconds" "0")
    with_probe_ops=$(extract_metric "${workload_line}" "throughput_ops" "0")
    elapsed_over_baseline=$(subtract_seconds "${with_probe_elapsed}" "${baseline_elapsed}")
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

    key="${threads},${size}"
    optimized_elapsed="${optimized_elapsed_by_key[${key}]:-0}"
    saved_vs_optimized=$(subtract_seconds "${optimized_elapsed}" "${with_probe_elapsed}")

    write_csv_row "ebpf_uprobe" "libbpf" "${mode}" "none" "n/a" \
        "${threads}" "${size}" "${total_ops}" "${iters_per_thread}" \
        "${baseline_elapsed}" "${baseline_ops}" "${with_probe_elapsed}" "${with_probe_ops}" \
        "${elapsed_over_baseline}" "${optimized_elapsed}" "${saved_vs_optimized}" \
        "${records_or_events}" "${malloc_events}" "${calloc_events}" "${realloc_events}" "${free_events}" \
        "${alloc_records}" "${free_records}" "${unmatched_free}" "${ringbuf_drops}" "${observed_events}" "${probe_log}"

    printf '[libbpf %s] threads=%s size=%s total_ops=%s elapsed=%s saved_vs_ld_opt=%s events=%s malloc=%s free=%s drops=%s observed=%s\n' \
        "${mode}" "${threads}" "${size}" "${total_ops}" "${with_probe_elapsed}" "${saved_vs_optimized}" \
        "${records_or_events}" "${malloc_events}" "${free_events}" "${ringbuf_drops}" "${observed_events}"
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
row_kind,backend,mode,tracking_mode,pid_tid_cache,threads,size,total_ops,iters_per_thread,baseline_elapsed_sec,baseline_ops,with_probe_elapsed_sec,with_probe_ops,elapsed_over_baseline_sec,ld_preload_optimized_elapsed_sec,elapsed_saved_vs_ld_preload_optimized_sec,records_or_events,malloc_events,calloc_events,realloc_events,free_events,alloc_records,free_records,unmatched_free,ringbuf_drops,observed_events,logs
EOF

for threads in "${THREAD_VALUES[@]}"; do
    if (( FIXED_TOTAL_OPS % threads != 0 )); then
        echo "LNHV1_FIXED_TOTAL_OPS=${FIXED_TOTAL_OPS} must be divisible by threads=${threads}" >&2
        exit 1
    fi
    iters_per_thread=$(( FIXED_TOTAL_OPS / threads ))

    for size in "${SIZE_VALUES[@]}"; do
        baseline_output=$(run_workload "${threads}" "${iters_per_thread}" "${size}")
        baseline_line=$(printf '%s\n' "${baseline_output}" | tail -n1)
        baseline_elapsed=$(extract_metric "${baseline_line}" "elapsed_seconds" "0")
        baseline_ops=$(extract_metric "${baseline_line}" "throughput_ops" "0")

        write_csv_row "baseline" "none" "baseline_no_hook" "none" "n/a" \
            "${threads}" "${size}" "${FIXED_TOTAL_OPS}" "${iters_per_thread}" \
            "${baseline_elapsed}" "${baseline_ops}" "${baseline_elapsed}" "${baseline_ops}" \
            "0.000000000" "" "" 0 0 0 0 0 0 0 0 0 0 ""
        printf '[baseline] threads=%s size=%s total_ops=%s elapsed=%s throughput_ops=%s\n' \
            "${threads}" "${size}" "${FIXED_TOTAL_OPS}" "${baseline_elapsed}" "${baseline_ops}"

        record_ld_preload_stage6 "ld_preload_stage6_default" "global" 0 \
            "${threads}" "${size}" "${iters_per_thread}" "${FIXED_TOTAL_OPS}" "${baseline_elapsed}" "${baseline_ops}"
        record_ld_preload_stage6 "ld_preload_stage6_optimized" "${OPT_TRACKING_MODE}" 1 \
            "${threads}" "${size}" "${iters_per_thread}" "${FIXED_TOTAL_OPS}" "${baseline_elapsed}" "${baseline_ops}"

        if [[ "${RUN_LIBBPF}" == "1" ]]; then
            for mode in "${LIBBPF_MODE_VALUES[@]}"; do
                record_libbpf_mode "${mode}" "${threads}" "${size}" "${iters_per_thread}" "${FIXED_TOTAL_OPS}" \
                    "${baseline_elapsed}" "${baseline_ops}" "${LIBC_PATH}"
            done
        fi
    done
done

echo "csv written to ${CSV_PATH}"
