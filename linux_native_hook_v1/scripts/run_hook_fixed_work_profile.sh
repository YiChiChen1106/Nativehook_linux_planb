#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

SOCKET_PATH=${LNHV1_SOCKET_PATH:-"/tmp/linux_native_hook_v1_fixed_work_profile_$$.sock"}
FLUSH_THRESHOLD=${LNHV1_FLUSH_THRESHOLD:-20}
SAMPLE_INTERVAL=${LNHV1_SAMPLE_INTERVAL:-1}
FILTER_SIZE=${LNHV1_FILTER_SIZE:--1}
BLOCKED=${LNHV1_BLOCKED:-0}
CAPACITY=${LNHV1_CAPACITY:-4096}
THREADS_LIST=${LNHV1_THREADS_LIST:-1,4}
SIZE_LIST=${LNHV1_SIZE_LIST:-32}
FIXED_TOTAL_OPS=${LNHV1_FIXED_TOTAL_OPS:-1000000}
STAGE_LIST=${LNHV1_ABLATION_STAGE_LIST:-5,6}
SCENARIO_LIST=${LNHV1_FIXED_PROFILE_SCENARIO_LIST:-before_global_cache_off,global_cache_on,sharded_cache_on,thread_local_fallback_cache_on}
CSV_PATH=${LNHV1_CSV_PATH:-"${ROOT_DIR}/results/hook_fixed_work_profile_server_$(date +%F).csv"}
AUTO_BUILD=${LNHV1_AUTO_BUILD:-1}

CONSUMER_BIN="${ROOT_DIR}/build/consumer"
HOOK_LIB="${ROOT_DIR}/build/hook_preload.so"
WORKLOAD="${ROOT_DIR}/build/perf_test_data_linux"

consumer_pid=""

declare -A before_elapsed_by_key

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

stage_name()
{
    case "$1" in
        4) printf 'tracking' ;;
        5) printf 'record_write' ;;
        6) printf 'notify' ;;
        *) printf 'unknown' ;;
    esac
}

scenario_tracking_mode()
{
    case "$1" in
        before_global_cache_off|global_cache_on) printf 'global' ;;
        sharded_cache_on) printf 'sharded' ;;
        thread_local_fallback_cache_on) printf 'thread_local_fallback' ;;
        *) return 1 ;;
    esac
}

scenario_pid_tid_cache()
{
    case "$1" in
        before_global_cache_off) printf '0' ;;
        global_cache_on|sharded_cache_on|thread_local_fallback_cache_on) printf '1' ;;
        *) return 1 ;;
    esac
}

