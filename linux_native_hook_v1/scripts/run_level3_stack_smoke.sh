#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${LNHV1_SMOKE_BUILD_DIR:-/tmp/lnhv1_level3_smoke}
SOCKET_PATH=${LNHV1_SMOKE_SOCKET:-/tmp/lnhv1_level3_smoke.sock}

mkdir -p "${BUILD_DIR}"
rm -f "${SOCKET_PATH}" "${BUILD_DIR}/consumer.log" "${BUILD_DIR}/program.log" "${BUILD_DIR}/verbose.log"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I "${ROOT_DIR}" \
  "${ROOT_DIR}/consumer/server_main.cpp" \
  "${ROOT_DIR}/consumer/control_server.cpp" \
  "${ROOT_DIR}/consumer/shm_consumer.cpp" \
  "${ROOT_DIR}/consumer/metrics.cpp" \
  "${ROOT_DIR}/producer_hook/ablation_config.cpp" \
  "${ROOT_DIR}/common/socket_fd.cpp" \
  "${ROOT_DIR}/common/stack_writer.cpp" \
  -pthread -lrt -o "${BUILD_DIR}/consumer_level3"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I "${ROOT_DIR}" -fPIC -shared \
  "${ROOT_DIR}/producer_hook/ablation_config.cpp" \
  "${ROOT_DIR}/producer_hook/hook_preload.cpp" \
  "${ROOT_DIR}/producer_hook/hook_guard.cpp" \
  "${ROOT_DIR}/producer_hook/hotpath_profile.cpp" \
  "${ROOT_DIR}/producer_hook/hook_writer.cpp" \
  "${ROOT_DIR}/producer_hook/stack_capture.cpp" \
  "${ROOT_DIR}/common/socket_fd.cpp" \
  "${ROOT_DIR}/common/stack_writer.cpp" \
  -ldl -pthread -lrt -o "${BUILD_DIR}/hook_preload_level3.so"

g++ -std=c++17 -O0 -g "${ROOT_DIR}/tests/level3_stack_smoke.cpp" -o "${BUILD_DIR}/level3_stack_smoke"

"${BUILD_DIR}/consumer_level3" \
  --socket "${SOCKET_PATH}" \
  --capacity 4096 \
  --flush-threshold 1 \
  --verbose > "${BUILD_DIR}/consumer.log" 2>&1 &
consumer_pid=$!

cleanup() {
  kill -TERM "${consumer_pid}" 2>/dev/null || true
  sleep 0.1
  kill -KILL "${consumer_pid}" 2>/dev/null || true
  wait "${consumer_pid}" 2>/dev/null || true
}
trap cleanup EXIT

for _ in {1..50}; do
  if grep -q "consumer listening" "${BUILD_DIR}/consumer.log"; then
    break
  fi
  sleep 0.1
done

LNHV1_SOCKET_PATH="${SOCKET_PATH}" \
LNHV1_STACK_CAPTURE=1 \
LNHV1_MAX_STACK_DEPTH=8 \
LNHV1_ABLATION_STAGE=6 \
LNHV1_TRACKING_MODE=global \
LD_PRELOAD="${BUILD_DIR}/hook_preload_level3.so" \
  "${BUILD_DIR}/level3_stack_smoke" > "${BUILD_DIR}/program.log" 2>&1

sleep 0.5
cleanup
trap - EXIT

grep -E "STACKMAP|VERBOSE" "${BUILD_DIR}/consumer.log" > "${BUILD_DIR}/verbose.log"

if ! grep -q "^STACKMAP," "${BUILD_DIR}/verbose.log"; then
  echo "missing STACKMAP output" >&2
  cat "${BUILD_DIR}/consumer.log" >&2
  exit 1
fi

if ! grep -Eq "^VERBOSE,0,[^,]+,[^,]+,[^,]+,[^,]+,[^,]+,[^,]+,[1-9][0-9]*," "${BUILD_DIR}/verbose.log"; then
  echo "missing malloc VERBOSE line with nonzero stack_id" >&2
  cat "${BUILD_DIR}/consumer.log" >&2
  exit 1
fi

python3 "${ROOT_DIR}/tools/nativehook_like_analyzer.py" "${BUILD_DIR}/verbose.log" > "${BUILD_DIR}/report.md"

grep -E "STACKMAP|VERBOSE,0,|VERBOSE,1," "${BUILD_DIR}/verbose.log" | head -20
grep -E "Stack Maps|stack:" "${BUILD_DIR}/report.md"
