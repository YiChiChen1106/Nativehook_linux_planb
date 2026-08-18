#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

PROFILE="all"
TOOL="${TOOL:-all}"
REPEATS="${REPEATS:-3}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
EXPERIMENT_NAME="${EXPERIMENT_NAME:-game_like_blind_${TIMESTAMP}}"
EXPERIMENT_DIR="${EXPERIMENT_DIR:-${ROOT_DIR}/results/game_like_blind/${EXPERIMENT_NAME}}"
BENCHMARK_SRC="${ROOT_DIR}/benchmarks/leak_benchmark.cpp"
BENCHMARK_BIN="${BUILD_DIR}/leak_benchmark"
DRY_RUN=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [--profile all|scene_scale|frame_hotpath|async_mixed|business_paths|business_edges] [--dry-run]

Environment:
  EXPERIMENT_NAME default: game_like_blind_<timestamp>
  EXPERIMENT_DIR  default: ${ROOT_DIR}/results/game_like_blind/<experiment_name>
  TOOL            default: all
  REPEATS         default: 3
  BUILD_DIR       default: ${ROOT_DIR}/build
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile)
      PROFILE="${2:-}"
      shift 2
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if ! [[ "${REPEATS}" =~ ^[0-9]+$ ]] || [[ "${REPEATS}" -le 0 ]]; then
  echo "REPEATS must be a positive integer" >&2
  exit 2
fi

case "${PROFILE}" in
  all|scene_scale|frame_hotpath|async_mixed|business_paths|business_edges)
    ;;
  *)
    echo "invalid --profile: ${PROFILE}" >&2
    exit 2
    ;;
esac

configure_profile() {
  local profile=$1
  case "${profile}" in
    scene_scale)
      PROFILE_CASES="game_scene_no_leak,game_scene_leak,game_scene_growth"
      PROFILE_THREADS=1
      PROFILE_ITERATIONS=256
      PROFILE_SIZE=4096
      PROFILE_ROUNDS=4
      PROFILE_DELAY_MS=50
      PROFILE_POST_DELAY_MS=1000
      PROFILE_PURPOSE="scene asset loading, unloading, and repeated scene transitions"
      ;;
    frame_hotpath)
      PROFILE_CASES="game_frame_no_leak,game_mixed"
      PROFILE_THREADS=4
      PROFILE_ITERATIONS=5000
      PROFILE_SIZE=64
      PROFILE_ROUNDS=4
      PROFILE_DELAY_MS=0
      PROFILE_POST_DELAY_MS=1500
      PROFILE_PURPOSE="per-frame temporary objects and mixed UI/frame churn"
      ;;
    async_mixed)
      PROFILE_CASES="game_async_delayed_free,game_mixed"
      PROFILE_THREADS=2
      PROFILE_ITERATIONS=512
      PROFILE_SIZE=1024
      PROFILE_ROUNDS=4
      PROFILE_DELAY_MS=100
      PROFILE_POST_DELAY_MS=2500
      PROFILE_PURPOSE="async resource buffers, delayed reclamation, and mixed paths"
      ;;
    business_paths)
      PROFILE_CASES="game_network_packet,game_audio_decode,game_cache_eviction"
      PROFILE_THREADS=2
      PROFILE_ITERATIONS=1200
      PROFILE_SIZE=1024
      PROFILE_ROUNDS=4
      PROFILE_DELAY_MS=20
      PROFILE_POST_DELAY_MS=1500
      PROFILE_PURPOSE="network packet processing, audio decode buffers, and cache eviction"
      ;;
    business_edges)
      PROFILE_CASES="game_object_pool_no_leak,game_object_pool_leak,game_mmap_no_leak,game_mmap_leak,game_ui_callback_no_leak,game_ui_callback_leak,game_long_running_no_leak,game_long_running_leak"
      PROFILE_THREADS=2
      PROFILE_ITERATIONS=128
      PROFILE_SIZE=4096
      PROFILE_ROUNDS=6
      PROFILE_DELAY_MS=250
      PROFILE_POST_DELAY_MS=2000
      PROFILE_PURPOSE="object pool ownership, mmap resource mappings, UI callback cycles, and long-running growth"
      ;;
    *)
      echo "unknown profile: ${profile}" >&2
      return 2
      ;;
  esac
}

