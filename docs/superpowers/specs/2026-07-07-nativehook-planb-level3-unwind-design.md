# Nativehook Plan B Level 3 Unwind Design

## Goal

Upgrade Plan B from address-level alloc/free matching to nativehook-like Level 3 stack aggregation. The first heavy version should collect a real multi-frame call stack on the producer side, assign a stable `stack_id`, export stack metadata through the consumer, and let the analyzer group outstanding allocations by stack.

## Scope

This design targets Linux Plan B under `linux_native_hook_v1`. It does not try to fully reproduce OpenHarmony nativehook's production unwinder, symbol table pipeline, or every proto field. The intended result is a working local implementation that demonstrates the same core capability:

- capture allocation call stacks in the hook path
- preserve a `stack_id` with each malloc/free event
- emit stack map records from the consumer
- aggregate outstanding bytes and counts by stack in the analyzer

## Architecture

### Producer

The hook producer captures a bounded backtrace for allocation events after the re-entry guard is active. The first version should use `backtrace()` from `execinfo.h`, with a small fixed maximum depth such as 16 frames. The frames are kept as raw program counters in fixed-size arrays to avoid heap allocation in the hook path.

The producer computes a deterministic 64-bit hash over the captured frame PCs and stores the low 32 bits as `stack_id` in `HookRecord`. A zero `stack_id` means no stack was captured. The producer should also cache stack maps by `stack_id` in process memory so the same stack can be exported once per producer process.

Stack capture must be configurable with environment variables:

- `LNHV1_STACK_CAPTURE=0|1` enables or disables the heavy stack path.
- `LNHV1_MAX_STACK_DEPTH=<n>` bounds the collected frame count.

The default should be off unless tests or demos explicitly enable it, because stack unwinding is much more expensive than the current hot path.

### Shared Data

`HookRecord` gains:

- `uint32_t stack_id`
- `uint16_t stack_depth`
- reserved padding for stable alignment

Because stack frames do not fit inside every allocation record, stack maps should be transported as separate metadata records. Add a new event type:

- `HookEventType::kStackMap`

A stack map record stores:

- `stack_id`
- `stack_depth`
- up to a fixed number of raw frame PCs

To keep this first version simple, stack map records can reuse the existing `HookRecord` envelope only if enough space is added for frame PCs. If that would bloat every allocation record too much, introduce a second fixed-size shared-memory record type is out of scope for the first pass. The practical compromise is to add a small frame array to `HookRecord` for stack-map records and keep it zeroed for normal malloc/free records.

### Consumer Output

Verbose output should remain backward-compatible:

```text
VERBOSE,type,tid,addr,size,pid,tv_sec,tv_nsec,stack_id,callsite
```

For stack maps, the consumer emits a separate line:

```text
STACKMAP,stack_id,depth,pc0,pc1,...
```

`callsite` can initially be the top non-hook PC as a hex string. Symbol names are optional in the first pass. This keeps the producer simple and avoids `backtrace_symbols()`, which may allocate.

### Analyzer

The analyzer should parse `STACKMAP` lines and attach stack frame lists to the JSON report. Existing allocation matching remains address-based. Statistics grouping should prefer `stack_id`; the markdown report should show top stack groups, outstanding bytes, outstanding count, and the stack PCs for each group when available.

The analyzer should continue to accept legacy verbose logs without stack fields.

## Data Flow

1. Application calls `malloc`.
2. Plan B hook records the allocation.
3. When stack capture is enabled, producer collects raw PCs and computes `stack_id`.
4. Producer writes one `STACKMAP` metadata record for a new stack, then writes malloc/free records carrying `stack_id`.
5. Consumer drains shared memory and prints `STACKMAP` plus extended `VERBOSE` lines.
6. Analyzer matches alloc/free by address and groups remaining outstanding allocations by `stack_id`.

## Error Handling

If unwinding fails, the producer writes the allocation with `stack_id=0`. If the stack cache is full or a stack map cannot be written because the ring is full, the allocation record should still be written. The analyzer should tolerate missing stack maps and report the group as `stack:<id>` with no frames.

## Testing

Add unit tests for analyzer parsing of `STACKMAP` lines and stack-group reports. Add C++ tests or an integration demo that enables `LNHV1_STACK_CAPTURE=1`, runs a small program with two allocation call paths, captures verbose output, and verifies that at least two distinct nonzero `stack_id` values are present.

Existing analyzer tests and current C++ benchmark/test scripts should continue to pass.

## Risks

`backtrace()` may allocate or take locks on some libc configurations. The hook re-entry guard reduces recursion risk, but stack capture should remain opt-in. Adding frame arrays to `HookRecord` increases shared-memory bandwidth; this should be measured separately from the optimized hot-path benchmark. Symbolization should stay outside the hook path.

## Implementation Order

1. Extend record schema and verbose parser/output format.
2. Add opt-in stack capture helper with fixed-size PC collection and hash.
3. Emit stack map metadata records before first use of a stack.
4. Extend analyzer to parse and render stack maps.
5. Add tests and a small demo log.
6. Sync the updated F drive repo to the Linux working copy after verification.
