#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CONSUMER_BIN="${ROOT_DIR}/build/consumer"
HOOK_LIB="${ROOT_DIR}/build/hook_preload.so"
WORKLOAD="${ROOT_DIR}/build/perf_test_data_linux"

THREADS_LIST=${THREADS_LIST:-1,4,8,16}
SUB_STAGE_LIST=${SUB_STAGE_LIST:-34,35}
TOTAL_OPS=${TOTAL_OPS:-1000000}
PATTERN=${PATTERN:-mixed3}

IFS=',' read -r -a THREAD_VALUES <<< "${THREADS_LIST}"
IFS=',' read -r -a SUB_STAGE_VALUES <<< "${SUB_STAGE_LIST}"

run_one() {
    local sub=$1 threads=$2 iters=$3
    local sock="/tmp/nh_sw_${sub}_${threads}.sock"
    local shm="/nh_sw_${sub}_${threads}"

    "${CONSUMER_BIN}" --socket "${sock}" --shm "${shm}" --capacity 4096 --flush-threshold 20 > /dev/null 2>&1 &
    local cpid=$!

    for i in $(seq 1 30); do
        [[ -S "${sock}" ]] && break
        sleep 0.1
    done

    local out
    out=$(LNHV1_ABLATION_STAGE=6 \
        LNHV1_SUBABLATION_STAGE="${sub}" \
        LNHV1_TRACKING_MODE=thread_local_fallback \
        LNHV1_PID_TID_CACHE=1 \
        LNHV1_SOCKET_PATH="${sock}" \
        LD_PRELOAD="${HOOK_LIB}" \
        "${WORKLOAD}" --threads "${threads}" --iters-per-thread "${iters}" --size 32 --pattern "${PATTERN}" 2>&1)
    local rc=$?

    kill "${cpid}" 2>/dev/null
    wait "${cpid}" 2>/dev/null || true
    rm -f "${sock}" "/dev/shm${shm}" 2>/dev/null

    local elapsed
    elapsed=$(echo "${out}" | grep -oP 'elapsed_seconds=\K[0-9.]+')
    echo "sub=${sub} threads=${threads} iters_per_thread=${iters} elapsed_seconds=${elapsed} exit_code=${rc}"
}

echo "# stack_writer ablation benchmark $(date +%F_%T)"
echo "# total_ops=${TOTAL_OPS} pattern=${PATTERN}"
echo ""

for sub in "${SUB_STAGE_VALUES[@]}"; do
    for threads in "${THREAD_VALUES[@]}"; do
        iters=$(( TOTAL_OPS / threads ))
        run_one "${sub}" "${threads}" "${iters}"
    done
done

echo ""
echo "# done"
