# AGENTS.md — native_hook Plan B

Instructions for future agents continuing the `native_hook` Plan B optimization work.

## Clone URLs

- GitHub SSH: `git@github.com:YiChiChen1106/Nativehook_linux_planb.git`
- GitLab HTTPS: `https://gitlab.youtune.tech/cychi/nativehook_linux_planb.git`
- Active branch: `optimize/writer-ring-sharded-batch`

## Local Workspace (WSL)

- Local root: `/mnt/f/codex_workspace/native_hook/`
- Main repo: `/mnt/f/codex_workspace/native_hook/planb_project_page/`
- Source tree: `/mnt/f/codex_workspace/native_hook/planb_project_page/linux_native_hook_v1/`
- Build output (WSL): `/tmp/nh-build/`
- CMake binary (pre-built): `/tmp/cmake-3.28.3-linux-x86_64/bin/cmake`

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
/tmp/cmake-3.28.3-linux-x86_64/bin/cmake -S /mnt/f/codex_workspace/native_hook/planb_project_page/linux_native_hook_v1 -B /tmp/nh-build
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

Above `main`:
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

## Current Next Step: Batch Semantic Validation

Do NOT redo these without explicit request:
- No new eBPF experiments
- No pid/tid cache experiments
- No tracking backend experiments
- No sample/filter sweeps

The task is to validate that delayed batch publication does not break the semantic guarantees needed:
1. Same-thread malloc/free order
2. Cross-thread free handling under `thread_local_fallback`
3. Thread-name record placement
4. Dropped-record count
5. Consumer termination and final flush behavior

## Conventions

- Prefer editing existing files; don't create new files unless necessary
- Don't add comments to code unless asked
- CRLF files on the Windows drive may show false diffs in WSL git — ignore line-ending-only diffs
- The standalone `/mnt/f/codex_workspace/native_hook/linux_native_hook_v1/` is an older copy — use `planb_project_page/linux_native_hook_v1/` for development
- If copying shell scripts from Windows to pink, strip CR first: `perl -pi -e 's/\r$//' scripts/*.sh`
