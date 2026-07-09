# nativehook-like leak analysis

## Summary

- Alloc events: 3
- Free events: 2
- Matched frees: 1
- Unmatched frees: 1
- Duplicate allocs: 0
- Outstanding allocations: 2
- Outstanding bytes: 160

## Statistics

| group | callsite | apply_count | release_count | outstanding_size | outstanding_count |
|---|---:|---:|---:|---:|---:|
| stack:11 | leak_case_A | 2 | 1 | 128 | 1 |
| stack:12 | leak_case_B | 1 | 0 | 32 | 1 |

## Outstanding Allocations

| addr | size | pid | tid | stack_id | callsite | age_ns |
|---|---:|---:|---:|---:|---|---:|
| 0x4000 | 128 | 7 | 101 | 11 | leak_case_A | 0 |
| 0x2000 | 32 | 7 | 102 | 12 | leak_case_B | 3000000300 |

## Unmatched Frees

| line | addr | pid | tid | ts_sec | ts_nsec |
|---:|---|---:|---:|---:|---:|
| 5 | 0x3000 | 7 | 104 | 13 | 400 |
