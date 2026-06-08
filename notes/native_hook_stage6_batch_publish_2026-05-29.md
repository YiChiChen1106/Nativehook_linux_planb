# Native hook Stage 6 batch publish experiment

Date: 2026-05-29

Server: pink

Workload:
- `perf_test_data_linux`
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

## Change

Added an experimental Stage 6 producer-side batch path controlled by:

```bash
LNHV1_STAGE6_BATCH_SIZE=<1..64>
```

Default is `0`, so existing Stage 6 behavior is unchanged unless the env var is set.

The batch path is enabled only for:
- Stage 6 full notify
- no `LNHV1_SUBABLATION_STAGE`
- non-blocked mode
- `batch_size > 1`

It buffers per-thread `HookRecord`s, preserves per-thread thread-name/event order inside the batch, then takes the writer mutex once, copies the batch into the shared ring, and publishes `write_index` once.

## Formal CSV

`linux_native_hook_v1/results/hook_writer_ring_impact_stage6_batch64_server_2026-05-29.csv`

## Result

Compared against the previous no-batch record-fill-outside-lock result:

| threads | no batch | batch64 | delta | improvement |
|---:|---:|---:|---:|---:|
| 1 | 1.545s | 1.217s | -0.328s | 21.2% |
| 4 | 2.718s | 0.859s | -1.859s | 68.4% |
| 8 | 3.121s | 1.278s | -1.844s | 59.1% |
| 16 | 3.408s | 1.340s | -2.068s | 60.7% |

Targeted batch-size checks:
- batch4: 8T `1.827s`, 16T `1.886s`
- batch8: 8T `1.578s`, 16T `1.584s`
- batch16: 8T `1.473s`, 16T `1.440s`
- batch32: 8T `1.362s`, 16T `1.413s`
- batch32 repeat: 8T `1.353s`, 16T `1.355s`
- batch64 repeat: 8T `1.262s`, 16T `1.332s`

Default-off check:
- env unset: 8T `3.390s`, 16T `3.355s`

## Interpretation

This strongly supports the hypothesis that per-record shared ring publication is the dominant remaining Stage 6 scaling cost. Reducing writer mutex acquisitions and publishing `write_index` once per batch moves Stage 6 full notify from roughly `3.1-3.4s` to `1.2-1.3s` at 8T/16T.

The result is now in the same range as, and at 8T better than, the earlier libbpf semantic ring reference for fixed 1M mixed allocator ops.

## Caveat

This is still an experimental path. Batching delays publication of records, so global cross-thread event ordering can differ from the immediate-publish path. Same-thread order inside a batch is preserved. Before making this the default optimized behavior, run a cross-thread semantics check and decide whether delayed publication is acceptable for the paper's "semantic ring" comparison.
