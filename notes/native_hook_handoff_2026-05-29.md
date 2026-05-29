# native_hook handoff

Date: 2026-05-29

This handoff is for continuing the `native_hook` Plan B work without redoing the older analysis.

## Workspace

- Local root: `F:\codex_workspace\native_hook`
- Main repo: `F:\codex_workspace\native_hook\planb_project_page`
- Active branch: `optimize/writer-ring-sharded-batch`
- Latest optimization commit before this handoff note: `4adf450 Add Stage 6 batch publish experiment`
- Pushed remotes:
  - GitHub `origin/optimize/writer-ring-sharded-batch`
  - GitLab `gitlab/optimize/writer-ring-sharded-batch`
- Server for formal performance numbers: `pink`
- Pink working directory:
  `/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`

The pink directory is the benchmark authority, but it is not the primary git workspace. Do code review, commits, and pushes from the local repo.

## Current Baseline Assumptions

Do not redo these unless the user explicitly asks:

- No new eBPF experiments.
- Do not repeat pid/tid cache experiments.
- Do not repeat tracking backend experiments.
- Do not repeat sample/filter sweeps.
- Formal performance numbers only count when run on pink.
- Preserve old 2026-05-27 experiment assets and notes; they are important evidence.

Confirmed optimized Stage 6 environment:

```bash
LNHV1_ABLATION_STAGE=6
LNHV1_PID_TID_CACHE=1
LNHV1_TRACKING_MODE=thread_local_fallback
```

The useful pid/tid cache and `thread_local_fallback` tracking work has already been integrated into `main` at:

- `c13d46d Add thread-local miss fallback tracking experiment`
- older supporting experiment branches/commits include pid/tid and tracking overall impact

## Branch Commit Stack

Current branch stack above `main`:

- `059cf82 Add writer ring impact experiment`
- `617c0b3 Split shm header cache lines`
- `d7f0f16 Revert "Split shm header cache lines"`
- `b29cb5c Move stage6 notify outside writer mutex`
- `1b3ec69 Move record fill outside writer mutex`
- `4adf450 Add Stage 6 batch publish experiment`

Interpretation:

- Cache-line split was tested and reverted because it did not give a stable gain.
- Notify outside writer mutex was kept.
- Record fill outside writer mutex was kept.
- Stage 6 batching was added as an experimental env-gated path, default off.

## Important Code Areas

- `linux_native_hook_v1/producer_hook/hook_writer.cpp`
  - Stage 6 thread-local record path
  - writer mutex critical section
  - shared ring copy and `write_index` publish
  - notify threshold handling
  - experimental Stage 6 batch path
- `linux_native_hook_v1/producer_hook/hook_writer.h`
  - `HookWriter` private helpers for batching and ring writes
- `linux_native_hook_v1/producer_hook/ablation_config.cpp`
  - env parsing and cached config
- `linux_native_hook_v1/producer_hook/ablation_config.h`
  - ablation constants and `kMaxStage6BatchSize`
- `linux_native_hook_v1/scripts/run_hook_writer_ring_impact.sh`
  - formal writer/ring impact benchmark driver
- `linux_native_hook_v1/tests/ablation_config_test.cpp`
  - config parser tests

Project instruction from `AGENTS.md`: prefer codebase-memory MCP graph tools for code discovery when available; use grep/ripgrep mainly for strings, configs, scripts, and non-code files.

## Experiments And Results

### Writer/Ring Impact

Formal note:

- `notes/native_hook_writer_ring_impact_2026-05-28.md`

Formal CSV:

- `linux_native_hook_v1/results/hook_writer_ring_impact_server_2026-05-28.csv`

Sub-stages:

- `28` `stage6_opt_no_writer_ring`
- `29` `stage6_opt_writer_mutex_only`
- `30` `stage6_opt_ring_index_check`
- `31` `stage6_opt_record_copy_no_publish`
- `32` `stage6_opt_atomic_publish_no_notify`
- `33` `stage6_opt_full_notify`

Conclusion:

