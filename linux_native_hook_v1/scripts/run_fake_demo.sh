#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
SOCKET_PATH=/tmp/linux_native_hook_v1.sock

"${ROOT_DIR}/build/consumer" --socket "${SOCKET_PATH}" &
CONSUMER_PID=$!
trap 'kill ${CONSUMER_PID} >/dev/null 2>&1 || true' EXIT

sleep 1
"${ROOT_DIR}/build/producer_fake" --socket "${SOCKET_PATH}" --bursts 10 --burst-size 32 --sleep-ms 50
