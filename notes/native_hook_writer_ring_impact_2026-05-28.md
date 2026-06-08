# native_hook writer/ring impact

## Experiment

- Host: `pink`
- CSV: `results/hook_writer_ring_impact_server_2026-05-28.csv`
- Workload: `perf_test_data_linux`
- Total ops: `1,000,000`
- Threads: `1, 4, 8, 16`
- Size: `32`
- Sample interval: `1`
- Filter size: `-1`
- Tracking mode: `thread_local_fallback`
- PID/TID cache: `1`
- Main stage: `6`

## Stage 6 sub-stages

- `28` `stage6_opt_no_writer_ring`
- `29` `stage6_opt_writer_mutex_only`
- `30` `stage6_opt_ring_index_check`
- `31` `stage6_opt_record_copy_no_publish`
- `32` `stage6_opt_atomic_publish_no_notify`
- `33` `stage6_opt_full_notify`

## Results

1T:
`0.972 -> 1.080 (+0.109) -> 1.136 (+0.055) -> 1.174 (+0.039) -> 1.222 (+0.048) -> 1.493 (+0.271)`

4T:
`0.723 -> 0.819 (+0.096) -> 1.068 (+0.248) -> 1.367 (+0.299) -> 1.928 (+0.561) -> 3.256 (+1.328)`

8T:
`0.849 -> 0.895 (+0.046) -> 0.976 (+0.081) -> 1.552 (+0.576) -> 2.019 (+0.467) -> 4.154 (+2.134)`

16T:
`0.856 -> 0.896 (+0.040) -> 1.088 (+0.193) -> 1.660 (+0.572) -> 2.021 (+0.361) -> 3.923 (+1.903)`

Full notify record counts:
- 1T: `2,002,004`
- 4T: `2,002,012`
- 8T: `2,002,026`
- 16T: `2,002,061`

## Readout

- `writer_mutex_only` does add cost, but it is not the dominant jump.
- `ring_index_check` and especially `record_copy_no_publish` become much more expensive at 4T/8T/16T, which points to shared ring-state contention and record copying as the main scaling drag.
- `atomic_publish_no_notify` adds another visible step, so the write-index path is also part of the problem.
- `full_notify` is the biggest extra cost at every thread count, so notify/consumer interaction still matters a lot.

## PPT conclusion

Stage 6 optimized does not stall primarily on the mutex alone. The first clear step up is small at `writer_mutex_only`, but the bigger penalties come from the ring path, especially `record_copy` and `atomic_publish`, and `full_notify` adds the largest remaining tax. In short: shared ring-state traffic is the main multithread scaling bottleneck, and notify/consumer wakeups are still a large extra cost on top of that.
