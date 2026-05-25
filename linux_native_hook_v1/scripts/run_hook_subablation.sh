#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

SAMPLE_INTERVAL=${LNHV1_SAMPLE_INTERVAL:-1}
FILTER_SIZE=${LNHV1_FILTER_SIZE:--1}
THREADS_LIST=${LNHV1_THREADS_LIST:-1,4}
SIZE_LIST=${LNHV1_SIZE_LIST:-32}
DURATION=${LNHV1_DURATION:-5}
SUB_STAGE_LIST=${LNHV1_SUBABLATION_STAGE_LIST:-1,2,3,4}
CSV_PATH=${LNHV1_CSV_PATH:-"${ROOT_DIR}/results/hook_tracking_subablation_server_$(date +%F).csv"}
AUTO_BUILD=${LNHV1_AUTO_BUILD:-1}

HOOK_LIB="${ROOT_DIR}/build/hook_preload.so"
WORKLOAD="${ROOT_DIR}/build/perf_test_data_linux"

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
        0) printf 'no_subablation' ;;
        1) printf 'tracking_sample_filter_only' ;;
        2) printf 'tracking_insert_only' ;;
        3) printf 'tracking_lookup_only' ;;
        4) printf 'tracking_full_erase' ;;
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

    LNHV1_ABLATION_STAGE=3 LD_PRELOAD="${HOOK_LIB}" \
        "${WORKLOAD}" --threads "${threads}" --duration "${duration}" --size "${size}"
}

run_tracking_substage_workload()
{
    local sub_stage=$1
    local threads=$2
    local duration=$3
    local size=$4

    LNHV1_ABLATION_STAGE=4 LNHV1_SUBABLATION_STAGE="${sub_stage}" LD_PRELOAD="${HOOK_LIB}" \
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

require_binary "${WORKLOAD}"
if [[ ! -f "${HOOK_LIB}" ]]; then
    echo "missing hook library: ${HOOK_LIB}" >&2
    exit 1
fi

IFS=',' read -r -a THREAD_VALUES <<< "${THREADS_LIST}"
IFS=',' read -r -a SIZE_VALUES <<< "${SIZE_LIST}"
IFS=',' read -r -a SUB_STAGE_VALUES <<< "${SUB_STAGE_LIST}"

cat >"${CSV_PATH}" <<'EOF'
main_stage,main_stage_name,sub_stage,sub_stage_name,threads,size,duration,sample_interval,filter_size,baseline_ops,with_hook_ops,overhead_pct
EOF

for threads in "${THREAD_VALUES[@]}"; do
    for size in "${SIZE_VALUES[@]}"; do
        baseline_output=$(run_workload "${threads}" "${DURATION}" "${size}")
        baseline_line=$(printf '%s\n' "${baseline_output}" | tail -n1)
        baseline_ops=$(extract_metric "${baseline_line}" "throughput_ops" "0")

        printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
            "0" \
            "baseline" \
            "0" \
            "no_hook" \
            "${threads}" \
            "${size}" \
            "${DURATION}" \
            "${SAMPLE_INTERVAL}" \
            "${FILTER_SIZE}" \
            "${baseline_ops}" \
            "${baseline_ops}" \
            "0.0000" \
            >>"${CSV_PATH}"

        stage3_output=$(run_stage3_workload "${threads}" "${DURATION}" "${size}")
        stage3_line=$(printf '%s\n' "${stage3_output}" | tail -n1)
        stage3_ops=$(extract_metric "${stage3_line}" "throughput_ops" "0")
        stage3_overhead=$(compute_overhead_pct "${baseline_ops}" "${stage3_ops}")
        printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
            "3" \
            "mutex" \
            "0" \
            "no_subablation" \
            "${threads}" \
            "${size}" \
            "${DURATION}" \
            "${SAMPLE_INTERVAL}" \
            "${FILTER_SIZE}" \
            "${baseline_ops}" \
            "${stage3_ops}" \
            "${stage3_overhead}" \
            >>"${CSV_PATH}"

        printf '[stage 3 mutex] threads=%s size=%s baseline_ops=%s with_hook_ops=%s overhead_pct=%s\n' \
            "${threads}" \
            "${size}" \
            "${baseline_ops}" \
            "${stage3_ops}" \
            "${stage3_overhead}"

        for sub_stage in "${SUB_STAGE_VALUES[@]}"; do
            name=$(sub_stage_name "${sub_stage}")
            if [[ "${name}" == "unknown" ]]; then
                echo "invalid tracking sub-ablation stage: ${sub_stage}" >&2
                exit 1
            fi

            hook_output=$(run_tracking_substage_workload "${sub_stage}" "${threads}" "${DURATION}" "${size}")
            hook_line=$(printf '%s\n' "${hook_output}" | tail -n1)
            with_hook_ops=$(extract_metric "${hook_line}" "throughput_ops" "0")
            overhead_pct=$(compute_overhead_pct "${baseline_ops}" "${with_hook_ops}")

            printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
                "4" \
                "tracking" \
                "${sub_stage}" \
                "${name}" \
                "${threads}" \
                "${size}" \
                "${DURATION}" \
                "${SAMPLE_INTERVAL}" \
                "${FILTER_SIZE}" \
                "${baseline_ops}" \
                "${with_hook_ops}" \
                "${overhead_pct}" \
                >>"${CSV_PATH}"

            printf '[stage 4 %s] threads=%s size=%s baseline_ops=%s with_hook_ops=%s overhead_pct=%s\n' \
                "${name}" \
                "${threads}" \
                "${size}" \
                "${baseline_ops}" \
                "${with_hook_ops}" \
                "${overhead_pct}"
        done
    done
done

echo "csv written to ${CSV_PATH}"
