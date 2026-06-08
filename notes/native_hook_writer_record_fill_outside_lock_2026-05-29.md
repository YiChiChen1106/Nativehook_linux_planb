# Native hook writer/ring optimization: record fill outside writer lock

Date: 2026-05-29

Server: pink

Workload:
- `perf_test_data_linux`
- `pattern=mixed3`
- fixed total ops: `1000000`
- threads: `1,4,8,16`
- size: `32`
- `sample_interval=1`
- `filter_size=-1`
- `blocked=0`
- `flush_threshold=20`
- `LNHV1_ABLATION_STAGE=6`
- `LNHV1_PID_TID_CACHE=1`
- `LNHV1_TRACKING_MODE=thread_local_fallback`

## Change kept

Move the normal Stage 6 thread-local path's `HookRecord` fill, cached pid/tid lookup, and timestamp collection outside `HookWriter::mutex_`.

The writer mutex still protects:
- thread-name record emission
- ring capacity/index check
- shared memory record copy
- write-index publish
- notify threshold accounting

So this is not a semantic shortcut. It only shortens the shared writer mutex hold time.

## Formal CSV

`linux_native_hook_v1/results/hook_writer_ring_impact_record_fill_outside_lock_server_2026-05-29.csv`

## Full notify result versus B1

Compared against B1 `notify_after_unlock`:

| threads | B1 full notify | record fill outside lock | delta | improvement |
|---:|---:|---:|---:|---:|
| 1 | 1.561s | 1.545s | -0.016s | 1.0% |
| 4 | 3.507s | 2.718s | -0.789s | 22.5% |
| 8 | 3.641s | 3.121s | -0.519s | 14.3% |
| 16 | 3.821s | 3.408s | -0.413s | 10.8% |

Repeat spot checks:
- 8T: `3.325s`, `3.534s`
- 16T: `3.444s`, `3.346s`

## Diagnostics rejected

Flush-threshold-only tuning did not give stable improvement:
- threshold 20: 8T `3.612s`, 16T `3.723s`
- threshold 100: 8T `3.721s`, 16T `3.857s`
- threshold 500: 8T `3.824s`, 16T `3.745s`
- threshold 2000: 8T `3.368s`, 16T `3.986s`

Producer read-index cache was not stable:
- run 1: 8T `3.717s`, 16T `3.692s`
- run 2: 8T `3.901s`, 16T `3.838s`

Consumer count-only drain did not help:
- 8T `3.771s`
- 16T `3.763s`

Those experiments were reverted from code. Their CSVs are kept as diagnostic evidence.

## Interpretation

This confirms the writer mutex itself is not the whole bottleneck, but the length of work done while holding that mutex matters a lot under 4T/8T/16T. Moving metadata fill out of the critical section recovers a meaningful part of the Stage 6 scaling loss.

Remaining gap is still large:
- 8T: `atomic_publish_no_notify=1.982s` vs `full_notify=3.121s`
- 16T: `atomic_publish_no_notify=2.021s` vs `full_notify=3.408s`

Next useful direction is producer-side batching or ring reservation batching, because record copy, atomic publish, and full notify/consumer interaction are still visible after shortening the mutex hold.
