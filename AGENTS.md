# AGENTS.md — native_hook Plan B

Instructions for future agents continuing the `native_hook` Plan B optimization work.

## Repository Map (Three Repos)

### 1. Plan B Prototype — `native_hook_linux_planb`

- GitHub: `git@github.com:YiChiChen1106/Nativehook_linux_planb.git`
- GitLab mirror: `https://gitlab.youtune.tech/cychi/nativehook_linux_planb.git`
- Active branch: `optimize/writer-ring-sharded-batch`
- Build: CMake (local WSL + pink server)
- Purpose: **Fast-iteration ablation experiments and benchmark**

### 2. Personal OH Fork — `cyc_nativehook`

- GitLab: `git@gitlab.youtune.tech:cychi/cyc_nativehook.git`
- Branch: `master` (synced with team fork)
- Build: GN + full OH SDK (not yet available)
- Purpose: **Translate prototype optimizations to real hook_client.cpp, verify manually**

### 3. Team OH Fork — `yt_nativehook`

- GitLab: `git@gitlab.youtune.tech:memory_leak/yt_nativehook.git`
- Branch: `master` (OH upstream + Plan B squash)
- Build: GN + full OH SDK
- Purpose: **Submit MRs for verified optimizations, team review**

## Target Codebase (Real OpenHarmony)

- Gitee upstream: `https://gitee.com/openharmony/developtools_profiler`
- Target path: `device/plugins/native_hook/src/hook_client.cpp`
- Build system: GN (not CMake), requires full OpenHarmony SDK
- WSL clone (team + personal fork in one repo): `/home/eden/projects/openharmony_nativehook/`
  - Remote `gitlab` → team fork (`memory_leak/yt_nativehook`)
  - Remote `origin` → personal fork (`cychi/cyc_nativehook`)

## Development Workflow

Every optimization follows this three-stage pipeline:

```
原型 (native_hook_linux_planb)
  │  CMake, WSL/pink, fast iteration
  │  ablation sub-stage measurement
  │  Prove the idea works with data
  ▼
个人 fork (cyc_nativehook)
  │  Translate: prototype C++ → hook_client.cpp
  │  Three-question pre-flight check:
  │    1. Which prototype module does this change?
  │    2. Which real file/function does it map to?
  │    3. Does the real code actually execute this path?
  │  If any question can't be answered → skip
  ▼
团队 fork (yt_nativehook)
  │  Submit MR with verified changes
  │  Team review → merge
```

**Why the personal fork middle step?** Past optimizations failed to port because
the check wasn't done (PID/TID cache was redundant, tracking fallback had no target).
The personal fork is a low-cost pre-screening layer before team review.

## Local Workspace (WSL)

- Plan B repo: `/home/eden/projects/native_hook_planb/`
- Source tree: `linux_native_hook_v1/` (inside the repo)
- Build output: `/tmp/nh-build/`
- CMake: `/tmp/cmake-3.28.3-linux-x86_64/bin/cmake`
- Old Windows-drive copy (avoid, CRLF issues): `/mnt/f/codex_workspace/native_hook/`

## Pink Server (benchmark authority)

- Host: `10.87.235.29`
- User: `cychi`
- SSH key: `~/.ssh/id_rsa`
- Working directory: `/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`

Pink is the official benchmark server. **Formal performance numbers only count when run on pink.**
Never treat local (WSL) performance numbers as formal results.

## Build

### Local (WSL) build

```bash
/tmp/cmake-3.28.3-linux-x86_64/bin/cmake -S /home/eden/projects/native_hook_planb/linux_native_hook_v1 -B /tmp/nh-build
/tmp/cmake-3.28.3-linux-x86_64/bin/cmake --build /tmp/nh-build -j
```

### Local (WSL) test

```bash
/tmp/cmake-3.28.3-linux-x86_64/bin/ctest --test-dir /tmp/nh-build --output-on-failure
```

### Pink build & test

```bash
ssh -i ~/.ssh/id_rsa -o StrictHostKeyChecking=no cychi@10.87.235.29 \
  "cd /mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1 && \
   cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure"
```

## Dependencies

- If only reading/editing code: no dependencies needed
- If building/testing locally: cmake, make (or ninja), g++ (or clang++), standard Linux dev headers
- Pink has a full build environment and sudo access

## Current Branch Commit Stack

`optimize/writer-ring-sharded-batch` above `main` (12 commits, 2026-06-08):

Deadlock fix & ablation data:
- `e19571b` Fix sub-stage 36 deadlock: remove double-lock on StackWriter inner_mutex_
- `3c33e3c` Update docs: sub-stage 36 fix & work log entry
- `61b194d` Update docs: sub-stage 34/35 new benchmark data after deadlock fix
- `69de811` Add sub-stage 36 full chain benchmark data

