# Nativehook Plan B Level 3 Unwind Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add opt-in multi-frame stack capture to Plan B so malloc/free records carry a real `stack_id`, consumer verbose logs emit stack maps, and the analyzer groups outstanding allocations by stack.

**Architecture:** Extend `HookRecord` with stack metadata and a small fixed frame array used by `kStackMap` records. The producer captures raw PCs with `backtrace()` only when `LNHV1_STACK_CAPTURE=1`, hashes frames into `stack_id`, emits one stack-map metadata record per process-local stack, and keeps normal alloc/free pairing behavior. The consumer prints backward-compatible extended `VERBOSE` lines plus `STACKMAP` lines; the Python analyzer parses both.

**Tech Stack:** C++17, glibc `execinfo.h` backtrace, POSIX shared memory ring, Python 3 unittest analyzer tests, existing CMake/test scripts.

---

### Task 1: Analyzer Stack Map Parsing

**Files:**
- Modify: `linux_native_hook_v1/tools/nativehook_like_analyzer.py`
- Modify: `linux_native_hook_v1/tests/test_nativehook_like_analyzer.py`

- [ ] **Step 1: Add failing analyzer test for `STACKMAP`**

Add this test method to `NativehookLikeAnalyzerTest`:

```python
    def test_parses_stackmap_and_attaches_frames_to_statistics(self):
        report = analyze_lines([
            "STACKMAP,42,3,0x401000,0x402000,0x403000",
            "VERBOSE,0,101,0x1000,64,7,10,0,42,0x401000",
            "VERBOSE,0,101,0x2000,32,7,11,0,42,0x401000",
            "VERBOSE,1,101,0x1000,0,7,12,0,42,0x401000",
        ])

        self.assertEqual(report["summary"]["stack_map_count"], 1)
        self.assertEqual(report["stack_maps"]["42"], ["0x401000", "0x402000", "0x403000"])
        by_key = {item["group_key"]: item for item in report["statistics"]}
        self.assertEqual(by_key["stack:42"]["frames"], ["0x401000", "0x402000", "0x403000"])
        self.assertEqual(by_key["stack:42"]["outstanding_size"], 32)
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```powershell
python -m unittest linux_native_hook_v1.tests.test_nativehook_like_analyzer.NativehookLikeAnalyzerTest.test_parses_stackmap_and_attaches_frames_to_statistics -v
```

Expected: fail because `STACKMAP` lines are ignored and `stack_map_count` does not exist.

- [ ] **Step 3: Implement `STACKMAP` parsing**

Add a `StackMapLine` parser or simple branch in `analyze_lines`:

```python
def parse_stackmap_line(line: str) -> tuple[int, list[str]] | None:
    text = line.strip()
    if not text or not text.startswith("STACKMAP,"):
        return None
    parts = [part.strip() for part in text.split(",")]
    if len(parts) < 3:
        raise ValueError(f"invalid STACKMAP line with {len(parts)} columns: {line.rstrip()}")
    stack_id = _parse_int(parts[1])
    depth = _parse_int(parts[2])
    frames = parts[3:3 + depth]
    return stack_id, frames
```

In `analyze_lines`, keep `stack_maps: dict[int, list[str]] = {}`. Before parsing `VERBOSE`, parse `STACKMAP` and store frames.

- [ ] **Step 4: Attach frames to report statistics**

After building `stat_rows`, add:

```python
    for item in stat_rows:
        if item["stack_id"] > 0:
            item["frames"] = stack_maps.get(item["stack_id"], [])
        else:
            item["frames"] = []
```

Add `stack_map_count` to summary and `stack_maps` to the returned report, converting keys to strings for JSON stability:

```python
        "stack_map_count": len(stack_maps),
```

```python
        "stack_maps": {str(key): value for key, value in sorted(stack_maps.items())},
```

- [ ] **Step 5: Render stack frames in Markdown**

After the Statistics table, append a `## Stack Maps` section when maps exist:

```python
    if report.get("stack_maps"):
        lines.extend(["", "## Stack Maps", "", "| stack_id | frames |", "|---:|---|"])
        for stack_id, frames in report["stack_maps"].items():
            lines.append(f"| {stack_id} | {' -> '.join(frames)} |")
```

