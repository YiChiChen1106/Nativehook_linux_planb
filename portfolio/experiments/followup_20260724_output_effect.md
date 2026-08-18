# 统一输出效果

| tool | case | repeats | expected_blocks | expected_bytes | detected_blocks | detected_bytes | byte_delta | match_rate | stack_groups | benchmark_stack_groups | benchmark_stack_hit | symbolized | has_stack | unknown_frames | runtime_ms | notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| bcc_memleak | definite_leak | 3 | 4000 | 256000 | 4000.0 | 256000.0 | 0.0 | 100.0% | 4.3 | 1.0 | 100.0% | 100.0% | 100.0% | 1.0 | 9558.3 | coarse benchmark stack attribution; ignored non-benchmark stacks: 3 stacks/16786064 bytes; coarse benchmark stack attribution; ignored non-benchmark stacks: 4 stacks/151003792 bytes |
| bcc_memleak | delayed_free | 3 | 0 | 0 | 0.0 | 0.0 | 0.0 | 100.0% | 4.0 | 0.0 |  |  | 100.0% | 1.0 | 9659.3 | ignored non-benchmark stacks: 4 stacks/151003840 bytes |
| bcc_memleak | growth_leak | 3 | 4000 | 256000 | 4000.0 | 256000.0 | 0.0 | 100.0% | 5.0 | 1.0 | 100.0% | 100.0% | 100.0% | 1.0 | 10060.7 | coarse benchmark stack attribution; ignored non-benchmark stacks: 4 stacks/151004128 bytes |
| bcc_memleak | high_freq_no_leak | 3 | 0 | 0 | 0.0 | 0.0 | 0.0 | 100.0% | 4.3 | 0.0 |  |  | 100.0% | 1.0 | 9560.0 | ignored non-benchmark stacks: 4 stacks/151003792 bytes; ignored non-benchmark stacks: 5 stacks/218112656 bytes |
| bcc_memleak | mixed | 3 | 800 | 64400 | 800.0 | 64400.0 | 0.0 | 100.0% | 5.0 | 1.0 | 100.0% | 100.0% | 100.0% | 1.0 | 9657.7 | coarse benchmark stack attribution; ignored non-benchmark stacks: 4 stacks/151003792 bytes |
| bcc_memleak | no_leak | 3 | 0 | 0 | 0.0 | 0.0 | 0.0 | 100.0% | 4.0 | 0.0 |  |  | 100.0% | 1.0 | 9624.7 | ignored non-benchmark stacks: 4 stacks/151003792 bytes |
| heaptrack | definite_leak | 3 | 4000 | 256000 | 4000.0 | 256000.0 | 0.0 | 100.0% | 6.0 | 1.0 | 100.0% | 100.0% | 100.0% | 0.0 | 1524.7 | heaptrack_print_generated; ignored non-benchmark leak entries: 5 entries/265544 bytes |
| heaptrack | delayed_free | 3 | 0 | 0 | 0.0 | 0.0 | 0.0 | 100.0% | 4.0 | 0.0 |  |  | 100.0% | 0.0 | 1632.7 | heaptrack_print_generated; ignored non-benchmark leak entries: 4 entries/9544 bytes |
| heaptrack | growth_leak | 3 | 4000 | 256000 | 4000.0 | 256000.0 | 0.0 | 100.0% | 6.0 | 1.0 | 100.0% | 100.0% | 100.0% | 0.0 | 1979.0 | heaptrack_print_generated; ignored non-benchmark leak entries: 5 entries/265544 bytes |
| heaptrack | high_freq_no_leak | 3 | 0 | 0 | 0.0 | 0.0 | 0.0 | 100.0% | 4.0 | 0.0 |  |  | 100.0% | 0.0 | 1531.3 | heaptrack_print_generated; ignored non-benchmark leak entries: 4 entries/9544 bytes |
| heaptrack | mixed | 3 | 800 | 64400 | 800.0 | 64400.0 | 0.0 | 100.0% | 7.0 | 1.0 | 100.0% | 100.0% | 100.0% | 0.0 | 1647.0 | heaptrack_print_generated; ignored non-benchmark leak entries: 5 entries/73944 bytes |
| heaptrack | no_leak | 3 | 0 | 0 | 0.0 | 0.0 | 0.0 | 100.0% | 4.0 | 0.0 |  |  | 100.0% | 0.0 | 1531.7 | heaptrack_print_generated; ignored non-benchmark leak entries: 4 entries/9544 bytes |
| lsan | definite_leak | 3 | 4000 | 256000 | 4000.0 | 256000.0 | 0.0 | 100.0% | 1.0 | 1.0 | 100.0% | 100.0% | 100.0% | 0.0 | 1086.0 |  |
| lsan | delayed_free | 3 | 0 | 0 | 0.0 | 0.0 | 0.0 | 100.0% | 0.0 | 0.0 |  |  | 0.0% | 0.0 | 1157.0 |  |
| lsan | growth_leak | 3 | 4000 | 256000 | 4000.0 | 256000.0 | 0.0 | 100.0% | 1.0 | 1.0 | 100.0% | 100.0% | 100.0% | 0.0 | 1587.0 |  |
| lsan | high_freq_no_leak | 3 | 0 | 0 | 0.0 | 0.0 | 0.0 | 100.0% | 0.0 | 0.0 |  |  | 0.0% | 0.0 | 1053.0 |  |
| lsan | mixed | 3 | 800 | 64400 | 800.0 | 64400.0 | 0.0 | 100.0% | 2.0 | 1.0 | 100.0% | 100.0% | 100.0% | 0.0 | 1189.0 |  |
| lsan | no_leak | 3 | 0 | 0 | 0.0 | 0.0 | 0.0 | 100.0% | 0.0 | 0.0 |  |  | 0.0% | 0.0 | 1448.0 |  |
| valgrind | definite_leak | 3 | 4000 | 256000 | 4000.0 | 256000.0 | 0.0 | 100.0% | 1.0 | 1.0 | 100.0% | 100.0% | 100.0% | 0.0 | 2547.0 |  |
| valgrind | delayed_free | 3 | 0 | 0 | 0.0 | 0.0 | 0.0 | 100.0% | 0.0 | 0.0 |  |  | 0.0% | 0.0 | 2665.3 |  |
| valgrind | growth_leak | 3 | 4000 | 256000 | 4000.0 | 256000.0 | 0.0 | 100.0% | 1.0 | 1.0 | 100.0% | 100.0% | 100.0% | 0.0 | 2982.0 |  |
| valgrind | high_freq_no_leak | 3 | 0 | 0 | 0.0 | 0.0 | 0.0 | 100.0% | 0.0 | 0.0 |  |  | 0.0% | 0.0 | 2445.3 |  |
| valgrind | mixed | 3 | 800 | 64400 | 800.0 | 64400.0 | 0.0 | 100.0% | 2.0 | 1.0 | 100.0% | 100.0% | 100.0% | 0.0 | 2605.7 |  |
| valgrind | no_leak | 3 | 0 | 0 | 0.0 | 0.0 | 0.0 | 100.0% | 0.0 | 0.0 |  |  | 0.0% | 0.0 | 3216.7 |  |
