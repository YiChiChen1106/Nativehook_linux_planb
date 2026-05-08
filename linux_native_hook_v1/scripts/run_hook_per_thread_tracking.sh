#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

SOCKET_PATH=${LNHV1_SOCKET_PATH:-"/tmp/linux_native_hook_v1_per_thread_tracking_$$.sock"}
FLUSH_THRESHOLD=${LNHV1_FLUSH_THRESHOLD:-20}
SAMPLE_INTERVAL=${LNHV1_SAMPLE_INTERVAL:-1}
FILTER_SIZE=${LNHV1_FILTER_SIZE:--1}
BLOCKED=${LNHV1_BLOCKED:-0}
CAPACITY=${LNHV1_CAPACITY:-4096}
THREADS_LIST=${LNHV1_THREADS_LIST:-1,4}
SIZE_LIST=${LNHV1_SIZE_LIST:-32}
DURATION=${LNHV1_DURATION:-5}
TRACKING_MODE_LIST=${LNHV1_TRACKING_MODE_LIST:-global,sharded,thread_local_fallback,thread_local_only}
CSV_PATH=${LNHV1_CSV_PATH:-"${ROOT_DIR}/results/hook_per_thread_tracking_server_$(date +%F).csv"}
AUTO_BUILD=${LNHV1_AUTO_BUILD:-1}

CONSUMER_BIN="${ROOT_DIR}/build/consumer"
HOOK_LIB="${ROOT_DIR}/build/hook_preload.so"
WORKLOAD="${ROOT_DIR}/build/perf_test_data_linux"
CROSS_THREAD_WORKLOAD="${ROOT_DIR}/build/cross_thread_free_linux"

consumer_pid=""

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

validate_tracking_mode()
{
    case "$1" in
        global|sharded|thread_local_fallback|thread_local_only) return 0 ;;
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

run_stage3_workload()
{
    local threads=$1
    local duration=$2
    local size=$3

    env -u LNHV1_SUBABLATION_STAGE -u LNHV1_TRACKING_MODE \
        LNHV1_ABLATION_STAGE=3 \
        LD_PRELOAD="${HOOK_LIB}" \
        "${WORKLOAD}" --threads "${threads}" --duration "${duration}" --size "${size}"
}

run_stage4_workload()
{
    local tracking_mode=$1
    local threads=$2
    local duration=$3
    local size=$4

    LNHV1_ABLATION_STAGE=4 \
        LNHV1_SUBABLATION_STAGE=4 \
        LNHV1_TRACKING_MODE="${tracking_mode}" \
        LD_PRELOAD="${HOOK_LIB}" \
        "${WORKLOAD}" --threads "${threads}" --duration "${duration}" --size "${size}"
}

