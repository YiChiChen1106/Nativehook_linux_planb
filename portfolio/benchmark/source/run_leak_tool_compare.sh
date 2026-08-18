#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUT_DIR="${OUT_DIR:-${ROOT_DIR}/results/tool_compare/${TIMESTAMP}}"

THREADS="${THREADS:-1}"
ITERATIONS="${ITERATIONS:-1000}"
SIZE="${SIZE:-64}"
ROUNDS="${ROUNDS:-4}"
DELAY_MS="${DELAY_MS:-100}"
POST_DELAY_MS="${POST_DELAY_MS:-1000}"
BCC_START_DELAY_MS="${BCC_START_DELAY_MS:-2000}"
BCC_INTERVAL="${BCC_INTERVAL:-2}"
BCC_COUNT="${BCC_COUNT:-3}"
BCC_POST_DELAY_MS="${BCC_POST_DELAY_MS:-$((POST_DELAY_MS + BCC_INTERVAL * BCC_COUNT * 1000 + 500))}"
BCC_OLDER_MS="${BCC_OLDER_MS:-0}"

TOOL="all"
CASE_FILTER="all"
CASE_LIST=""
DRY_RUN=0

TOOLS=(valgrind lsan heaptrack bcc_memleak)
CASES=(
  no_leak definite_leak growth_leak delayed_free high_freq_no_leak mixed
  game_scene_no_leak game_scene_leak game_scene_growth
  game_async_delayed_free game_frame_no_leak game_mixed
  game_network_packet game_audio_decode game_cache_eviction
  game_object_pool_no_leak game_object_pool_leak
  game_mmap_no_leak game_mmap_leak
  game_ui_callback_no_leak game_ui_callback_leak
  game_long_running_no_leak game_long_running_leak
)

usage() {
  cat <<EOF
Usage: $(basename "$0") [--tool all|valgrind|lsan|heaptrack|bcc_memleak] [--case case|all] [--cases case1,case2,...] [--dry-run]

Environment:
  BUILD_DIR       default: ${ROOT_DIR}/build
  OUT_DIR         default: ${ROOT_DIR}/results/tool_compare/<timestamp>
  THREADS         default: 1
  ITERATIONS      default: 1000
  SIZE            default: 64
  ROUNDS          default: 4
  DELAY_MS        default: 100
  POST_DELAY_MS   default: 1000
  BCC_START_DELAY_MS default: 1000
  BCC_INTERVAL    default: 2
  BCC_COUNT       default: 3
  BCC_POST_DELAY_MS default: ${BCC_POST_DELAY_MS}
  BCC_OLDER_MS    default: 0
EOF
}

contains() {
  local needle=$1
  shift
  local item
  for item in "$@"; do
    if [[ "${item}" == "${needle}" ]]; then
      return 0
    fi
  done
  return 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tool)
      TOOL="${2:-}"
      shift 2
      ;;
    --case)
      CASE_FILTER="${2:-}"
      shift 2
      ;;
    --cases)
      CASE_LIST="${2:-}"
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

if [[ "${TOOL}" != "all" ]] && ! contains "${TOOL}" "${TOOLS[@]}"; then
  echo "invalid --tool: ${TOOL}" >&2
  exit 2
fi

if [[ -n "${CASE_LIST}" && "${CASE_FILTER}" != "all" ]]; then
  echo "use only one of --case and --cases" >&2
  exit 2
fi

if [[ "${CASE_FILTER}" != "all" ]] && ! contains "${CASE_FILTER}" "${CASES[@]}"; then
  echo "invalid --case: ${CASE_FILTER}" >&2
  exit 2
fi

if [[ -n "${CASE_LIST}" ]]; then
  IFS=',' read -r -a requested_cases <<< "${CASE_LIST}"
  if [[ "${#requested_cases[@]}" -eq 0 ]]; then
    echo "--cases must contain at least one case" >&2
    exit 2
  fi
  for requested_case in "${requested_cases[@]}"; do
    if ! contains "${requested_case}" "${CASES[@]}"; then
      echo "invalid --cases entry: ${requested_case}" >&2
      exit 2
    fi
  done
