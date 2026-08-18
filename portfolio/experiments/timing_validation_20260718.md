# BCC timing validation

## growth_leak

Parameters: `start_delay_ms=0`, `delay_ms=1000`, `interval=1s`, `count=9`, three repeats.

Expected checkpoints for two threads are approximately:

```text
668 -> 1336 -> 2004 -> 2672 -> 3340 -> 4000 blocks
```

Observed Benchmark-attributed snapshots:

```text
repeat 1: 668 -> 1336 -> 2004 -> 2664 -> 2664 -> 2664 -> 2664 -> 2664 -> 2664
repeat 2: 668 -> 1336 -> 2004 -> 2664 -> 2664 -> 2664 -> 2664 -> 2664 -> 2664
repeat 3: 667 -> 1335 -> 2003 -> 2663 -> 2663 -> 2663 -> 2663 -> 2663 -> 2663
```

The first three checkpoints follow the expected growth. Later snapshots stop around 2663/2664 while the benchmark remains alive for the post-delay window. This indicates that the current BCC collection path loses later allocation events or stops updating its outstanding-allocation state under this workload. The timing experiment confirms that the snapshot interval can observe growth, while the later undercount remains an open BCC limitation.

## delayed_free

Parameters: `start_delay_ms=2000`, `delay_ms=5000`, `interval=1s`, `count=9`, three repeats.

All three repeats observed the same phase transition:

```text
before allocation: runtime-only stacks
after allocation: 4000 blocks / 256000 bytes
before and around free: 4000 blocks / 256000 bytes
after free: 0 Benchmark-attributed blocks / 0 bytes
```

The final summaries for all three repeats are `matches_expected=yes`. Runtime and allocator stacks are retained in `notes`; they are excluded from Benchmark totals.

## Conclusion

The delayed-free timing is now aligned with the intended truth checkpoints. Growth detection is partially validated: early growth is visible, while the later plateau at 2663/2664 requires a separate investigation into BCC event tracking or map updates before using growth recall as a final performance conclusion.
