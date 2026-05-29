# native_hook writer notify-after-unlock

## Change

- Moved the non-blocked stage 6 writer `eventfd` notify out of the writer mutex in `hook_writer.cpp`.
- Kept blocked/self-drain behavior unchanged.
- Also moved `Flush()` notify out of the mutex for consistency.

## Environment

- Host: `pink`
- Workload: `perf_test_data_linux`
- Pattern: `mixed3`
- Total ops: `1,000,000`
- Threads: `1, 4, 8, 16`
- Size: `32`
- Tracking mode: `thread_local_fallback`
- PID/TID cache: `1`
- Main stage: `6`
- Sub-stages: `28..33`

## CSVs

- `results/hook_writer_ring_impact_notify_after_unlock_server_2026-05-29.csv`
- `results/hook_writer_ring_impact_notify_after_unlock_server_2026-05-29_repeat.csv`

## Readout

Main run, `full_notify`:

- `1T`: `1.493 -> 1.561`
- `4T`: `3.256 -> 3.507`
- `8T`: `4.154 -> 3.641`
- `16T`: `3.923 -> 3.821`

Repeat run, `full_notify`:

- `8T`: `4.154 -> 3.674`
- `16T`: `3.923 -> 3.794`

## Conclusion

Moving notify out of the writer mutex is a real win at the thread counts we care about most.
It does not fix the ring path by itself, but it trims a meaningful chunk off the `full_notify` cost at 8T and 16T.
I kept the change.
