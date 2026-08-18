# Tool Compare Summary

| case | tool | expected_blocks | expected_bytes | detected_blocks | detected_bytes | matches_expected | has_stack | summary | notes | result_dir |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| definite_leak | valgrind | 20 | 1280 | 20 | 1280 | yes | no | definitely lost: 1280 bytes in 20 blocks |  | benchmark-root/results/first_light/valgrind/definite_leak |
| definite_leak | lsan | 20 | 1280 | 20 | 1280 | yes | yes | LSan leaked: 1280 bytes in 20 allocations |  | benchmark-root/results/first_light/lsan/definite_leak |
| definite_leak | heaptrack | 20 | 1280 | 20 | 1311 | no | yes | benchmark heaptrack leaks: 1311 bytes in 20 calls | heaptrack_print_generated; ignored non-benchmark leak entries: 5 entries/10379 bytes | benchmark-root/results/first_light/heaptrack/definite_leak |
| definite_leak | bcc_memleak | 20 | 1280 | 1 | 4096 | no | yes | memleak outstanding: 4096 bytes in 1 allocations |  | benchmark-root/results/first_light/bcc_memleak/definite_leak |
| delayed_free | valgrind | 0 | 0 | 0 | 0 | yes | no | valgrind reports no possible leaks |  | benchmark-root/results/first_light/valgrind/delayed_free |
| delayed_free | lsan | 0 | 0 | 0 | 0 | yes | no | LSan emitted no leak report |  | benchmark-root/results/first_light/lsan/delayed_free |
| delayed_free | heaptrack | 0 | 0 | 0 | 0 | yes | yes | no benchmark leak stack in heaptrack print | heaptrack_print_generated; ignored non-benchmark leak entries: 4 entries/9068 bytes | benchmark-root/results/first_light/heaptrack/delayed_free |
| delayed_free | bcc_memleak | 0 | 0 | 0 | 0 | yes | yes | no benchmark leak stack in memleak snapshot | ignored non-benchmark stacks: 2 stacks/8393008 bytes | benchmark-root/results/first_light/bcc_memleak/delayed_free |
| growth_leak | valgrind | 20 | 1280 | 20 | 1280 | yes | no | definitely lost: 1280 bytes in 20 blocks |  | benchmark-root/results/first_light/valgrind/growth_leak |
| growth_leak | lsan | 20 | 1280 | 20 | 1280 | yes | yes | LSan leaked: 1280 bytes in 20 allocations |  | benchmark-root/results/first_light/lsan/growth_leak |
| growth_leak | heaptrack | 20 | 1280 | 20 | 1311 | no | yes | benchmark heaptrack leaks: 1311 bytes in 20 calls | heaptrack_print_generated; ignored non-benchmark leak entries: 5 entries/10379 bytes | benchmark-root/results/first_light/heaptrack/growth_leak |
| growth_leak | bcc_memleak | 20 | 1280 | 1 | 4096 | no | yes | memleak outstanding: 4096 bytes in 1 allocations |  | benchmark-root/results/first_light/bcc_memleak/growth_leak |
| high_freq_no_leak | valgrind | 0 | 0 | 0 | 0 | yes | no | valgrind reports no possible leaks |  | benchmark-root/results/first_light/valgrind/high_freq_no_leak |
| high_freq_no_leak | lsan | 0 | 0 | 0 | 0 | yes | no | LSan emitted no leak report |  | benchmark-root/results/first_light/lsan/high_freq_no_leak |
| high_freq_no_leak | heaptrack | 0 | 0 | 0 | 0 | yes | yes | no benchmark leak stack in heaptrack print | heaptrack_print_generated; ignored non-benchmark leak entries: 4 entries/9068 bytes | benchmark-root/results/first_light/heaptrack/high_freq_no_leak |
| high_freq_no_leak | bcc_memleak | 0 | 0 | 0 | 0 | yes | yes | no benchmark leak stack in memleak snapshot | ignored non-benchmark stacks: 2 stacks/8393008 bytes | benchmark-root/results/first_light/bcc_memleak/high_freq_no_leak |
| mixed | valgrind | 4 | 322 | 4 | 322 | yes | no | definitely lost: 322 bytes in 4 blocks |  | benchmark-root/results/first_light/valgrind/mixed |
| mixed | lsan | 4 | 322 | 4 | 322 | yes | yes | LSan leaked: 322 bytes in 4 allocations |  | benchmark-root/results/first_light/lsan/mixed |
| mixed | heaptrack | 4 | 322 | 4 | 322 | yes | yes | benchmark heaptrack leaks: 322 bytes in 4 calls | heaptrack_print_generated; ignored non-benchmark leak entries: 5 entries/9390 bytes | benchmark-root/results/first_light/heaptrack/mixed |
| mixed | bcc_memleak | 4 | 322 | 1 | 134217728 | no | yes | memleak outstanding: 134217728 bytes in 1 allocations |  | benchmark-root/results/first_light/bcc_memleak/mixed |
| no_leak | valgrind | 0 | 0 | 0 | 0 | yes | no | valgrind reports no possible leaks |  | benchmark-root/results/first_light/valgrind/no_leak |
| no_leak | lsan | 0 | 0 | 0 | 0 | yes | no | LSan emitted no leak report |  | benchmark-root/results/first_light/lsan/no_leak |
| no_leak | heaptrack | 0 | 0 | 0 | 0 | yes | yes | no benchmark leak stack in heaptrack print | heaptrack_print_generated; ignored non-benchmark leak entries: 4 entries/9068 bytes | benchmark-root/results/first_light/heaptrack/no_leak |
| no_leak | bcc_memleak | 0 | 0 | 0 | 0 | yes | yes | no benchmark leak stack in memleak snapshot | ignored non-benchmark stacks: 1 stacks/4096 bytes | benchmark-root/results/first_light/bcc_memleak/no_leak |
