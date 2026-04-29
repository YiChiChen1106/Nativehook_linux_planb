#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

SOCKET_PATH=${LNHV1_SOCKET_PATH:-/tmp/linux_native_hook_v1.sock}
SAMPLE_INTERVAL=${LNHV1_SAMPLE_INTERVAL:-1}
FILTER_SIZE=${LNHV1_FILTER_SIZE:--1}
BLOCKED=${LNHV1_BLOCKED:-0}
HOOK_LIB="${ROOT_DIR}/build/hook_preload.so"
WORKLOAD="${ROOT_DIR}/build/perf_test_data_linux"

echo "[1/3] start consumer"
echo "consumer config: sample_interval=${SAMPLE_INTERVAL} filter_size=${FILTER_SIZE} blocked=${BLOCKED}"
CONSUMER_ARGS=(
  --socket "${SOCKET_PATH}"
  --sample-interval "${SAMPLE_INTERVAL}"
  --filter-size "${FILTER_SIZE}"
)
if [[ "${BLOCKED}" != "0" ]]; then
  CONSUMER_ARGS+=(--blocked)
fi
"${ROOT_DIR}/build/consumer" "${CONSUMER_ARGS[@]}" &
CONSUMER_PID=$!
trap 'kill ${CONSUMER_PID} >/dev/null 2>&1 || true' EXIT

sleep 1

echo "[2/3] baseline"
"${WORKLOAD}" --threads 4 --duration 5 --size 32

echo "[3/3] with hook"
LNHV1_SOCKET_PATH="${SOCKET_PATH}" LD_PRELOAD="${HOOK_LIB}" "${WORKLOAD}" --threads 4 --duration 5 --size 32
