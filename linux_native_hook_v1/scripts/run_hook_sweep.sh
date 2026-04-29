#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

SOCKET_PATH=${LNHV1_SOCKET_PATH:-/tmp/linux_native_hook_v1.sock}
FLUSH_THRESHOLD=${LNHV1_FLUSH_THRESHOLD:-20}
BLOCKED_LIST=${LNHV1_BLOCKED_LIST:-0}
THREADS_LIST=${LNHV1_THREADS_LIST:-1}
SIZE_LIST=${LNHV1_SIZE_LIST:-32}
SAMPLE_LIST=${LNHV1_SAMPLE_LIST:-1}
FILTER_LIST=${LNHV1_FILTER_LIST:--1}
DURATION=${LNHV1_DURATION:-1}
CSV_PATH=${LNHV1_CSV_PATH:-"${ROOT_DIR}/results/hook_sweep.csv"}

CONSUMER_BIN="${ROOT_DIR}/build/consumer"
HOOK_LIB="${ROOT_DIR}/build/hook_preload.so"
WORKLOAD="${ROOT_DIR}/build/perf_test_data_linux"

consumer_pid=""

cleanup()
{
    if [[ -n "${consumer_pid}" ]]; then
        stop_consumer
    fi
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
    local hook_ops=$2
    awk -v baseline="${baseline_ops}" -v hook="${hook_ops}" 'BEGIN {
        if (baseline == 0) {
            printf "0.0000"
        } else {
            printf "%.4f", ((baseline - hook) * 100.0) / baseline
        }
    }'
}

start_consumer()
{
    local sample_interval=$1
    local filter_size=$2
    local blocked=$3
    local consumer_log=$4

    consumer_args=(
        --socket "${SOCKET_PATH}"
        --flush-threshold "${FLUSH_THRESHOLD}"
        --sample-interval "${sample_interval}"
        --filter-size "${filter_size}"
    )
    if [[ "${blocked}" != "0" ]]; then
        consumer_args+=(--blocked)
    fi

    "${CONSUMER_BIN}" "${consumer_args[@]}" >"${consumer_log}" 2>&1 &
    consumer_pid=$!
    sleep 1

    if ! kill -0 "${consumer_pid}" >/dev/null 2>&1; then
        echo "consumer failed to start for sample_interval=${sample_interval} filter_size=${filter_size} blocked=${blocked}" >&2
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

run_hooked_workload()
{
    local threads=$1
    local duration=$2
    local size=$3

    LNHV1_SOCKET_PATH="${SOCKET_PATH}" LD_PRELOAD="${HOOK_LIB}" \
        "${WORKLOAD}" --threads "${threads}" --duration "${duration}" --size "${size}"
}

mkdir -p "$(dirname "${CSV_PATH}")"
mkdir -p "${ROOT_DIR}/results/logs"

require_binary "${CONSUMER_BIN}"
require_binary "${WORKLOAD}"
if [[ ! -f "${HOOK_LIB}" ]]; then
    echo "missing hook library: ${HOOK_LIB}" >&2
    exit 1
fi

IFS=',' read -r -a THREAD_VALUES <<< "${THREADS_LIST}"
IFS=',' read -r -a SIZE_VALUES <<< "${SIZE_LIST}"
IFS=',' read -r -a SAMPLE_VALUES <<< "${SAMPLE_LIST}"
IFS=',' read -r -a FILTER_VALUES <<< "${FILTER_LIST}"
IFS=',' read -r -a BLOCKED_VALUES <<< "${BLOCKED_LIST}"

cat >"${CSV_PATH}" <<'EOF'
threads,size,duration,flush_threshold,blocked,sample_interval,filter_size,baseline_ops,with_hook_ops,overhead_pct,records,alloc,free,thread_name,flush,dropped,avg_batch
EOF

for threads in "${THREAD_VALUES[@]}"; do
    for size in "${SIZE_VALUES[@]}"; do
        baseline_output=$(run_workload "${threads}" "${DURATION}" "${size}")
        baseline_line=$(printf '%s\n' "${baseline_output}" | tail -n1)
        baseline_ops=$(extract_metric "${baseline_line}" "throughput_ops" "0")

        for blocked in "${BLOCKED_VALUES[@]}"; do
            for sample_interval in "${SAMPLE_VALUES[@]}"; do
                for filter_size in "${FILTER_VALUES[@]}"; do
                    log_suffix="t${threads}_s${size}_d${DURATION}_b${blocked}_si${sample_interval}_f${filter_size}"
                    consumer_log="${ROOT_DIR}/results/logs/${log_suffix}.consumer.log"

                    start_consumer "${sample_interval}" "${filter_size}" "${blocked}" "${consumer_log}"
                    hook_output=$(run_hooked_workload "${threads}" "${DURATION}" "${size}")
                    hook_line=$(printf '%s\n' "${hook_output}" | tail -n1)
                    with_hook_ops=$(extract_metric "${hook_line}" "throughput_ops" "0")
                    sleep 1
                    stop_consumer

                    wake_line=$(grep '^wake=' "${consumer_log}" | tail -n1 || true)
                    records=$(extract_metric "${wake_line}" "records" "0")
                    alloc=$(extract_metric "${wake_line}" "alloc" "0")
                    free=$(extract_metric "${wake_line}" "free" "0")
                    thread_name=$(extract_metric "${wake_line}" "thread_name" "0")
                    flush=$(extract_metric "${wake_line}" "flush" "0")
                    dropped=$(extract_metric "${wake_line}" "dropped" "0")
                    avg_batch=$(extract_metric "${wake_line}" "avg_batch" "0")
                    overhead_pct=$(compute_overhead_pct "${baseline_ops}" "${with_hook_ops}")

                    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
                        "${threads}" \
                        "${size}" \
                        "${DURATION}" \
                        "${FLUSH_THRESHOLD}" \
                        "${blocked}" \
                        "${sample_interval}" \
                        "${filter_size}" \
                        "${baseline_ops}" \
                        "${with_hook_ops}" \
                        "${overhead_pct}" \
                        "${records}" \
                        "${alloc}" \
                        "${free}" \
                        "${thread_name}" \
                        "${flush}" \
                        "${dropped}" \
                        "${avg_batch}" \
                        >>"${CSV_PATH}"

                    printf '[case] threads=%s size=%s blocked=%s sample_interval=%s filter_size=%s baseline_ops=%s with_hook_ops=%s overhead_pct=%s records=%s flush=%s dropped=%s avg_batch=%s\n' \
                        "${threads}" \
                        "${size}" \
                        "${blocked}" \
                        "${sample_interval}" \
                        "${filter_size}" \
                        "${baseline_ops}" \
                        "${with_hook_ops}" \
                        "${overhead_pct}" \
                        "${records}" \
                        "${flush}" \
                        "${dropped}" \
                        "${avg_batch}"
                done
            done
        done
    done
done

echo "csv written to ${CSV_PATH}"
