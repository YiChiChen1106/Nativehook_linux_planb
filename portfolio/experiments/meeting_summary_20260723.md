# 7.23 组会实验汇总

实验服务器：Linux test server；Benchmark 参数：2 threads、2000 iterations/thread、size=64、growth rounds=6。

## 检测效果

| 工具 | Precision | Recall | 精确匹配 | Unknown | 说明 |
| --- | ---: | ---: | --- | ---: | --- |
| valgrind | 100.00% | 100.00% | 18/18 | 0 | 对象/字节数匹配 |
| lsan | 100.00% | 100.00% | 18/18 | 0 | 对象/字节数匹配 |
| heaptrack | 100.00% | 100.00% | 18/18 | 0 | 对象/字节数匹配 |
| bcc_memleak | 100.00% | 100.00% | 18/18 | 0 | BCC 含粗粒度栈归属 |

## 性能

| 工具 | workload 平均耗时 | 标准差 | 相对裸 Benchmark | runner 平均耗时 |
| --- | ---: | ---: | ---: | ---: |
| baseline | 0.34 ms | 0.06 ms | 1.00x | 4.40 ms |
| valgrind | 78.91 ms | 1.87 ms | 234.58x | 1498.60 ms |
| lsan | 2.12 ms | 0.29 ms | 6.32x | 50.60 ms |
| heaptrack | 77.89 ms | 9.79 ms | 231.55x | 537.40 ms |
| BCC memleak | 20.84 ms* | 1.44 ms | 61.29x* | 9560.00 ms |

*BCC workload 时间扣除了 7500 ms 采样等待；runner 时间包含完整观察窗口。

## BCC growth 对比

| 线程数 | 预期 | 最终观测 | 精确匹配 |
| ---: | --- | --- | --- |
| 1 | 2000 blocks / 128000 bytes | 1332 blocks / 85248 bytes | 0/3 |
| 2 | 4000 blocks / 256000 bytes | 2664 blocks / 170496 bytes | 0/3 |

前 3 个快照可以观察到增长；后续快照稳定停在单线程 1332、双线程 2664。该问题与线程数呈线性关系，仍需作为 BCC 长时间分阶段追踪限制记录。

## BCC 栈定位

| 泄漏路径 | 精确匹配 | 结果 |
| --- | --- | --- |
| DefiniteLeakPath | 3/3 | 4000 blocks / 256000 bytes |
| GrowthLeakPath | 3/3 | 4000 blocks / 256000 bytes |
| MixedLeakPath | 3/3 | 800 blocks / 64400 bytes |

启用 `-fno-omit-frame-pointer` 后，BCC 原始栈中可以看到 `DefiniteLeakPath`、`GrowthLeakPath` 和 `MixedLeakPath`，细粒度路径定位得到验证。

## 口径说明

- Precision/recall 按 case、对象数量和字节数匹配计算。
- BCC 的第一轮 100% 结果包含粗粒度归属，细粒度调用栈结论使用 frame-pointer 实验单独验证。
- BCC runner 时间包含采样窗口；BCC 的 workload 时间从 `benchmark.json` 中扣除 7500 ms 观察等待后再比较。
- heaptrack 的字节单位已按十进制 SI 单位解析。