- [ ] **Step 6: Run analyzer tests**

Run:

```powershell
python -m unittest linux_native_hook_v1.tests.test_nativehook_like_analyzer -v
```

Expected: all analyzer tests pass.

### Task 2: Record Schema And Consumer Output

**Files:**
- Modify: `linux_native_hook_v1/common/hook_record.h`
- Modify: `linux_native_hook_v1/consumer/shm_consumer.cpp`

- [ ] **Step 1: Extend record schema**

Add `kStackMap` and stack fields:

```cpp
enum class HookEventType : uint32_t {
    kMalloc = 0,
    kFree = 1,
    kThreadName = 8,
    kStackMap = 9,
    kEnd = 12,
};

constexpr uint16_t kMaxStackFrames = 16;

struct HookRecord {
    timespec ts {};
    uint64_t addr = 0;
    uint64_t size = 0;
    uint64_t frames[kMaxStackFrames] = {0};
    uint32_t pid = 0;
    uint32_t tid = 0;
    uint32_t stack_id = 0;
    uint16_t stack_depth = 0;
    uint16_t type = 0;
    uint16_t tag_id = 0;
    uint16_t reserved = 0;
    char name[32] = {0};
};
```

- [ ] **Step 2: Print extended verbose and stack maps**

In both sharded and legacy consumer loops, add a branch:

```cpp
                if (verbose && record.type == static_cast<uint16_t>(HookEventType::kStackMap)) {
                    std::printf("STACKMAP,%u,%u",
                        static_cast<unsigned>(record.stack_id),
                        static_cast<unsigned>(record.stack_depth));
                    const uint16_t depth = record.stack_depth > kMaxStackFrames ? kMaxStackFrames : record.stack_depth;
                    for (uint16_t frame_index = 0; frame_index < depth; ++frame_index) {
                        std::printf(",0x%lx", static_cast<unsigned long>(record.frames[frame_index]));
                    }
                    std::printf("\n");
                    read_idx = (read_idx + 1) % shard_cap;
                    ++batch_count;
                    continue;
                }
```

For regular verbose lines, append stack metadata:

```cpp
                    std::printf("VERBOSE,%u,%u,%lu,%lu,%lu,%ld,%ld,%u,0x%lx\n",
                        static_cast<unsigned>(record.type),
                        static_cast<unsigned>(record.tid),
                        static_cast<unsigned long>(record.addr),
                        static_cast<unsigned long>(record.size),
                        static_cast<unsigned long>(record.pid),
                        static_cast<long>(record.ts.tv_sec),
                        static_cast<long>(record.ts.tv_nsec),
                        static_cast<unsigned>(record.stack_id),
                        static_cast<unsigned long>(record.frames[0]));
```

- [ ] **Step 3: Build compile check**

Run:

```powershell
cmake --build linux_native_hook_v1/build
```

If no existing build directory exists, configure first:

```powershell
cmake -S linux_native_hook_v1 -B linux_native_hook_v1/build
cmake --build linux_native_hook_v1/build
```

Expected: project builds or fails only because the current machine lacks Linux toolchain support. If Windows cannot build this Linux project, defer compile verification to WSL.

### Task 3: Stack Capture Helper

**Files:**
- Create: `linux_native_hook_v1/producer_hook/stack_capture.h`
- Create: `linux_native_hook_v1/producer_hook/stack_capture.cpp`
- Modify: `linux_native_hook_v1/CMakeLists.txt`

- [ ] **Step 1: Create stack capture API**

Create `stack_capture.h`:

```cpp
#pragma once

#include <cstdint>

#include "common/hook_record.h"

namespace linux_native_hook_v1 {

struct CapturedStack {
    uint32_t stack_id = 0;
    uint16_t depth = 0;
    uint64_t frames[kMaxStackFrames] = {0};
};

bool StackCaptureEnabled();
CapturedStack CaptureStack();

}  // namespace linux_native_hook_v1
```

- [ ] **Step 2: Implement opt-in `backtrace()` capture**

Create `stack_capture.cpp`:

