#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

SOCKET_PATH=${LNHV1_SOCKET_PATH:-"/tmp/linux_native_hook_v1_writer_ring_impact_$$.sock"}
FLUSH_THRESHOLD=${LNHV1_FLUSH_THRESHOLD:-20}
SAMPLE_INTERVAL=${LNHV1_SAMPLE_INTERVAL:-1}
FILTER_SIZE=${LNHV1_FILTER_SIZE:--1}
BLOCKED=${LNHV1_BLOCKED:-0}
CAPACITY=${LNHV1_CAPACITY:-4096}
THREADS_LIST=${LNHV1_THREADS_LIST:-1,4,8,16}
SIZE_LIST=${LNHV1_SIZE_LIST:-32}
FIXED_TOTAL_OPS=${LNHV1_FIXED_TOTAL_OPS:-1000000}
ALLOC_PATTERN=${LNHV1_ALLOC_PATTERN:-mixed3}
MAIN_STAGE=${LNHV1_MAIN_STAGE:-6}
TRACKING_MODE=${LNHV1_TRACKING_MODE:-thread_local_fallback}
PID_TID_CACHE=${LNHV1_PID_TID_CACHE:-1}
SUB_STAGE_LIST=${LNHV1_SUBABLATION_STAGE_LIST:-28,29,30,31,32,33}
CSV_PATH=${LNHV1_CSV_PATH:-"${ROOT_DIR}/results/hook_writer_ring_impact_server_2026-05-28.csv"}
AUTO_BUILD=${LNHV1_AUTO_BUILD:-1}

CONSUMER_BIN="${ROOT_DIR}/build/consumer"
HOOK_LIB="${ROOT_DIR}/build/hook_preload.so"
WORKLOAD="${ROOT_DIR}/build/perf_test_data_linux"

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

sub_stage_name()
{
    case "$1" in
        0) printf 'baseline' ;;
        28) printf 'stage6_opt_no_writer_ring' ;;
        29) printf 'stage6_opt_writer_mutex_only' ;;
        30) printf 'stage6_opt_ring_index_check' ;;
        31) printf 'stage6_opt_record_copy_no_publish' ;;
        32) printf 'stage6_opt_atomic_publish_no_notify' ;;
        33) printf 'stage6_opt_full_notify' ;;
        *) printf 'unknown' ;;
    esac
}

sub_stage_uses_consumer()
{
    case "$1" in
        30|31|32|33) return 0 ;;
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

run_workload()
{
    local threads=$1
    local iters_per_thread=$2
    local size=$3

    "${WORKLOAD}" --threads "${threads}" --iters-per-thread "${iters_per_thread}" --size "${size}"
}