needs_consumer()
{
    [[ "$1" -ge 5 ]]
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

compute_saved_seconds()
{
    local before=$1
    local current=$2
    awk -v before="${before}" -v current="${current}" 'BEGIN {
        printf "%.9f", before - current
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
    local iters_per_thread=$2
    local size=$3

    "${WORKLOAD}" --threads "${threads}" --iters-per-thread "${iters_per_thread}" --size "${size}"
}

run_profiled_workload()
{
    local stage=$1
    local tracking_mode=$2
    local pid_tid_cache=$3
    local profile_path=$4
    local threads=$5
    local iters_per_thread=$6
    local size=$7

    env -u LNHV1_SUBABLATION_STAGE \
        LNHV1_ABLATION_STAGE="${stage}" \
        LNHV1_TRACKING_MODE="${tracking_mode}" \
        LNHV1_PID_TID_CACHE="${pid_tid_cache}" \
        LNHV1_HOTPATH_PROFILE=1 \
        LNHV1_HOTPATH_PROFILE_PATH="${profile_path}" \
        LNHV1_SOCKET_PATH="${SOCKET_PATH}" \
        LD_PRELOAD="${HOOK_LIB}" \
        "${WORKLOAD}" --threads "${threads}" --iters-per-thread "${iters_per_thread}" --size "${size}"
}

append_baseline_row()
{
    local threads=$1
    local size=$2
    local iters_per_thread=$3
    local total_ops=$4
    local elapsed_seconds=$5
    local throughput_ops=$6

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "baseline" \
        "baseline_no_hook" \
        "0" \
        "baseline" \
        "" \
        "" \
        "${threads}" \
        "${size}" \
        "${total_ops}" \
        "${iters_per_thread}" \
        "${elapsed_seconds}" \
        "${throughput_ops}" \
        "" \
        "" \
        "0.000000000" \
        "baseline" \
        0 0 0 0 0 "" "" >>"${CSV_PATH}"
}

append_profile_rows()
{
    local scenario=$1
    local stage=$2
    local name=$3
    local tracking_mode=$4
    local pid_tid_cache=$5
    local threads=$6
    local size=$7
    local iters_per_thread=$8
    local total_ops=$9
    local baseline_elapsed=${10}
    local baseline_ops=${11}
    local with_hook_elapsed=${12}
    local with_hook_ops=${13}
    local saved_vs_before=${14}
    local profile_log=${15}
    local consumer_log=${16}

    if [[ ! -s "${profile_log}" ]]; then
        printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
            "profile" "${scenario}" "${stage}" "${name}" "${tracking_mode}" "${pid_tid_cache}" \
            "${threads}" "${size}" "${total_ops}" "${iters_per_thread}" \
            "${baseline_elapsed}" "${baseline_ops}" "${with_hook_elapsed}" "${with_hook_ops}" "${saved_vs_before}" \
            "profile_missing" 0 0 0 0 0 "${profile_log}" "${consumer_log}" >>"${CSV_PATH}"
        return
    fi

    tail -n +2 "${profile_log}" | while IFS=, read -r segment count total_cycles avg_cycles total_ns avg_ns; do
        printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
            "profile" \
            "${scenario}" \
            "${stage}" \
            "${name}" \
            "${tracking_mode}" \
            "${pid_tid_cache}" \
            "${threads}" \
            "${size}" \
            "${total_ops}" \
            "${iters_per_thread}" \
            "${baseline_elapsed}" \
            "${baseline_ops}" \
            "${with_hook_elapsed}" \
            "${with_hook_ops}" \
            "${saved_vs_before}" \
            "${segment}" \
            "${count}" \
            "${total_cycles}" \
            "${avg_cycles}" \
            "${total_ns}" \
            "${avg_ns}" \
            "${profile_log}" \
            "${consumer_log}" \
            >>"${CSV_PATH}"
    done
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
IFS=',' read -r -a STAGE_VALUES <<< "${STAGE_LIST}"
IFS=',' read -r -a SCENARIO_VALUES <<< "${SCENARIO_LIST}"

cat >"${CSV_PATH}" <<'EOF'
row_kind,scenario,stage,stage_name,tracking_mode,pid_tid_cache,threads,size,total_ops,iters_per_thread,baseline_elapsed_sec,baseline_ops,with_hook_elapsed_sec,with_hook_ops,elapsed_saved_vs_before_sec,segment,count,total_cycles,avg_cycles,total_ns,avg_ns,profile_log,consumer_log
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
        total_iterations=$(extract_metric "${baseline_line}" "total_iterations" "${FIXED_TOTAL_OPS}")
        append_baseline_row "${threads}" "${size}" "${iters_per_thread}" "${total_iterations}" "${baseline_elapsed}" "${baseline_ops}"

        for scenario in "${SCENARIO_VALUES[@]}"; do
            tracking_mode=$(scenario_tracking_mode "${scenario}")
            pid_tid_cache=$(scenario_pid_tid_cache "${scenario}")

            for stage in "${STAGE_VALUES[@]}"; do
                name=$(stage_name "${stage}")
                if [[ "${name}" == "unknown" ]]; then
                    echo "invalid ablation stage for fixed profile: ${stage}" >&2
                    exit 1
                fi

                log_suffix="fixed_work_profile_${scenario}_stage${stage}_${name}_${tracking_mode}_cache${pid_tid_cache}_t${threads}_s${size}_ops${FIXED_TOTAL_OPS}"
                profile_log="${ROOT_DIR}/results/logs/${log_suffix}.profile.csv"
                consumer_log="${ROOT_DIR}/results/logs/${log_suffix}.consumer.log"
                rm -f "${profile_log}"

                if needs_consumer "${stage}"; then
                    start_consumer "${consumer_log}"
                else
                    consumer_log=""
                fi

                hook_output=$(run_profiled_workload \
                    "${stage}" "${tracking_mode}" "${pid_tid_cache}" "${profile_log}" \
                    "${threads}" "${iters_per_thread}" "${size}")
                hook_line=$(printf '%s\n' "${hook_output}" | tail -n1)
                with_hook_elapsed=$(extract_metric "${hook_line}" "elapsed_seconds" "0")
                with_hook_ops=$(extract_metric "${hook_line}" "throughput_ops" "0")

                if needs_consumer "${stage}"; then
                    sleep 1
                    stop_consumer
                fi

                key="${threads},${size},${stage}"
                if [[ "${scenario}" == "before_global_cache_off" ]]; then
                    before_elapsed_by_key["${key}"]="${with_hook_elapsed}"
                    saved_vs_before="0.000000000"
                else
                    before_elapsed="${before_elapsed_by_key[${key}]:-0}"
                    saved_vs_before=$(compute_saved_seconds "${before_elapsed}" "${with_hook_elapsed}")
                fi

                append_profile_rows \
                    "${scenario}" "${stage}" "${name}" "${tracking_mode}" "${pid_tid_cache}" \
                    "${threads}" "${size}" "${iters_per_thread}" "${FIXED_TOTAL_OPS}" \
                    "${baseline_elapsed}" "${baseline_ops}" "${with_hook_elapsed}" "${with_hook_ops}" \
                    "${saved_vs_before}" "${profile_log}" "${consumer_log}"

                printf '[fixed profile %s stage %s %s] tracking_mode=%s cache=%s threads=%s size=%s total_ops=%s elapsed=%s saved_vs_before=%s throughput_ops=%s profile=%s\n' \
                    "${scenario}" \
                    "${stage}" \
                    "${name}" \
                    "${tracking_mode}" \
                    "${pid_tid_cache}" \
                    "${threads}" \
                    "${size}" \
                    "${FIXED_TOTAL_OPS}" \
                    "${with_hook_elapsed}" \
                    "${saved_vs_before}" \
                    "${with_hook_ops}" \
                    "${profile_log}"
            done
        done
    done
done

echo "csv written to ${CSV_PATH}"