```cpp
#include "producer_hook/stack_capture.h"

#include <algorithm>
#include <cstdlib>
#include <execinfo.h>

namespace linux_native_hook_v1 {
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

bool EnvEnabled(const char* value)
{
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

uint16_t ConfiguredDepth()
{
    const char* value = std::getenv("LNHV1_MAX_STACK_DEPTH");
    if (value == nullptr || value[0] == '\0') {
        return kMaxStackFrames;
    }
    const long parsed = std::strtol(value, nullptr, 10);
    if (parsed <= 0) {
        return 0;
    }
    return static_cast<uint16_t>(std::min<long>(parsed, kMaxStackFrames));
}

uint32_t HashFrames(const uint64_t* frames, uint16_t depth)
{
    uint64_t hash = kFnvOffset;
    for (uint16_t i = 0; i < depth; ++i) {
        uint64_t value = frames[i];
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= static_cast<uint8_t>(value & 0xffU);
            hash *= kFnvPrime;
            value >>= 8U;
        }
    }
    const uint32_t stack_id = static_cast<uint32_t>((hash >> 32U) ^ (hash & 0xffffffffU));
    return stack_id == 0 ? 1 : stack_id;
}

}  // namespace

bool StackCaptureEnabled()
{
    static const bool enabled = EnvEnabled(std::getenv("LNHV1_STACK_CAPTURE"));
    return enabled;
}

CapturedStack CaptureStack()
{
    CapturedStack captured {};
    if (!StackCaptureEnabled()) {
        return captured;
    }

    const uint16_t max_depth = ConfiguredDepth();
    if (max_depth == 0) {
        return captured;
    }

    void* raw[kMaxStackFrames] = {nullptr};
    const int depth = backtrace(raw, max_depth);
    if (depth <= 0) {
        return captured;
    }

    captured.depth = static_cast<uint16_t>(depth);
    for (uint16_t i = 0; i < captured.depth; ++i) {
        captured.frames[i] = reinterpret_cast<uint64_t>(raw[i]);
    }
    captured.stack_id = HashFrames(captured.frames, captured.depth);
    return captured;
}

}  // namespace linux_native_hook_v1
```

- [ ] **Step 3: Add source to CMake**

In `linux_native_hook_v1/CMakeLists.txt`, add `producer_hook/stack_capture.cpp` to the hook preload library target sources.

- [ ] **Step 4: Build check**

Run the same CMake build command as Task 2.

Expected: source compiles on Linux.

### Task 4: Producer Stack Map Emission

**Files:**
- Modify: `linux_native_hook_v1/producer_hook/hook_writer.h`
- Modify: `linux_native_hook_v1/producer_hook/hook_writer.cpp`

- [ ] **Step 1: Add stack map cache and helper declarations**

In `HookWriter`, add:

```cpp
    bool MaybeEmitStackMapLocked(const CapturedStack& stack);
    void FillRecordStack(HookRecord* record, const CapturedStack& stack);
```

Add a member:

```cpp
    std::unordered_set<uint32_t> emitted_stack_maps_;
```

Include `producer_hook/stack_capture.h`.

- [ ] **Step 2: Implement helper methods**

Add:

```cpp
void HookWriter::FillRecordStack(HookRecord* record, const CapturedStack& stack)
{
    if (record == nullptr || stack.stack_id == 0) {
        return;
    }
    record->stack_id = stack.stack_id;
    record->stack_depth = stack.depth;
    for (uint16_t i = 0; i < stack.depth && i < kMaxStackFrames; ++i) {
        record->frames[i] = stack.frames[i];
    }
}

bool HookWriter::MaybeEmitStackMapLocked(const CapturedStack& stack)
{
    if (stack.stack_id == 0 || stack.depth == 0) {
        return true;
    }
    if (emitted_stack_maps_.find(stack.stack_id) != emitted_stack_maps_.end()) {
        return true;
    }

    HookRecord map_record {};
    map_record.type = static_cast<uint16_t>(HookEventType::kStackMap);
    map_record.stack_id = stack.stack_id;
    map_record.stack_depth = stack.depth;
    const bool use_pid_tid_cache = GetPidTidCacheEnabled();
    map_record.pid = MetadataPid(use_pid_tid_cache);
    map_record.tid = MetadataTid(use_pid_tid_cache);
    map_record.ts = NowTs(clock_id_);
    for (uint16_t i = 0; i < stack.depth && i < kMaxStackFrames; ++i) {
        map_record.frames[i] = stack.frames[i];
    }

    const bool ret = WriteRecordLocked(map_record, false, false);
    if (ret) {
        emitted_stack_maps_.insert(stack.stack_id);
    }
    return ret;
}
```