run_stage6_substage_workload()
{
    local sub_stage=$1
    local threads=$2
    local iters_per_thread=$3
    local size=$4

    if [[ "${sub_stage}" == "33" ]]; then
        env -u LNHV1_SUBABLATION_STAGE \
            LNHV1_ABLATION_STAGE="${MAIN_STAGE}" \
            LNHV1_PID_TID_CACHE="${PID_TID_CACHE}" \
            LNHV1_TRACKING_MODE="${TRACKING_MODE}" \
            LNHV1_SOCKET_PATH="${SOCKET_PATH}" \
            LD_PRELOAD="${HOOK_LIB}" \
            "${WORKLOAD}" --threads "${threads}" --iters-per-thread "${iters_per_thread}" --size "${size}"
        return
    fi

    LNHV1_ABLATION_STAGE="${MAIN_STAGE}" \
        LNHV1_SUBABLATION_STAGE="${sub_stage}" \
        LNHV1_PID_TID_CACHE="${PID_TID_CACHE}" \
        LNHV1_TRACKING_MODE="${TRACKING_MODE}" \
        LNHV1_SOCKET_PATH="${SOCKET_PATH}" \
        LD_PRELOAD="${HOOK_LIB}" \
        "${WORKLOAD}" --threads "${threads}" --iters-per-thread "${iters_per_thread}" --size "${size}"
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
if [[ ! -f "${HOOK_LIB}" ]]; then
    echo "missing hook library: ${HOOK_LIB}" >&2
    exit 1
fi

IFS=',' read -r -a THREAD_VALUES <<< "${THREADS_LIST}"
IFS=',' read -r -a SIZE_VALUES <<< "${SIZE_LIST}"
IFS=',' read -r -a SUB_STAGE_VALUES <<< "${SUB_STAGE_LIST}"

cat >"${CSV_PATH}" <<'EOF'
main_stage,main_stage_name,sub_stage,sub_stage_name,tracking_mode,pid_tid_cache,threads,size,total_ops,iters_per_thread,alloc_pattern,baseline_elapsed_sec,baseline_ops,with_hook_elapsed_sec,with_hook_ops,elapsed_over_baseline_sec,records,consumer_log
EOF

write_csv_row()
{
    local main_stage=$1
    local main_stage_name=$2
    local sub_stage=$3
    local sub_stage_name_value=$4
    local tracking_mode=$5
    local pid_tid_cache=$6
    local threads=$7
    local size=$8
    local total_ops=$9
    local iters_per_thread=${10}
    local alloc_pattern=${11}
    local baseline_elapsed=${12}
    local baseline_ops=${13}
    local with_hook_elapsed=${14}
    local with_hook_ops=${15}
    local elapsed_over_baseline=${16}
    local records=${17}
    local consumer_log=${18}

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "${main_stage}" \
        "${main_stage_name}" \
        "${sub_stage}" \
        "${sub_stage_name_value}" \
        "${tracking_mode}" \
        "${pid_tid_cache}" \
        "${threads}" \
        "${size}" \
        "${total_ops}" \
        "${iters_per_thread}" \
        "${alloc_pattern}" \
        "${baseline_elapsed}" \
        "${baseline_ops}" \
        "${with_hook_elapsed}" \
        "${with_hook_ops}" \
        "${elapsed_over_baseline}" \
        "${records}" \
        "${consumer_log}" \
        >>"${CSV_PATH}"
}

record_result()
{
    local main_stage=$1
    local main_stage_name=$2
    local sub_stage=$3
    local sub_stage_name_value=$4
    local threads=$5
    local size=$6
    local total_ops=$7
    local iters_per_thread=$8
    local baseline_elapsed=$9
    local baseline_ops=${10}
    local hook_output=${11}
    local consumer_log=${12}
    local hook_line
    local with_hook_elapsed
    local with_hook_ops
    local elapsed_over_baseline
    local records

    hook_line=$(printf '%s\n' "${hook_output}" | tail -n1)
    with_hook_elapsed=$(extract_metric "${hook_line}" "elapsed_seconds" "0")
    with_hook_ops=$(extract_metric "${hook_line}" "throughput_ops" "0")
    elapsed_over_baseline=$(subtract_seconds "${with_hook_elapsed}" "${baseline_elapsed}")
    records="0"
    if [[ -n "${consumer_log}" && -f "${consumer_log}" ]]; then
        local wake_line
        wake_line=$(grep '^wake=' "${consumer_log}" | tail -n1 || true)
        records=$(extract_metric "${wake_line}" "records" "0")
    fi

    write_csv_row "${main_stage}" "${main_stage_name}" "${sub_stage}" "${sub_stage_name_value}" \
        "${TRACKING_MODE}" "${PID_TID_CACHE}" "${threads}" "${size}" "${total_ops}" "${iters_per_thread}" \
        "${ALLOC_PATTERN}" "${baseline_elapsed}" "${baseline_ops}" "${with_hook_elapsed}" "${with_hook_ops}" \
        "${elapsed_over_baseline}" "${records}" "${consumer_log}"

    printf '[stage %s %s sub=%s %s] threads=%s size=%s total_ops=%s with_hook_elapsed=%s over_baseline=%s records=%s\n' \
        "${main_stage}" "${main_stage_name}" "${sub_stage}" "${sub_stage_name_value}" \
        "${threads}" "${size}" "${total_ops}" "${with_hook_elapsed}" "${elapsed_over_baseline}" "${records}"
}

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
        write_csv_row 0 baseline 0 baseline "${TRACKING_MODE}" "${PID_TID_CACHE}" \
            "${threads}" "${size}" "${FIXED_TOTAL_OPS}" "${iters_per_thread}" "${ALLOC_PATTERN}" \
            "${baseline_elapsed}" "${baseline_ops}" "${baseline_elapsed}" "${baseline_ops}" "0.000000000" 0 ""
        printf '[baseline] threads=%s size=%s total_ops=%s elapsed=%s throughput_ops=%s\n' \
            "${threads}" "${size}" "${FIXED_TOTAL_OPS}" "${baseline_elapsed}" "${baseline_ops}"

        for sub_stage in "${SUB_STAGE_VALUES[@]}"; do
            name=$(sub_stage_name "${sub_stage}")
            if [[ "${name}" == "unknown" ]]; then
                echo "invalid writer/ring impact sub-ablation stage: ${sub_stage}" >&2
                exit 1
            fi

            consumer_log=""
            if sub_stage_uses_consumer "${sub_stage}"; then
                consumer_log="${ROOT_DIR}/results/logs/writer_ring_impact_substage${sub_stage}_${name}_t${threads}_s${size}_ops${FIXED_TOTAL_OPS}.consumer.log"
                start_consumer "${consumer_log}"
            fi

            hook_output=$(run_stage6_substage_workload "${sub_stage}" "${threads}" "${iters_per_thread}" "${size}")

            if [[ -n "${consumer_log}" ]]; then
                sleep 1
                stop_consumer
            fi

            record_result 6 stage6_optimized "${sub_stage}" "${name}" \
                "${threads}" "${size}" "${FIXED_TOTAL_OPS}" "${iters_per_thread}" \
                "${baseline_elapsed}" "${baseline_ops}" "${hook_output}" "${consumer_log}"
        done
    done
done

echo "csv written to ${CSV_PATH}"
