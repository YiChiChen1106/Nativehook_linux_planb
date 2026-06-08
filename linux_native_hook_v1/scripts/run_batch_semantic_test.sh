#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${BUILD_DIR:-/tmp/nh-build}"
RESULTS_DIR="${RESULTS_DIR:-$PROJECT_DIR/results}"
TIMESTAMP="$(date +%Y-%m-%d)"

CONSUMER="${BUILD_DIR}/consumer"
TEST_BIN="${BUILD_DIR}/batch_semantic_test"
HOOK_PRELOAD="${BUILD_DIR}/hook_preload.so"

mkdir -p "$RESULTS_DIR"

run_test() {
    local batch_size="$1"
    local label="$2"
    local socket_path="/tmp/nh_sem_$$_${batch_size}.sock"
    local csv_path="${RESULTS_DIR}/diag_batch_semantic_batch${batch_size}_${TIMESTAMP}.csv"

    rm -f "$socket_path"

    "$CONSUMER" \
        --socket "$socket_path" \
        --capacity 16384 \
        --flush-threshold 20 \
        --sample-interval 1 \
        --filter-size -1 \
        --verbose \
        > /tmp/nh_consumer_$$_${batch_size}.log 2>&1 &
    local consumer_pid=$!
    sleep 0.5

    local env_vars=(
        "LNHV1_SOCKET_PATH=$socket_path"
        "LNHV1_ABLATION_STAGE=6"
        "LNHV1_PID_TID_CACHE=1"
        "LNHV1_TRACKING_MODE=thread_local_fallback"
    )
    if [ "$batch_size" -gt 0 ]; then
        env_vars+=("LNHV1_STAGE6_BATCH_SIZE=$batch_size")
    fi

    env "${env_vars[@]}" LD_PRELOAD="$HOOK_PRELOAD" timeout 10 "$TEST_BIN" \
        --cross 400 --same 200 --multi 100 --size 32 \
        > /dev/null 2>&1
    local test_rc=$?

    sleep 1

    kill "$consumer_pid" 2>/dev/null || true
    wait "$consumer_pid" 2>/dev/null || true
    rm -f "$socket_path"

    grep '^VERBOSE,' /tmp/nh_consumer_$$_${batch_size}.log > "$csv_path" 2>/dev/null || true
    local record_count=$(wc -l < "$csv_path" 2>/dev/null || echo 0)

    local alloc_count=$(grep -c ',0,' "$csv_path" 2>/dev/null || echo 0)
    local free_count=$(grep -c ',1,' "$csv_path" 2>/dev/null || echo 0)
    local name_count=$(grep -c ',8,' "$csv_path" 2>/dev/null || echo 0)
    local end_count=$(grep -c ',12,' "$csv_path" 2>/dev/null || echo 0)

    local unique_tids=$(awk -F',' '{print $2}' "$csv_path" | sort -u | wc -l)

    local free_before_alloc=$(awk -F',' '
    BEGIN { bad = 0 }
    {
        type = $1; addr = $3;
        if (!(addr in first)) {
            first[addr] = type
            if (type == "1") bad++
        }
    }
    END { print bad }
    ' "$csv_path")

    local duplicate_allocs=$(awk -F',' '$1=="0"{print $3}' "$csv_path" | sort | uniq -d | wc -l)
    local duplicate_frees=$(awk -F',' '$1=="1"{print $3}' "$csv_path" | sort | uniq -d | wc -l)

    rm -f /tmp/nh_consumer_$$_${batch_size}.log
}

analyze_csv() {
    local csv="$1"
    local label="$2"

    local total=$(wc -l < "$csv" 2>/dev/null || echo 0)
    local alloc_count=$(grep -c ',0,' "$csv" 2>/dev/null || echo 0)
    local free_count=$(grep -c ',1,' "$csv" 2>/dev/null || echo 0)
    local name_count=$(grep -c ',8,' "$csv" 2>/dev/null || echo 0)
    local end_count=$(grep -c ',12,' "$csv" 2>/dev/null || echo 0)

    local unique_tids=$(awk -F',' '{print $2}' "$csv" | sort -u | wc -l)

    local free_before_alloc=$(awk -F',' '
    BEGIN { bad = 0 }
    {
        type = $1; addr = $3;
        if (!(addr in first)) {
            first[addr] = type
            if (type == "1") bad++
        }
    }
    END { print bad }
    ' "$csv")

    local duplicate_allocs=$(awk -F',' '$1=="0"{print $3}' "$csv" | sort | uniq -d | wc -l)
    local duplicate_frees=$(awk -F',' '$1=="1"{print $3}' "$csv" | sort | uniq -d | wc -l)

    local per_tid_report=$(awk -F',' '
    {
        type = $1; tid = $2; addr = $3;
        if (!(tid in first_seen)) first_seen[tid] = 1
        if (type == "0") allocs[tid]++
        if (type == "1") frees[tid]++
    }
    END {
        for (t in first_seen) {
            printf "  tid %s: alloc=%d free=%d\n", t, allocs[t], frees[t]
        }
    }' "$csv" | sort)

    echo ""
    echo "=== $label ==="
    echo "  records:       $total"
    echo "  alloc:         $alloc_count"
    echo "  free:          $free_count"
    echo "  thread_name:   $name_count"
    echo "  end:           $end_count"
    echo "  unique_tids:   $unique_tids"
    echo "  free_before_alloc_warnings: $free_before_alloc"
    echo "  duplicate_alloc_addrs: $duplicate_allocs"
    echo "  duplicate_free_addrs: $duplicate_frees"
    echo "  per_tid:"
    echo "$per_tid_report" | while IFS= read -r line; do echo "    $line"; done
}

echo "============================================="
echo "Stage 6 Batch Semantic Validation Test"
echo "Date: $TIMESTAMP"
echo "============================================="

echo ""
echo "Running batch=0 (immediate publish)..."
run_test 0 "no_batch"

echo "Running batch=64 (delayed publish)..."
run_test 64 "batch64"

NO_BATCH_CSV="${RESULTS_DIR}/diag_batch_semantic_batch0_${TIMESTAMP}.csv"
BATCH64_CSV="${RESULTS_DIR}/diag_batch_semantic_batch64_${TIMESTAMP}.csv"

analyze_csv "$NO_BATCH_CSV" "No Batch (batch=0)"
analyze_csv "$BATCH64_CSV" "Batch 64"

echo ""
echo "=== Semantic Analysis ==="
echo ""
echo "Cross-thread free ordering check (free-before-alloc addresses):"

for csv in "$NO_BATCH_CSV" "$BATCH64_CSV"; do
    label=$(basename "$csv")
    violations=$(awk -F',' '
    BEGIN { bad = 0 }
    {
        type = $1; addr = $3; tid = $2;
        if (!(addr in alloc_tid) && type == "0") {
            alloc_tid[addr] = tid
        }
        if (type == "1" && !(addr in alloc_tid)) {
            bad++
        }
    }
    END { print bad }
    ' "$csv")
    echo "  $label: free-before-alloc violations = $violations"
done

echo ""
echo "Same-thread ordering check (within each tid, no interleaving violation):"
for csv in "$NO_BATCH_CSV" "$BATCH64_CSV"; do
    label=$(basename "$csv")
    violations=$(awk -F',' '
    {
        type = $1; tid = $2; addr = $3;
        key = tid ":" addr
        if (type == "0" && !(key in seen)) {
            seen[key] = 1
        } else if (type == "0" && ((key in freed))) {
            outstanding[key]++
        } else if (type == "1" && (key in seen)) {
            freed[key] = 1
        }
    }
    END {
        bad = 0
        for (k in freed) {
            split(k, parts, ":")
            tid = parts[1]
            addr = parts[2]
            if (!((tid ":" addr) in seen)) bad++
        }
        print bad
    }
    ' "$csv")
    echo "  $label: per-tid ordering violations = $violations"
done

echo ""
echo "Results saved to $RESULTS_DIR/"