- [ ] **Step 3: Capture stack in allocation paths**

In each full allocation record path, capture before filling the record:

```cpp
    const CapturedStack stack = CaptureStack();
```

After `MaybeWriteThreadNameLocked(...)` and before writing the allocation record under the writer lock:

```cpp
    MaybeEmitStackMapLocked(stack);
    FillRecordStack(&record, stack);
```

Apply to locked, sharded, thread-local, and Stage6 optimized allocation paths that write real malloc records. Free records may keep `stack_id=0`; analyzer groups frees by the original allocation stack after address matching.

- [ ] **Step 4: Preserve behavior when stack capture is disabled**

Run existing tests and demo with no `LNHV1_STACK_CAPTURE` set. Expected verbose output still parses and analyzer legacy tests pass.

### Task 5: Integration Demo And Docs

**Files:**
- Create: `linux_native_hook_v1/examples/nativehook_like_stack_verbose.txt`
- Modify: `linux_native_hook_v1/README.md`

- [ ] **Step 1: Add example verbose log**

Create an example with two stack maps and outstanding records:

```text
STACKMAP,42,3,0x401000,0x402000,0x403000
STACKMAP,84,3,0x501000,0x502000,0x503000
VERBOSE,0,101,0x1000,64,7,10,0,42,0x401000
VERBOSE,0,101,0x2000,32,7,11,0,42,0x401000
VERBOSE,1,101,0x1000,0,7,12,0,0,0x0
VERBOSE,0,102,0x3000,128,7,13,0,84,0x501000
```

- [ ] **Step 2: Document stack capture usage**

Add README commands:

```bash
LNHV1_STACK_CAPTURE=1 LNHV1_MAX_STACK_DEPTH=16 LD_PRELOAD=./libnative_hook_preload.so ./your_program
python3 tools/nativehook_like_analyzer.py verbose.log --markdown results/level3_report.md --json results/level3_report.json
```

Explain that `STACKMAP` frames are raw PCs and can be symbolized offline later.

- [ ] **Step 3: Run analyzer on example**

Run:

```powershell
python linux_native_hook_v1/tools/nativehook_like_analyzer.py linux_native_hook_v1/examples/nativehook_like_stack_verbose.txt
```

Expected: markdown includes `Stack Maps` and groups `stack:42`, `stack:84`.

### Task 6: Linux Verification And Sync

**Files:**
- Copy touched files to `/home/eden/projects/native_hook_planb`

- [ ] **Step 1: Run Windows-side Python verification**

Run:

```powershell
python -m unittest linux_native_hook_v1.tests.test_nativehook_like_analyzer -v
git diff --check
```

Expected: pass.

- [ ] **Step 2: Sync touched files to WSL**

Copy only touched files:

```powershell
wsl bash -lc "mkdir -p /home/eden/projects/native_hook_planb/docs/superpowers/plans /home/eden/projects/native_hook_planb/docs/superpowers/specs /home/eden/projects/native_hook_planb/linux_native_hook_v1/producer_hook /home/eden/projects/native_hook_planb/linux_native_hook_v1/tools /home/eden/projects/native_hook_planb/linux_native_hook_v1/tests /home/eden/projects/native_hook_planb/linux_native_hook_v1/examples"
```

Use `cp` from `/mnt/f/codex_workspace/native_hook/planb_project_page/...` to the matching WSL paths for all touched files.

- [ ] **Step 3: Run Linux verification**

Run:

```powershell
wsl bash -lc "cd /home/eden/projects/native_hook_planb && python3 -m unittest linux_native_hook_v1.tests.test_nativehook_like_analyzer -v && cmake -S linux_native_hook_v1 -B linux_native_hook_v1/build && cmake --build linux_native_hook_v1/build"
```

Expected: analyzer tests pass and C++ builds on Linux.

- [ ] **Step 4: Report residual risk**

Report whether `backtrace()` works in the hook path during a smoke run. If only compile tests pass, state that runtime LD_PRELOAD smoke still needs to be run before claiming production readiness.
