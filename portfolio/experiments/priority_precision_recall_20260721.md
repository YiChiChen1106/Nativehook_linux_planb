# 2026-07-21 precision and recall

Metrics use case-level leak presence. Positive cases are `definite_leak`, `growth_leak`, and `mixed`; negative cases are `no_leak`, `delayed_free`, and `high_freq_no_leak`. Unknown parser results are reported separately.

## Overall metrics

| tool | samples | unknown | TP | FP | FN | TN | precision | recall | positive_cases | negative_cases |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| valgrind | 18 | 0 | 9 | 0 | 0 | 9 | 100.00% | 100.00% | definite_leak;growth_leak;mixed | no_leak;delayed_free;high_freq_no_leak |
| lsan | 18 | 0 | 9 | 0 | 0 | 9 | 100.00% | 100.00% | definite_leak;growth_leak;mixed | no_leak;delayed_free;high_freq_no_leak |
| heaptrack | 18 | 0 | 9 | 0 | 0 | 9 | 100.00% | 100.00% | definite_leak;growth_leak;mixed | no_leak;delayed_free;high_freq_no_leak |
| bcc_memleak | 18 | 0 | 9 | 0 | 0 | 9 | 100.00% | 100.00% | definite_leak;growth_leak;mixed | no_leak;delayed_free;high_freq_no_leak |

## Detail