Stage 6 optimized does not stall primarily on the mutex alone. `writer_mutex_only` adds some cost, but the larger scaling penalties come from ring index/copy, atomic publish, and finally notify/consumer interaction. Shared ring-state traffic is the main multithread bottleneck, with notify/consumer as a large extra cost.

### Notify Outside Writer Mutex

Formal CSVs:

- `linux_native_hook_v1/results/hook_writer_ring_impact_notify_after_unlock_server_2026-05-29.csv`
- `linux_native_hook_v1/results/hook_writer_ring_impact_notify_after_unlock_server_2026-05-29_repeat.csv`

Note:

- `notes/native_hook_writer_notify_after_unlock_2026-05-29.md`

Kept because it helped 8T/16T and reduced time spent notifying while holding the writer mutex.

### Record Fill Outside Writer Mutex

Formal note:

- `notes/native_hook_writer_record_fill_outside_lock_2026-05-29.md`

Formal CSV:

- `linux_native_hook_v1/results/hook_writer_ring_impact_record_fill_outside_lock_server_2026-05-29.csv`

Result versus B1 notify-after-unlock:

| threads | B1 full notify | record fill outside lock | improvement |
|---:|---:|---:|---:|
| 1 | 1.561s | 1.545s | 1.0% |
| 4 | 3.507s | 2.718s | 22.5% |
| 8 | 3.641s | 3.121s | 14.3% |
| 16 | 3.821s | 3.408s | 10.8% |

Conclusion:

The mutex itself is not the whole bottleneck, but shortening the work done inside the shared writer mutex matters under multithreading.

Rejected diagnostics around this step:

- Flush-threshold-only tuning was unstable.
- Producer read-index cache was unstable or worse.
- Consumer count-only drain did not help.

Their CSVs are kept as diagnostic evidence; do not delete them.

### Stage 6 Batch Publish

Commit:

- `4adf450 Add Stage 6 batch publish experiment`

Formal note:

- `notes/native_hook_stage6_batch_publish_2026-05-29.md`

Formal CSV:

- `linux_native_hook_v1/results/hook_writer_ring_impact_stage6_batch64_server_2026-05-29.csv`

Additional diagnostic CSVs:

- `linux_native_hook_v1/results/diag_stage6_batch_default_off_t8_t16_server_2026-05-29.csv`
- `linux_native_hook_v1/results/diag_stage6_batch_size_4_t8_t16_server_2026-05-29.csv`
- `linux_native_hook_v1/results/diag_stage6_batch_size_8_t8_t16_server_2026-05-29.csv`
- `linux_native_hook_v1/results/diag_stage6_batch_size_16_t8_t16_server_2026-05-29.csv`
- `linux_native_hook_v1/results/diag_stage6_batch_size_32_t8_t16_server_2026-05-29.csv`
- `linux_native_hook_v1/results/diag_stage6_batch_size_32_t8_t16_server_2026-05-29_repeat.csv`
- `linux_native_hook_v1/results/diag_stage6_batch_size_64_t8_t16_server_2026-05-29_repeat.csv`

Env gate:

```bash
LNHV1_STAGE6_BATCH_SIZE=<1..64>
```

Default is `0`, so existing Stage 6 behavior is unchanged unless this env var is set.

Batching is enabled only for:

- Stage 6 full notify
- no `LNHV1_SUBABLATION_STAGE`
- non-blocked mode
- `batch_size > 1`

Batch implementation summary:

- Per-thread `Stage6RecordBatch` buffers `HookRecord`s.
- Same-thread thread-name/event order is preserved inside the batch.
- The producer takes the writer mutex once per batch.
- It copies multiple records into the shared ring and publishes `write_index` once.
- Thread-local destructor flushes any remaining records through `HookWriter::Flush()`.

Result versus no-batch record-fill-outside-lock:

| threads | no batch | batch64 | improvement |
|---:|---:|---:|---:|
| 1 | 1.545s | 1.217s | 21.2% |
| 4 | 2.718s | 0.859s | 68.4% |
| 8 | 3.121s | 1.278s | 59.1% |
| 16 | 3.408s | 1.340s | 60.7% |