ensure_benchmark() {
  if [[ -x "${BENCHMARK_BIN}" && "${BENCHMARK_BIN}" -nt "${BENCHMARK_SRC}" ]]; then
    return
  fi
  mkdir -p "${BUILD_DIR}"
  if command -v cmake >/dev/null 2>&1; then
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
    cmake --build "${BUILD_DIR}" --target leak_benchmark -j
  else
    "${CXX:-g++}" -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -pthread \
      "${BENCHMARK_SRC}" -o "${BENCHMARK_BIN}"
  fi
}

write_truth_manifest() {
  local profile_dir=$1
  local truth_dir="${profile_dir}/truth"
  mkdir -p "${truth_dir}"
  IFS=',' read -r -a truth_cases <<< "${PROFILE_CASES}"
  for case_name in "${truth_cases[@]}"; do
    "${BENCHMARK_BIN}" \
      --case "${case_name}" \
      --threads "${PROFILE_THREADS}" \
      --iterations "${PROFILE_ITERATIONS}" \
      --size "${PROFILE_SIZE}" \
      --rounds "${PROFILE_ROUNDS}" \
      --delay-ms "${PROFILE_DELAY_MS}" \
      --post-delay-ms 0 \
      --json > "${truth_dir}/${case_name}.json"
  done
  python3 - "${truth_dir}" "${profile_dir}/truth_manifest.csv" <<'PY'
import csv
import json
import sys
from pathlib import Path

truth_dir = Path(sys.argv[1])
output_path = Path(sys.argv[2])
fields = [
    "case", "suite_group", "purpose", "intentional_leak", "threads",
    "iterations_per_thread", "allocation_size", "rounds", "delay_ms",
    "expected_outstanding_blocks", "expected_outstanding_bytes",
]
rows = []
for path in sorted(truth_dir.glob("*.json")):
    data = json.loads(path.read_text(encoding="utf-8"))
    rows.append({field: data.get(field, "") for field in fields})
output_path.parent.mkdir(parents=True, exist_ok=True)
with output_path.open("w", encoding="utf-8", newline="") as handle:
    writer = csv.DictWriter(handle, fieldnames=fields)
    writer.writeheader()
    writer.writerows(rows)
PY
}

write_config() {
  local profile_dir=$1
  local profile=$2
  cat > "${profile_dir}/experiment_config.txt" <<EOF
experiment_name=${EXPERIMENT_NAME}
profile=${profile}
purpose=${PROFILE_PURPOSE}
experiment_dir=${profile_dir}
truth_manifest=${profile_dir}/truth_manifest.csv
repeats=${REPEATS}
tool=${TOOL}
build_dir=${BUILD_DIR}
threads=${PROFILE_THREADS}
iterations=${PROFILE_ITERATIONS}
size=${PROFILE_SIZE}
rounds=${PROFILE_ROUNDS}
delay_ms=${PROFILE_DELAY_MS}
post_delay_ms=${PROFILE_POST_DELAY_MS}
cases=${PROFILE_CASES}
blind_validation=true
truth_frozen_before_tool_run=true
EOF
}

init_index() {
  local profile_dir=$1
  printf 'repeat,out_dir,status,duration_seconds,summary_csv,summary_md\n' > "${profile_dir}/experiment_index.csv"
  {
    echo "# Game-like Blind Experiment"
    echo
    echo "profile=${CURRENT_PROFILE}"
    echo "purpose=${PROFILE_PURPOSE}"
    echo
    echo "| repeat | status | duration_seconds | out_dir | summary_csv | summary_md |"
    echo "| --- | --- | --- | --- | --- | --- |"
  } > "${profile_dir}/experiment_index.md"
}