| tool | case | repeat | truth | detected_blocks | detected_bytes | prediction | outcome | bytes_match |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| valgrind | definite_leak | 1 | positive | 4000 | 256000 | positive | TP | yes |
| valgrind | delayed_free | 1 | negative | 0 | 0 | negative | TN | yes |
| valgrind | growth_leak | 1 | positive | 4000 | 256000 | positive | TP | yes |
| valgrind | high_freq_no_leak | 1 | negative | 0 | 0 | negative | TN | yes |
| valgrind | mixed | 1 | positive | 800 | 64400 | positive | TP | yes |
| valgrind | no_leak | 1 | negative | 0 | 0 | negative | TN | yes |
| valgrind | definite_leak | 2 | positive | 4000 | 256000 | positive | TP | yes |
| valgrind | delayed_free | 2 | negative | 0 | 0 | negative | TN | yes |
| valgrind | growth_leak | 2 | positive | 4000 | 256000 | positive | TP | yes |
| valgrind | high_freq_no_leak | 2 | negative | 0 | 0 | negative | TN | yes |
| valgrind | mixed | 2 | positive | 800 | 64400 | positive | TP | yes |
| valgrind | no_leak | 2 | negative | 0 | 0 | negative | TN | yes |
| valgrind | definite_leak | 3 | positive | 4000 | 256000 | positive | TP | yes |
| valgrind | delayed_free | 3 | negative | 0 | 0 | negative | TN | yes |
| valgrind | growth_leak | 3 | positive | 4000 | 256000 | positive | TP | yes |
| valgrind | high_freq_no_leak | 3 | negative | 0 | 0 | negative | TN | yes |
| valgrind | mixed | 3 | positive | 800 | 64400 | positive | TP | yes |
| valgrind | no_leak | 3 | negative | 0 | 0 | negative | TN | yes |
| lsan | definite_leak | 1 | positive | 4000 | 256000 | positive | TP | yes |
| lsan | delayed_free | 1 | negative | 0 | 0 | negative | TN | yes |
| lsan | growth_leak | 1 | positive | 4000 | 256000 | positive | TP | yes |
| lsan | high_freq_no_leak | 1 | negative | 0 | 0 | negative | TN | yes |
| lsan | mixed | 1 | positive | 800 | 64400 | positive | TP | yes |
| lsan | no_leak | 1 | negative | 0 | 0 | negative | TN | yes |
| lsan | definite_leak | 2 | positive | 4000 | 256000 | positive | TP | yes |
| lsan | delayed_free | 2 | negative | 0 | 0 | negative | TN | yes |
| lsan | growth_leak | 2 | positive | 4000 | 256000 | positive | TP | yes |
| lsan | high_freq_no_leak | 2 | negative | 0 | 0 | negative | TN | yes |
| lsan | mixed | 2 | positive | 800 | 64400 | positive | TP | yes |
| lsan | no_leak | 2 | negative | 0 | 0 | negative | TN | yes |
| lsan | definite_leak | 3 | positive | 4000 | 256000 | positive | TP | yes |
| lsan | delayed_free | 3 | negative | 0 | 0 | negative | TN | yes |
| lsan | growth_leak | 3 | positive | 4000 | 256000 | positive | TP | yes |
| lsan | high_freq_no_leak | 3 | negative | 0 | 0 | negative | TN | yes |
| lsan | mixed | 3 | positive | 800 | 64400 | positive | TP | yes |
| lsan | no_leak | 3 | negative | 0 | 0 | negative | TN | yes |
| heaptrack | definite_leak | 1 | positive | 4000 | 256000 | positive | TP | yes |
| heaptrack | delayed_free | 1 | negative | 0 | 0 | negative | TN | yes |
| heaptrack | growth_leak | 1 | positive | 4000 | 256000 | positive | TP | yes |
| heaptrack | high_freq_no_leak | 1 | negative | 0 | 0 | negative | TN | yes |
| heaptrack | mixed | 1 | positive | 800 | 64400 | positive | TP | yes |
| heaptrack | no_leak | 1 | negative | 0 | 0 | negative | TN | yes |
| heaptrack | definite_leak | 2 | positive | 4000 | 256000 | positive | TP | yes |
| heaptrack | delayed_free | 2 | negative | 0 | 0 | negative | TN | yes |
| heaptrack | growth_leak | 2 | positive | 4000 | 256000 | positive | TP | yes |
| heaptrack | high_freq_no_leak | 2 | negative | 0 | 0 | negative | TN | yes |
| heaptrack | mixed | 2 | positive | 800 | 64400 | positive | TP | yes |
| heaptrack | no_leak | 2 | negative | 0 | 0 | negative | TN | yes |
| heaptrack | definite_leak | 3 | positive | 4000 | 256000 | positive | TP | yes |
| heaptrack | delayed_free | 3 | negative | 0 | 0 | negative | TN | yes |
| heaptrack | growth_leak | 3 | positive | 4000 | 256000 | positive | TP | yes |
| heaptrack | high_freq_no_leak | 3 | negative | 0 | 0 | negative | TN | yes |
| heaptrack | mixed | 3 | positive | 800 | 64400 | positive | TP | yes |
| heaptrack | no_leak | 3 | negative | 0 | 0 | negative | TN | yes |
| bcc_memleak | definite_leak | 1 | positive | 4000 | 256000 | positive | TP | yes |
| bcc_memleak | delayed_free | 1 | negative | 0 | 0 | negative | TN | yes |
| bcc_memleak | growth_leak | 1 | positive | 4000 | 256000 | positive | TP | yes |
| bcc_memleak | high_freq_no_leak | 1 | negative | 0 | 0 | negative | TN | yes |
| bcc_memleak | mixed | 1 | positive | 800 | 64400 | positive | TP | yes |
| bcc_memleak | no_leak | 1 | negative | 0 | 0 | negative | TN | yes |
| bcc_memleak | definite_leak | 2 | positive | 4000 | 256000 | positive | TP | yes |
| bcc_memleak | delayed_free | 2 | negative | 0 | 0 | negative | TN | yes |
| bcc_memleak | growth_leak | 2 | positive | 4000 | 256000 | positive | TP | yes |
| bcc_memleak | high_freq_no_leak | 2 | negative | 0 | 0 | negative | TN | yes |
| bcc_memleak | mixed | 2 | positive | 800 | 64400 | positive | TP | yes |
| bcc_memleak | no_leak | 2 | negative | 0 | 0 | negative | TN | yes |
| bcc_memleak | definite_leak | 3 | positive | 4000 | 256000 | positive | TP | yes |
| bcc_memleak | delayed_free | 3 | negative | 0 | 0 | negative | TN | yes |
| bcc_memleak | growth_leak | 3 | positive | 4000 | 256000 | positive | TP | yes |
| bcc_memleak | high_freq_no_leak | 3 | negative | 0 | 0 | negative | TN | yes |
| bcc_memleak | mixed | 3 | positive | 800 | 64400 | positive | TP | yes |
| bcc_memleak | no_leak | 3 | negative | 0 | 0 | negative | TN | yes |