Experiments:
- `f0bd79a` Add StackWriter batch publish experiment (**negative result**)
- `4aa70b1` Add eventfd flush threshold optimization for StackWriter (~4%)
- `5ab84a2` Route free path through StackWriter for sub-stage 34/35/36

Docs:
- `ff6c837` Add work log 2026-06-05
- `b8ab30a` Rewrite work log 2026-06-05 as comprehensive 6/4-6/5 record
- `f80f867` Update work log: eventfd flush threshold optimization results
- `18c3ee0` Add three-repo workflow and repository map to AGENTS.md

Earlier commits:
- `8d82a43` Add native hook handoff note
- `4adf450` Add Stage 6 batch publish experiment
- `1b3ec69` Move record fill outside writer mutex
- `b29cb5c` Move stage6 notify outside writer mutex
- `d7f0f16` Revert "Split shm header cache lines"
- `617c0b3` Split shm header cache lines
- `059cf82` Add writer ring impact experiment

## Optimized Stage 6 Environment

```bash
LNHV1_ABLATION_STAGE=6
LNHV1_PID_TID_CACHE=1
LNHV1_TRACKING_MODE=thread_local_fallback
```

## Key Code Areas

- `producer_hook/hook_writer.cpp` — Stage 6 thread-local record path, writer mutex, shared ring, batch publish
- `producer_hook/hook_writer.h` — `HookWriter` interface, batch helpers
- `producer_hook/ablation_config.cpp` — env parsing and cached config
- `producer_hook/ablation_config.h` — ablation constants, `kMaxStage6BatchSize`
- `producer_hook/hook_preload.cpp` — LD_PRELOAD entry points (malloc/free interposition)
- `common/shm_layout.h` — ShmHeader, ring buffer layout, atomic helpers
- `common/hook_record.h` — HookRecord struct
- `consumer/shm_consumer.cpp` — ring buffer consumer
- `consumer/server_main.cpp` — consumer server main loop
- `scripts/run_hook_writer_ring_impact.sh` — formal writer/ring impact benchmark
- `tests/ablation_config_test.cpp` — config parser tests

## Stage 6 Batch Publish

Env gate: `LNHV1_STAGE6_BATCH_SIZE=<1..64>` (default 0 = off)

Batching is active only when: ablation_stage=6, no SUBABLATION_STAGE, non-blocked mode, batch_size > 1.

Per-thread `Stage6RecordBatch` buffers records, preserves same-thread order, then takes writer mutex once per batch.

Current result (batch64 vs no-batch):
| threads | no batch | batch64 | improvement |
|---:|---:|---:|---:|
| 1 | 1.545s | 1.217s | 21.2% |
| 4 | 2.718s | 0.859s | 68.4% |
| 8 | 3.121s | 1.278s | 59.1% |
| 16 | 3.408s | 1.340s | 60.7% |

## Current Status & Next Steps

### Alignment Status

Hot-path mapping table (prototype ↔ real OH):

```
                          Prototype              Real (hook_client.cpp)    Status
────────────────────────────────────────────────────────────────────────────────
 malloc/filter/sample     ✅ hook_writer         ✅ hook_malloc             aligned
 re-entry guard           ✅ HookReentryGuard    ✅ __set_hook_flag         aligned
 FpUnwind stack walk      ❌ missing             ✅ FpUnwind()              x86 can't do
 GetStackSize             ❌ missing             ✅ GetStackSize()          x86 can't do
 StackRawData fill        ⚠️ simplified          ✅ rawdata                 simplified
 record size calc         ⚠️ simplified          ✅ realSize                simplified
 client lock              ❌ missing             ✅ weakClient.lock()       see note
 UpdateThreadName         ❌ missing             ✅ UpdateThreadName()      in HookWriter
 AddressHandler tracking  ⚠️ stub (header only)  ✅ AddAllocAddr()          not wired
 StackWriter write/flush  ✅ stack_writer.cpp    ✅ WriteWithPayloadTimeout aligned
 notify                   ✅ NotifyEventFd       ✅ PrepareFlush/Flush      aligned
```

Notes:
- `address_handler.h` exists as abstract interface but is not wired into the hot path.
  Thread-local `unordered_set` in HookWriter serves the same purpose.
- `client lock` (`weakClient.lock()`) was not implemented as a separate layer.
  HookWriter's `mutex_` and StackWriter's `inner_mutex_` jointly cover this.
- Items marked `❌ missing` or `⚠️ simplified` that involve aarch64-specific
  functionality (FpUnwind, etc.) cannot be replicated in the x86 prototype.

### Ablation Sub-stage Map

| Sub | Name | Measures | Status |
|---|---|---|---|
| 28-33 | Writer/Ring impact | HookWriter layer overhead | ✅ done |
| 34 | write_only | StackWriter ring write + inner_mutex_ | ✅ 0.28s (1T) |
| 35 | flush_only | 34 + eventfd syscall | ✅ 0.82s (1T) |
| 36 | full | 35 + consumer drain | ✅ 0.84s (1T) |