append_index() {
  local profile_dir=$1
  local repeat=$2
  local out_dir=$3
  local status=$4
  local duration_seconds=$5
  printf '%s,%s,%s,%s,%s,%s\n' \
    "${repeat}" "${out_dir}" "${status}" "${duration_seconds}" \
    "${out_dir}/tool_compare_summary.csv" "${out_dir}/tool_compare_summary.md" \
    >> "${profile_dir}/experiment_index.csv"
  printf '| %s | %s | %s | %s | %s | %s |\n' \
    "${repeat}" "${status}" "${duration_seconds}" "${out_dir}" \
    "${out_dir}/tool_compare_summary.csv" "${out_dir}/tool_compare_summary.md" \
    >> "${profile_dir}/experiment_index.md"
}

run_profile() {
  local profile=$1
  CURRENT_PROFILE="${profile}"
  configure_profile "${profile}"

  local profile_dir="${EXPERIMENT_DIR}/${profile}"
  mkdir -p "${profile_dir}"
  write_config "${profile_dir}" "${profile}"
  init_index "${profile_dir}"
  ensure_benchmark
  write_truth_manifest "${profile_dir}"
  echo "profile=${profile} cases=${PROFILE_CASES} threads=${PROFILE_THREADS} iterations=${PROFILE_ITERATIONS} size=${PROFILE_SIZE}"

  local failed=0
  for repeat in $(seq 1 "${REPEATS}"); do
    local repeat_dir="${profile_dir}/repeat_${repeat}"
    mkdir -p "${repeat_dir}"
    local start_epoch
    local end_epoch
    local rc=0
    start_epoch=$(date +%s)

    set +e
    compare_args=(--tool "${TOOL}" --cases "${PROFILE_CASES}")
    if [[ "${DRY_RUN}" == "1" ]]; then
      compare_args+=(--dry-run)
    fi
    OUT_DIR="${repeat_dir}" \
    BUILD_DIR="${BUILD_DIR}" \
    THREADS="${PROFILE_THREADS}" \
    ITERATIONS="${PROFILE_ITERATIONS}" \
    SIZE="${PROFILE_SIZE}" \
    ROUNDS="${PROFILE_ROUNDS}" \
    DELAY_MS="${PROFILE_DELAY_MS}" \
    POST_DELAY_MS="${PROFILE_POST_DELAY_MS}" \
    BCC_START_DELAY_MS=2000 \
    BCC_INTERVAL=2 \
    BCC_COUNT=3 \
    BCC_POST_DELAY_MS=$((PROFILE_POST_DELAY_MS + 7000)) \
      "${SCRIPT_DIR}/run_leak_tool_compare.sh" "${compare_args[@]}" \
      > "${repeat_dir}/runner.stdout.log" \
      2> "${repeat_dir}/runner.stderr.log"
    rc=$?
    if [[ "${rc}" -eq 0 ]]; then
      python3 "${ROOT_DIR}/tools/summarize_tool_compare.py" "${repeat_dir}" \
        > "${repeat_dir}/summarizer.stdout.log" \
        2> "${repeat_dir}/summarizer.stderr.log"
      rc=$?
    fi
    set -e

    end_epoch=$(date +%s)
    if [[ "${rc}" -eq 0 ]]; then
      append_index "${profile_dir}" "${repeat}" "${repeat_dir}" "ok" "$((end_epoch - start_epoch))"
    else
      append_index "${profile_dir}" "${repeat}" "${repeat_dir}" "failed:${rc}" "$((end_epoch - start_epoch))"
      failed=1
    fi
  done

  python3 "${ROOT_DIR}/tools/summarize_formal_experiment.py" "${profile_dir}" \
    --csv "${profile_dir}/formal_experiment_summary.csv" \
    --markdown "${profile_dir}/formal_experiment_summary.md" \
    > "${profile_dir}/formal_summarizer.stdout.log" \
    2> "${profile_dir}/formal_summarizer.stderr.log" || failed=1

  echo "profile_summary=${profile_dir}/formal_experiment_summary.csv"
  return "${failed}"
}

mkdir -p "${EXPERIMENT_DIR}"
if [[ "${PROFILE}" == "all" ]]; then
  failed=0
  for profile in scene_scale frame_hotpath async_mixed business_paths business_edges; do
    if ! run_profile "${profile}"; then
      failed=1
    fi
  done
  exit "${failed}"
fi

run_profile "${PROFILE}"