Batch-size checks:

- batch4: 8T `1.827s`, 16T `1.886s`
- batch8: 8T `1.578s`, 16T `1.584s`
- batch16: 8T `1.473s`, 16T `1.440s`
- batch32: 8T `1.362s`, 16T `1.413s`
- batch32 repeat: 8T `1.353s`, 16T `1.355s`
- batch64 repeat: 8T `1.262s`, 16T `1.332s`

Default-off check:

- env unset: 8T `3.390s`, 16T `3.355s`

Conclusion:

Batching strongly supports the hypothesis that per-record shared ring publication is now the dominant remaining Stage 6 scaling cost. Reducing writer mutex acquisitions and atomic `write_index` publishes moves Stage 6 full notify from roughly `3.1-3.4s` to `1.2-1.3s` at 8T/16T.

Important caveat:

Do not claim cross-thread semantics are fully verified. Batching delays publication, so global cross-thread event ordering can differ from immediate publish. Same-thread order inside the batch is preserved. A dedicated cross-thread semantic check is the next required step before making batching the default optimized behavior.

## Verification Already Done

On pink after the batch implementation:

```bash
cd /mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Result:

- `21/21` tests passed.

Local git state after push was clean.

## Suggested Next Step

Do the Stage 6 batch semantic validation before optimizing further:

1. Build a deterministic cross-thread test or script that checks whether delayed publish changes the semantic guarantees the paper depends on.
2. Compare default immediate publish versus `LNHV1_STAGE6_BATCH_SIZE=64`.
3. Specifically inspect:
   - same-thread malloc/free order
   - cross-thread free handling under `thread_local_fallback`
   - thread-name record placement
   - dropped-record count
   - consumer termination and final flush behavior
4. If the semantic difference is acceptable, decide whether to:
   - keep batching env-gated for experiments only, or
   - make a conservative batch size the Stage 6 optimized default.

After semantic validation, possible optimization directions:

- Try smaller default batch sizes such as `16` or `32` if ordering/latency matters.
- Consider per-producer ring reservation or sharded producer rings if global ordering must be stricter than batch publish allows.
- Consider consumer-side changes only after the producer publish semantics are nailed down.

## Commands For Future Agents

Check branch and status:

```powershell
cd F:\codex_workspace\native_hook\planb_project_page
git status --short
git log --oneline --decorate -12
```

Run pink build/test:

```powershell
ssh -i "C:\Users\28100\.ssh\id_rsa" -o ConnectTimeout=5 -o StrictHostKeyChecking=no -o UserKnownHostsFile=NUL cychi@10.87.235.29 "cd /mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1 && cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure"
```

Run the batch64 writer/ring impact experiment on pink only if fresh formal numbers are needed:

```bash
cd /mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1
LNHV1_STAGE6_BATCH_SIZE=64 \
LNHV1_ABLATION_STAGE=6 \
LNHV1_PID_TID_CACHE=1 \
LNHV1_TRACKING_MODE=thread_local_fallback \
LNHV1_SUBABLATION_STAGE_LIST=28,29,30,31,32,33 \
LNHV1_CSV_PATH=results/hook_writer_ring_impact_stage6_batch64_server_2026-05-29.csv \
bash scripts/run_hook_writer_ring_impact.sh
```

If copying shell scripts from Windows to pink, convert CRLF to LF before running:

```bash
perl -pi -e 's/\r$//' scripts/*.sh
```

## Short PPT Takeaway

After pid/tid cache and thread-local fallback tracking, Stage 6 no longer scales poorly because of metadata or tracking alone. The writer/ring experiment shows the remaining cost is shared ring publication: record copy, atomic `write_index` publish, and notify/consumer interaction dominate under 8T/16T. Moving notify and record fill out of the mutex helped, but producer-side batching is the first large win, cutting 8T/16T full-notify time from about `3.1-3.4s` to `1.2-1.3s`. Batching should stay experimental until cross-thread semantics are explicitly validated.