fi

selected_tools() {
  if [[ "${TOOL}" == "all" ]]; then
    printf '%s\n' "${TOOLS[@]}"
  else
    printf '%s\n' "${TOOL}"
  fi
}

selected_cases() {
  if [[ -n "${CASE_LIST}" ]]; then
    printf '%s\n' "${requested_cases[@]}"
    return
  fi
  if [[ "${CASE_FILTER}" == "all" ]]; then
    printf '%s\n' "${CASES[@]}"
  else
    printf '%s\n' "${CASE_FILTER}"
  fi
}

tool_version_for() {
  local tool_name=$1
  case "${tool_name}" in
    valgrind)
      valgrind --version 2>/dev/null || echo unavailable
      ;;
    lsan)
      "${CLANGXX:-clang++}" --version 2>/dev/null | head -n 1 || echo unavailable
      ;;
    heaptrack)
      heaptrack --version 2>/dev/null | head -n 1 || echo unavailable
      ;;
    bcc_memleak)
      rpm -q bcc-tools 2>/dev/null || echo bcc-tools-unknown
      ;;
    *)
      echo unknown
      ;;
  esac
}

requires_root_for() {
  [[ "$1" == "bcc_memleak" ]] && echo yes || echo no
}

build_required_for() {
  [[ "$1" == "lsan" ]] && echo yes || echo no
}

ensure_benchmark() {
  local benchmark_bin="${BUILD_DIR}/leak_benchmark"
  local benchmark_src="${ROOT_DIR}/benchmarks/leak_benchmark.cpp"
  if [[ -x "${benchmark_bin}" && "${benchmark_bin}" -nt "${benchmark_src}" ]]; then
    return
  fi
  mkdir -p "${BUILD_DIR}"
  if command -v cmake >/dev/null 2>&1; then
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
    cmake --build "${BUILD_DIR}" --target leak_benchmark -j
  else
    "${CXX:-g++}" -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -pthread \
      "${benchmark_src}" -o "${benchmark_bin}"
  fi
}

ensure_lsan_benchmark() {
  local lsan_bin="${BUILD_DIR}/leak_benchmark_lsan"
  local benchmark_src="${ROOT_DIR}/benchmarks/leak_benchmark.cpp"
  if [[ -x "${lsan_bin}" && "${lsan_bin}" -nt "${benchmark_src}" ]]; then
    return
  fi
  mkdir -p "${BUILD_DIR}"
  "${CLANGXX:-clang++}" -std=c++17 -O1 -g -fsanitize=address -fno-omit-frame-pointer \
    "${benchmark_src}" -pthread -o "${lsan_bin}"
}

write_run_config() {
  local output_dir=$1
  local tool_name=$2
  local case_name=$3
  local command_text=$4
  cat > "${output_dir}/run_config.txt" <<EOF
tool=${tool_name}
case=${case_name}
threads=${THREADS}
iterations=${ITERATIONS}
size=${SIZE}
rounds=${ROUNDS}
delay_ms=${DELAY_MS}
post_delay_ms=${POST_DELAY_MS}
bcc_start_delay_ms=${BCC_START_DELAY_MS}
bcc_interval=${BCC_INTERVAL}
bcc_count=${BCC_COUNT}
bcc_post_delay_ms=${BCC_POST_DELAY_MS}
bcc_older_ms=${BCC_OLDER_MS}
tool_version=$(tool_version_for "${tool_name}")
requires_root=$(requires_root_for "${tool_name}")
build_required=$(build_required_for "${tool_name}")
command=${command_text}
EOF
}