### Remaining Directions

Priority order:

1. **Consumer ablation** — Break down sub=36's consumer drain (eventfd read, ring traversal, read_index update). Currently a black box at ~0.03s total.
2. **Simulate longer critical section** — Prototype's ring write is ~25ns; real ShareMemoryBlock may be heavier with FpUnwind data memcpy. Add a configurable delay to inner_mutex_ critical section to assess contention at realistic scales.
3. **Real code compilation** — Blocked on OH SDK. Once available, build the GitLab fork and validate ported optimizations.

### Do NOT Redo

- No new eBPF experiments
- No pid/tid cache experiments (redundant — real code already does this)
- No tracking backend experiments (no equivalent in real code)
- No sample/filter sweeps
- No StackWriter batch publish (proven ineffective)
- No lock-free ring buffer (inner_mutex_ too light to justify complexity)

## Optimization Methodology: Prototype-Real Alignment

To avoid wasting time on optimizations that work in the prototype but have no target in the real codebase,
every proposed optimization MUST pass these three checks before any code is written:

### Step 1: Draw a Hot-Path Mapping Table

Map every step of the real `hook_malloc` hot path to its prototype equivalent (or mark "missing"):

```
                          Prototype              Real (hook_client.cpp)
──────────────────────────────────────────────────────────────────
 malloc/filter/sample     ✅ hook_writer         ✅ hook_malloc
 re-entry guard           ✅ HookReentryGuard    ✅ __set_hook_flag
 FpUnwind stack walk      ❌ missing             ✅ FpUnwind()
 GetStackSize             ❌ missing             ✅ GetStackSize()
 StackRawData fill        ⚠️ simplified          ✅ rawdata.{pid,tid,size,addr,ts}
 record size calc         ⚠️ simplified          ✅ realSize by fpunwind mode
 client lock              ❌ missing             ✅ weakClient.lock()
 UpdateThreadName         ❌ missing             ✅ UpdateThreadName()
 AddressHandler tracking  ✅ address_handler.h   ✅ AddAllocAddr()
  StackWriter write/flush  ✅ stack_writer.cpp    ✅ WriteWithPayloadTimeout/Flush
  notify                   ✅ NotifyEventFd       ✅ PrepareFlush/Flush

StackWriter ablation sub-stages (34-36):
  sub=34 write_only → ShareMemoryBlock::PutWithPayloadTimeout cost
  sub=35 flush_only → write + EventNotifier::Post cost
  sub=36 full       → complete SendStackWithPayload chain
```

### Step 2: Re-Align the Prototype When Gaps Are Found

When the mapping table shows `❌ missing` or `⚠️ simplified` entries, refactor the prototype
before proposing new optimizations. The goal is **structural comparability**: same modules,
same boundaries, even if simplified internally. Example: split `hook_writer.cpp` into layers
matching `hook_client` + `stack_writer` + `address_handler`.

### Step 3: Pre-Flight Check for Every Optimization

Before writing any optimization code, answer three questions:

1. **Which prototype module does this change?** (e.g. "the record-fill path in hook_writer")
2. **Which real file/function does it map to?** (e.g. "hook_malloc in hook_client.cpp, lines 590-610")
3. **Does the real code actually execute this path?** (must cite the exact line that proves it)

If any question can't be answered with specific file:line references, do not proceed.

### Lessons Learned

Two optimizations from the prototype failed to port because this check wasn't done:

- **PID/TID cache**: Real code already uses `pthread_getspecific` (TID) and `atomic load` (PID).
  The optimization was correct in direction but redundant — real code had it before the prototype existed.

- **thread_local_fallback tracking**: Prototype's producer-side alloc table (hash set + sharded fallback)
  has no equivalent in the real code, where free records are sent raw to the daemon for server-side matching.
  The optimization targeted a bottleneck that only existed in the simplified architecture.

## Conventions

- **Before taking any action (editing files, running commands that modify the codebase or system),
  explain what you plan to do in Chinese first and wait for explicit permission.**
- **After answering a question, suggest next steps ranked by recommendation priority (highest first).**
- **黄总 (the supervisor) is briefed every Thursday. Do not suggest reaching out to him outside of
  the weekly Thursday cadence unless something has gone wrong.**
- Prefer editing existing files; don't create new files unless necessary
- Don't add comments to code unless asked
- Use `/home/eden/projects/native_hook_planb/` for development (WSL native, no CRLF issues)
- The old Windows-drive copy at `/mnt/f/codex_workspace/native_hook/` has CRLF corruption — do not use for development, only read from it if needed for reference
- If copying shell scripts from Windows to pink, strip CR first: `perl -pi -e 's/\r$//' scripts/*.sh`
