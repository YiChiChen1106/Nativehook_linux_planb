# 2026-07-21 no-tool baseline

Workload: `high_freq_no_leak`, 2 threads, 2000 iterations per thread, size 64, post delay 1000 ms, 5 repeats.

## Baseline repeats

| repeat | status | CPU | max RSS (KB) | elapsed (s) | runner (ms) | artifact bytes |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 0 | 0.0% | 3932 | 1.0 | 1010.0 | 1598.0 |
| 2 | 0 | 0.0% | 3944 | 1.0 | 1007.0 | 1598.0 |
| 3 | 0 | 0.0% | 3372 | 1.0 | 1007.0 | 1598.0 |
| 4 | 0 | 0.0% | 3932 | 1.0 | 1008.0 | 1598.0 |
| 5 | 0 | 0.0% | 3944 | 1.0 | 1008.0 | 1598.0 |

## Baseline mean +/- standard deviation

| CPU | max RSS (KB) | elapsed (s) | runner (ms) | artifact bytes |
| ---: | ---: | ---: | ---: | ---: |
| 0.00 +/- 0.00% | 3825 +/- 253 | 1.00 +/- 0.00 | 1008.00 +/- 1.22 | 1598 +/- 0 |

## Instrumentation overhead relative to baseline

Runtime ratio uses runner duration; RSS delta and CPU delta are relative to the no-tool run.

| tool | baseline runner (ms) | tool runner (ms) | runtime ratio | baseline RSS (KB) | tool RSS (KB) | RSS delta (KB) | baseline CPU | tool CPU | CPU delta (pp) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| bcc_memleak | 1008.00 | 9557.80 | 9.48x | 3825 | 155133 | 151308 | 0.00% | 10.00% | 10.00 |
| heaptrack | 1008.00 | 1528.60 | 1.52x | 3825 | 70206 | 66381 | 0.00% | 23.00% | 23.00 |
| lsan | 1008.00 | 1053.80 | 1.05x | 3825 | 46940 | 43115 | 0.00% | 7.40% | 7.40 |
| valgrind | 1008.00 | 2439.00 | 2.42x | 3825 | 84052 | 80227 | 0.00% | 59.00% | 59.00 |