run_valgrind_case() {
  local case_name=$1
  local output_dir=$2
  local benchmark_bin="${BUILD_DIR}/leak_benchmark"
  ensure_benchmark
  write_run_config "${output_dir}" valgrind "${case_name}" "valgrind --leak-check=full --show-leak-kinds=all ${benchmark_bin}"
  valgrind --leak-check=full --show-leak-kinds=all --num-callers=24 \
    --log-file="${output_dir}/valgrind.log" \
    "${benchmark_bin}" \
      --case "${case_name}" \
      --threads "${THREADS}" \
      --iterations "${ITERATIONS}" \
      --size "${SIZE}" \
      --rounds "${ROUNDS}" \
      --delay-ms "${DELAY_MS}" \
      --post-delay-ms "${POST_DELAY_MS}" \
      --json > "${output_dir}/benchmark.json" 2> "${output_dir}/stderr.log"
}

run_lsan_case() {
  local case_name=$1
  local output_dir=$2
  local lsan_bin="${BUILD_DIR}/leak_benchmark_lsan"
  ensure_lsan_benchmark
  write_run_config "${output_dir}" lsan "${case_name}" "ASAN_OPTIONS=detect_leaks=1 ${lsan_bin}"
  set +e
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:exitcode=0 \
    "${lsan_bin}" \
      --case "${case_name}" \
      --threads "${THREADS}" \
      --iterations "${ITERATIONS}" \
      --size "${SIZE}" \
      --rounds "${ROUNDS}" \
      --delay-ms "${DELAY_MS}" \
      --post-delay-ms "${POST_DELAY_MS}" \
      --json > "${output_dir}/benchmark.json" 2> "${output_dir}/lsan.log"
  local rc=$?
  set -e
  echo "${rc}" > "${output_dir}/tool_exit_code.txt"
}

run_heaptrack_case() {
  local case_name=$1
  local output_dir=$2
  local benchmark_bin="${BUILD_DIR}/leak_benchmark"
  ensure_benchmark
  write_run_config "${output_dir}" heaptrack "${case_name}" "heaptrack ${benchmark_bin}"
  heaptrack -o "${output_dir}/heaptrack" \
    "${benchmark_bin}" \
      --case "${case_name}" \
      --threads "${THREADS}" \
      --iterations "${ITERATIONS}" \
      --size "${SIZE}" \
      --rounds "${ROUNDS}" \
      --delay-ms "${DELAY_MS}" \
      --post-delay-ms "${POST_DELAY_MS}" \
      --json > "${output_dir}/benchmark.json" 2> "${output_dir}/heaptrack.log"
  analyze_heaptrack_profile "${output_dir}"
}

analyze_heaptrack_profile() {
  local output_dir=$1
  local profile_file
  profile_file=$(find "${output_dir}" -maxdepth 1 -type f \( -name 'heaptrack*.zst' -o -name 'heaptrack*.gz' \) | sort | head -n 1)
  if [[ -z "${profile_file}" ]]; then
    echo "missing heaptrack profile" > "${output_dir}/heaptrack_print.stderr.log"
    echo "1" > "${output_dir}/heaptrack_print_exit_code.txt"
    return 0
  fi
  if ! command -v heaptrack_print >/dev/null 2>&1; then
    echo "heaptrack_print not found" > "${output_dir}/heaptrack_print.stderr.log"
    echo "127" > "${output_dir}/heaptrack_print_exit_code.txt"
    return 0
  fi

  set +e
  heaptrack_print \
    --print-leaks true \
    --print-allocators false \
    --print-peaks false \
    --print-temporary false \
    "${profile_file}" > "${output_dir}/heaptrack_print.log" 2> "${output_dir}/heaptrack_print.stderr.log"
  local rc=$?
  set -e
  echo "${rc}" > "${output_dir}/heaptrack_print_exit_code.txt"
}

