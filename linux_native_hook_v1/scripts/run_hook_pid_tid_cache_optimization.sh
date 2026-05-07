#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

SOCKET_PATH=${LNHV1_SOCKET_PATH:-"/tmp/linux_native_hook_v1_pid_tid_cache_$$.sock"}
FLUSH_THRESHOLD=${LNHV1_FLUSH_THRESHOLD:-20}
SAMPLE_INTERVAL=${LNHV1_SAMPLE_INTERVAL:-1}
FILTER_SIZE=${LNHV1_FILTER_SIZE:--1}
BLOCKED=${LNHV1_BLOCKED:-0}
CAPACITY=${LNHV1_CAPACITY:-4096}
THREADS_LIST=${LNHV1_THREADS_LIST:-1,4}
SIZE_LIST=${LNHV1_SIZE_LIST:-32}
DURATION=${LNHV1_DURATION:-5}
STAGE_LIST=${LNHV1_ABLATION_STAGE_LIST:-5,6}
CACHE_LIST=${LNHV1_PID_TID_CACHE_LIST:-0,1}
CSV_PATH=${LNHV1_CSV_PATH:-"${ROOT_DIR}/results/hook_pid_tid_cache_optimization_server_$(date +%F).csv"}
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

stage_name()
{
    case "$1" in
        4) printf 'tracking' ;;
        5) printf 'record_write' ;;
        6) printf 'notify' ;;
        *) printf 'unknown' ;;
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

run_stage4_workload()
{
    local threads=$1
    local duration=$2
    local size=$3

    LNHV1_ABLATION_STAGE=4 LNHV1_SUBABLATION_STAGE=4 LD_PRELOAD="${HOOK_LIB}" \
        "${WORKLOAD}" --threads "${threads}" --duration "${duration}" --size "${size}"
}

run_hooked_workload()
{
    local stage=$1
    local cache_enabled=$2
    local threads=$3
    local duration=$4
    local size=$5

    env -u LNHV1_SUBABLATION_STAGE \
        LNHV1_ABLATION_STAGE="${stage}" \
        LNHV1_PID_TID_CACHE="${cache_enabled}" \
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
IFS=',' read -r -a CACHE_VALUES <<< "${CACHE_LIST}"

cat >"${CSV_PATH}" <<'EOF'
stage,stage_name,pid_tid_cache,threads,size,duration,flush_threshold,sample_interval,filter_size,blocked,baseline_ops,with_hook_ops,overhead_pct,records,alloc,free,thread_name,flush,dropped,avg_batch,consumer_log
EOF

write_csv_row()
{
    local stage=$1
    local stage_name_value=$2
    local cache_enabled=$3
    local threads=$4
    local size=$5
    local baseline_ops=$6
    local with_hook_ops=$7
    local overhead_pct=$8
    local records=$9
    local alloc=${10}
    local free=${11}
    local thread_name=${12}
    local flush=${13}
    local dropped=${14}
    local avg_batch=${15}
    local consumer_log=${16}

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "${stage}" \
        "${stage_name_value}" \
        "${cache_enabled}" \
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
        "${records}" \
        "${alloc}" \
        "${free}" \
        "${thread_name}" \
        "${flush}" \
        "${dropped}" \
        "${avg_batch}" \
        "${consumer_log}" \
        >>"${CSV_PATH}"
}

for threads in "${THREAD_VALUES[@]}"; do
    for size in "${SIZE_VALUES[@]}"; do
        baseline_output=$(run_workload "${threads}" "${DURATION}" "${size}")
        baseline_line=$(printf '%s\n' "${baseline_output}" | tail -n1)
        baseline_ops=$(extract_metric "${baseline_line}" "throughput_ops" "0")
        write_csv_row 0 baseline 0 "${threads}" "${size}" "${baseline_ops}" "${baseline_ops}" "0.0000" 0 0 0 0 0 0 0 ""

        stage4_output=$(run_stage4_workload "${threads}" "${DURATION}" "${size}")
        stage4_line=$(printf '%s\n' "${stage4_output}" | tail -n1)
        stage4_ops=$(extract_metric "${stage4_line}" "throughput_ops" "0")
        stage4_overhead=$(compute_overhead_pct "${baseline_ops}" "${stage4_ops}")
        write_csv_row 4 tracking 0 "${threads}" "${size}" "${baseline_ops}" "${stage4_ops}" "${stage4_overhead}" 0 0 0 0 0 0 0 ""

        printf '[stage 4 tracking] threads=%s size=%s baseline_ops=%s with_hook_ops=%s overhead_pct=%s\n' \
            "${threads}" "${size}" "${baseline_ops}" "${stage4_ops}" "${stage4_overhead}"

        for stage in "${STAGE_VALUES[@]}"; do
            name=$(stage_name "${stage}")
            if [[ "${name}" == "unknown" ]]; then
                echo "invalid ablation stage for pid/tid cache optimization: ${stage}" >&2
                exit 1
            fi

            for cache_enabled in "${CACHE_VALUES[@]}"; do
                if [[ "${cache_enabled}" != "0" && "${cache_enabled}" != "1" ]]; then
                    echo "invalid LNHV1_PID_TID_CACHE value in list: ${cache_enabled}" >&2
                    exit 1
                fi

                log_suffix="pid_tid_cache_stage${stage}_${name}_cache${cache_enabled}_t${threads}_s${size}_d${DURATION}"
                consumer_log="${ROOT_DIR}/results/logs/${log_suffix}.consumer.log"
                records=0
                alloc=0
                free=0
                thread_name=0
                flush=0
                dropped=0
                avg_batch=0

                start_consumer "${consumer_log}"
                hook_output=$(run_hooked_workload "${stage}" "${cache_enabled}" "${threads}" "${DURATION}" "${size}")
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
                write_csv_row "${stage}" "${name}" "${cache_enabled}" "${threads}" "${size}" \
                    "${baseline_ops}" "${with_hook_ops}" "${overhead_pct}" \
                    "${records}" "${alloc}" "${free}" "${thread_name}" "${flush}" "${dropped}" "${avg_batch}" "${consumer_log}"

                printf '[stage %s %s cache=%s] threads=%s size=%s baseline_ops=%s with_hook_ops=%s overhead_pct=%s records=%s flush=%s dropped=%s avg_batch=%s\n' \
                    "${stage}" "${name}" "${cache_enabled}" "${threads}" "${size}" \
                    "${baseline_ops}" "${with_hook_ops}" "${overhead_pct}" \
                    "${records}" "${flush}" "${dropped}" "${avg_batch}"
            done
        done
    done
done

echo "csv written to ${CSV_PATH}"
