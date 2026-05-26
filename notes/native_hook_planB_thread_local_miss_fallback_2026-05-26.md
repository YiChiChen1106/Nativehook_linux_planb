# Per-thread tracking miss-fallback experiment

## What changed

This experiment changes `LNHV1_TRACKING_MODE=thread_local_fallback` from the old local + sharded mirror design to a lighter ownership / miss-fallback design.

- Alloc path: insert into the thread-local live set and record `addr -> owner_context` in a sharded ownership map.
- Free path, local hit: erase only the thread-local entry and return. It does not take the shared ownership shard mutex.
- Free path, local miss: check the sharded ownership map. If the owner is another thread, consume that ownership entry and record the cross-thread free.

The key change is that same-thread free no longer pays the fallback shard lookup/erase cost. The shared fallback path is only used on local miss.

## Validation

Built and tested on `pink`:

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build -R ablation_config --output-on-failure
```

Hotpath profile smoke, one thread, Stage 4 full tracking:

| design | throughput ops/s | tracking shard mutex count |
|---|---:|---:|
| old thread_local_fallback profile | 445,680 | 891,367 |
| miss-fallback profile | 670,603 | 670,609 |

The mutex count drops from roughly alloc + free to roughly alloc only, which matches the intended design.

Cross-thread smoke with `cross_thread_free_linux --count 1000 --size 32` completed successfully and the consumer log showed alloc/free records in the expected range (`alloc=1004`, `free=998` in the last sampled line, no drops).

## Formal result

CSV:

```text
linux_native_hook_v1/results/hook_per_thread_tracking_miss_fallback_server_2026-05-26.csv
```

Parameters: `threads=1,4`, `size=32`, `duration=5`, `sample_interval=1`, `filter_size=-1`, `blocked=0`, `flush_threshold=20`.

### Stage 4 tracking

| threads | global | sharded | old thread_local_fallback | miss-fallback | thread_local_only |
|---:|---:|---:|---:|---:|---:|
| 1 | 1.21M | 1.25M | 1.11M | 1.19M | 1.38M |
| 4 | 0.53M | 0.51M | 0.46M | 1.70M | 4.32M |

The main win is four-thread Stage 4: miss-fallback is about 3.2x over global, 3.4x over sharded, and 3.7x over the old fallback mirror design.

### Cache-on Stage 5/6

| threads | stage | global | sharded | miss-fallback | thread_local_only |
|---:|---|---:|---:|---:|---:|
| 4 | Stage 5 record_write | 0.42M | 0.34M | 0.38M | 0.37M |
| 4 | Stage 6 notify | 0.22M | 0.31M | 0.34M | 0.34M |

Stage 4 improves clearly, but Stage 5/6 do not move by the same amount. This suggests the remaining producer hot path is now dominated more by writer/ring/notify-side work than by tracking backend alone.

## Interpretation

The lighter miss-fallback design is useful as a tracking-backend experiment:

- It confirms that the old per-thread fallback was expensive because same-thread free still touched the shared fallback shard.
- It makes Stage 4 scale better under four threads.
- It does not by itself solve the full Stage 5/6 producer path, so the next structural target should be writer/ring mutex or per-thread producer/ring context.

Caveat: this is still an experimental fallback design. Same-thread local hits intentionally skip ownership cleanup, so the ownership map is a miss-fallback structure rather than a fully cleaned live-allocation oracle. Before treating it as production semantics, the cross-thread ownership/reuse story needs a stricter design.