run_bcc_memleak_case() {
  local case_name=$1
  local output_dir=$2
  local benchmark_bin="${BUILD_DIR}/leak_benchmark"
  ensure_benchmark
  write_run_config "${output_dir}" bcc_memleak "${case_name}" "sudo /usr/share/bcc/tools/memleak -p <benchmark_pid>"
  "${benchmark_bin}" \
    --case "${case_name}" \
    --threads "${THREADS}" \
    --iterations "${ITERATIONS}" \
    --size "${SIZE}" \
    --rounds "${ROUNDS}" \
    --start-delay-ms "${BCC_START_DELAY_MS}" \
    --delay-ms "${DELAY_MS}" \
    --post-delay-ms "${BCC_POST_DELAY_MS}" \
    --json > "${output_dir}/benchmark.json" 2> "${output_dir}/benchmark.stderr.log" &
  local benchmark_pid=$!
  echo "${benchmark_pid}" > "${output_dir}/benchmark.pid"
  sleep 0.2
  set +e
  sudo -n /usr/share/bcc/tools/memleak \
    -a \
    -o "${BCC_OLDER_MS}" \
    --combined-only \
    -p "${benchmark_pid}" \
    "${BCC_INTERVAL}" "${BCC_COUNT}" > "${output_dir}/memleak.log" 2> "${output_dir}/stderr.log"
  local memleak_rc=$?
  wait "${benchmark_pid}" 2>/dev/null
  local benchmark_rc=$?
  set -e
  echo "${memleak_rc}" > "${output_dir}/memleak_exit_code.txt"
  echo "${benchmark_rc}" > "${output_dir}/benchmark_exit_code.txt"
  if [[ "${benchmark_rc}" -ne 0 ]]; then
    return "${benchmark_rc}"
  fi
  return 0
}


dry_run_case() {
  local tool_name=$1
  local case_name=$2
  local output_dir=$3
  echo "DRY_RUN tool=${tool_name} case=${case_name} out_dir=${OUT_DIR} case_dir=${output_dir}"
}

run_case() {
  local tool_name=$1
  local case_name=$2
  local output_dir="${OUT_DIR}/${tool_name}/${case_name}"
  mkdir -p "${output_dir}"

  if [[ "${DRY_RUN}" == "1" ]]; then
    dry_run_case "${tool_name}" "${case_name}" "${output_dir}"
    return
  fi

  echo "running tool=${tool_name} case=${case_name} out=${output_dir}"
  local start_ns
  local end_ns
  local rc=0
  local duration_ms
  local output_bytes
  start_ns=$(date +%s%N)
  set +e
  case "${tool_name}" in
    valgrind)
      run_valgrind_case "${case_name}" "${output_dir}"
      ;;
    lsan)
      run_lsan_case "${case_name}" "${output_dir}"
      ;;
    heaptrack)
      run_heaptrack_case "${case_name}" "${output_dir}"
      ;;
    bcc_memleak)
      run_bcc_memleak_case "${case_name}" "${output_dir}"
      ;;
    *)
      echo "unknown tool: ${tool_name}" >&2
      exit 2
      ;;
  esac
  rc=$?
  set -e
  end_ns=$(date +%s%N)
  duration_ms=$(( (end_ns - start_ns) / 1000000 ))
  output_bytes=$(find "${output_dir}" -type f -printf '%s\n' | awk '{sum += $1} END {print sum + 0}')
  printf 'runner_duration_ms=%s\noutput_bytes=%s\nrunner_exit_code=%s\n' \
    "${duration_ms}" "${output_bytes}" "${rc}" >> "${output_dir}/run_config.txt"
  echo "${rc}" > "${output_dir}/runner_exit_code.txt"
  if [[ "${rc}" -ne 0 ]]; then
    return "${rc}"
  fi
}

mkdir -p "${OUT_DIR}"
echo "output_dir=${OUT_DIR}"

while IFS= read -r tool_name; do
  while IFS= read -r case_name; do
    run_case "${tool_name}" "${case_name}"
  done < <(selected_cases)
done < <(selected_tools)

echo "done"