run_full_workload()
{
    local stage=$1
    local tracking_mode=$2
    local threads=$3
    local duration=$4
    local size=$5

    env -u LNHV1_SUBABLATION_STAGE \
        LNHV1_ABLATION_STAGE="${stage}" \
        LNHV1_PID_TID_CACHE=1 \
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
    g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I"${ROOT_DIR}" \
        "${ROOT_DIR}/tests/cross_thread_free_linux.cpp" \
        -o "${CROSS_THREAD_WORKLOAD}" -pthread
    g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -fPIC -shared -I"${ROOT_DIR}" \
        "${ROOT_DIR}/producer_hook/ablation_config.cpp" \
        "${ROOT_DIR}/producer_hook/hook_preload.cpp" \
        "${ROOT_DIR}/producer_hook/hook_guard.cpp" \
        "${ROOT_DIR}/producer_hook/hook_writer.cpp" \
        "${ROOT_DIR}/common/socket_fd.cpp" \
        -o "${HOOK_LIB}" -pthread -ldl -lrt
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
mkdir -p "${ROOT_DIR}/results/logs"

require_binary "${CONSUMER_BIN}"
require_binary "${WORKLOAD}"
require_binary "${CROSS_THREAD_WORKLOAD}"
if [[ ! -f "${HOOK_LIB}" ]]; then
    echo "missing hook library: ${HOOK_LIB}" >&2
    exit 1
fi

IFS=',' read -r -a THREAD_VALUES <<< "${THREADS_LIST}"
IFS=',' read -r -a SIZE_VALUES <<< "${SIZE_LIST}"
IFS=',' read -r -a TRACKING_MODE_VALUES <<< "${TRACKING_MODE_LIST}"

cat >"${CSV_PATH}" <<'EOF'
main_stage,main_stage_name,tracking_mode,pid_tid_cache,threads,size,duration,flush_threshold,sample_interval,filter_size,blocked,baseline_ops,with_hook_ops,overhead_pct,consumer_log
EOF

write_csv_row()
{
    local main_stage=$1
    local main_stage_name=$2
    local tracking_mode=$3
    local pid_tid_cache=$4
    local threads=$5
    local size=$6
    local baseline_ops=$7
    local with_hook_ops=$8
    local overhead_pct=$9
    local consumer_log=${10}

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "${main_stage}" \
        "${main_stage_name}" \
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
        "${with_hook_ops}" \
        "${overhead_pct}" \
        "${consumer_log}" \
        >>"${CSV_PATH}"
}

record_hook_result()
{
    local main_stage=$1
    local main_stage_name=$2
    local tracking_mode=$3
    local pid_tid_cache=$4
    local threads=$5
    local size=$6
    local baseline_ops=$7
    local hook_output=$8
    local consumer_log=$9
    local hook_line
    local with_hook_ops
    local overhead_pct

    hook_line=$(printf '%s\n' "${hook_output}" | tail -n1)
    with_hook_ops=$(extract_metric "${hook_line}" "throughput_ops" "0")
    overhead_pct=$(compute_overhead_pct "${baseline_ops}" "${with_hook_ops}")
    write_csv_row "${main_stage}" "${main_stage_name}" "${tracking_mode}" "${pid_tid_cache}" \
        "${threads}" "${size}" "${baseline_ops}" "${with_hook_ops}" "${overhead_pct}" "${consumer_log}"

    printf '[stage %s %s mode=%s cache=%s] threads=%s size=%s baseline_ops=%s with_hook_ops=%s overhead_pct=%s\n' \
        "${main_stage}" "${main_stage_name}" "${tracking_mode}" "${pid_tid_cache}" \
        "${threads}" "${size}" "${baseline_ops}" "${with_hook_ops}" "${overhead_pct}"
}

run_full_reference()
{
    local stage=$1
    local stage_name_value=$2
    local tracking_mode=$3
    local threads=$4
    local size=$5
    local baseline_ops=$6
    local consumer_log
    local hook_output

    consumer_log="${ROOT_DIR}/results/logs/per_thread_tracking_stage${stage}_${stage_name_value}_${tracking_mode}_cache1_t${threads}_s${size}_d${DURATION}.consumer.log"
    start_consumer "${consumer_log}"
    hook_output=$(run_full_workload "${stage}" "${tracking_mode}" "${threads}" "${DURATION}" "${size}")
    sleep 1
    stop_consumer

    record_hook_result "${stage}" "${stage_name_value}" "${tracking_mode}" 1 \
        "${threads}" "${size}" "${baseline_ops}" "${hook_output}" "${consumer_log}"
}

for mode in "${TRACKING_MODE_VALUES[@]}"; do
    if ! validate_tracking_mode "${mode}"; then
        echo "invalid tracking mode: ${mode}" >&2
        exit 1
    fi
done

for threads in "${THREAD_VALUES[@]}"; do
    for size in "${SIZE_VALUES[@]}"; do
        baseline_output=$(run_workload "${threads}" "${DURATION}" "${size}")
        baseline_line=$(printf '%s\n' "${baseline_output}" | tail -n1)
        baseline_ops=$(extract_metric "${baseline_line}" "throughput_ops" "0")
        write_csv_row 0 baseline none 0 "${threads}" "${size}" "${baseline_ops}" "${baseline_ops}" "0.0000" ""

        stage3_output=$(run_stage3_workload "${threads}" "${DURATION}" "${size}")
        record_hook_result 3 mutex none 0 "${threads}" "${size}" "${baseline_ops}" "${stage3_output}" ""

        for tracking_mode in "${TRACKING_MODE_VALUES[@]}"; do
            stage4_output=$(run_stage4_workload "${tracking_mode}" "${threads}" "${DURATION}" "${size}")
            record_hook_result 4 tracking "${tracking_mode}" 0 "${threads}" "${size}" "${baseline_ops}" "${stage4_output}" ""

            run_full_reference 5 record_write "${tracking_mode}" "${threads}" "${size}" "${baseline_ops}"
            run_full_reference 6 notify "${tracking_mode}" "${threads}" "${size}" "${baseline_ops}"
        done
    done
done

echo "csv written to ${CSV_PATH}"
