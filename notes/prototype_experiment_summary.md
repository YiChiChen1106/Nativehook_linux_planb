# 原型实验汇总 — OH 代码已移植优化验证

> pink 服务器采集，5 次重复 avg ± stddev，1M mixed3 迭代，ft=20，cap=4096

---

## 1. batch64（record-fill-before-lock + 批量发布）

**OH 改动**：五个 hook 函数（malloc/calloc/realloc/aligned_alloc/free）先填 record 再拿锁，per-thread buffer 攒 64 条批量写入。

| config | 1T | 4T | 8T | 16T |
|---|---|---|---|---|
| no-batch（per-record） | 0.364 ± 0.010 | 0.988 ± 0.177 | 1.424 ± 0.074 | 1.426 ± 0.036 |
| **batch64** | **0.267 ± 0.008** | **0.395 ± 0.084** | **0.580 ± 0.029** | **0.590 ± 0.017** |
| 改善 | 27% | 60% | 59% | 59% |

batch64 把拿锁频率压了 64 倍，HookWriter 全局 mutex 临界区足够大（tracking + ring write + notify），减少锁频率效果显著，多线程尤其受益。

---

## 2. TID 取余分片（batch64 + sharded auto vs batch64 alone）

**OH 改动**：`hook_client.cpp` 新增 `GetShardIndex()`，thread_local 缓存 TID 取余，替换原有地址取余分片。

| config | 1T | 4T | 8T | 16T |
|---|---|---|---|---|
| batch64 only | 0.279 ± 0.030 | 0.465 ± 0.013 | 0.591 ± 0.039 | 0.599 ± 0.021 |
| **batch64 + shard auto** | **0.262 ± 0.001** | **0.429 ± 0.012** | **0.575 ± 0.021** | **0.561 ± 0.006** |
| 改善 | 6% | 8% | 3% | 6% |

TID 分片将每个线程固定到自己的分片，消除 `ShareMemoryBlock` 内部锁竞争。batch64 已经大幅减少锁频率，分片在此基础上进一步消除了残留竞争。

---

## 3. 分片数：auto vs fixed（`GetShardCount()` 自动检测）

**OH 改动**：`GetShardCount()` 当 `g_sharedMemCount <= 1` 时自动检测 CPU 核数（`sysconf(_SC_NPROCESSORS_ONLN)`），clamped 2-16。

| config | 4T | 16T |
|---|---|---|
| shard=4 | 0.401 ± 0.062 | 0.566 ± 0.004 |
| shard=16 | 0.403 ± 0.066 | 0.582 ± 0.018 |
| **shard=auto** | **0.433 ± 0.004** | **0.555 ± 0.010** |

auto 模式和固定最优值在同一水平。32/64 片不可用（每片容量太小，batch64 的 64 条 batch 填满分片导致频繁溢出）。

---

## 4. 偏斜负载（鲁棒性验证）

batch64 + shard auto，16T。`--skew P` 表示 thread#0 承担 P% 总迭代。

| 偏斜 | 16T 耗时 |
|---|---|
| 均衡（skew=0） | 0.565 ± 0.013 |
| 50% on thread#0 | 0.551 ± 0.019 |
| 80% on thread#0 | 0.572 ± 0.017 |

TID 分片在偏斜负载下稳定，不会因单个线程过载崩溃。溢出机制在分片写满时兜底不丢记录。

---

## 5. 完整逻辑链

```
OH 已有：batch64（record-fill-before-lock + 批量发布）
  → 拿锁频率 64x ↓，1T 提速 27%，多线程提速 ~60%
  → 已移植到 cyc_nativehook / yt_nativehook
       ↓
原型进一步：TID 取余分片（GetShardIndex）
  → 地址分片 → 线程分片，消除 ShareMemoryBlock 锁竞争
  → batch64 基础上再提 3-8%
  → 已移植
       ↓
原型进一步：auto 分片数（GetShardCount）
  → 无需手动配置，自动匹配 CPU 核数
  → 已移植
       ↓
原型进一步：偏斜负载 + 溢出
  → 负载不均时稳定，不丢记录
  → 溢出在原型验证，OH 共享内存池架构天然支持
       ↓
  结论：LD_PRELOAD + batch64 + TID 分片是最优 producer 方案
```

---

## 6. OH 代码改动清单

| 改动 | 文件:行 | 状态 |
|---|---|---|
| batch64（`FlushStage6Batch`） | `hook_client.cpp` | ✅ 已移植 |
| TID 取余分片（`GetShardIndex`） | `hook_client.cpp:214` | ✅ 已移植 |
| auto 分片数（`GetShardCount`） | `hook_client.cpp:214` | ✅ 已移植 |
| 架构无需改（每 StackWriter 独立计数器） | `stack_writer.cpp` | — |
| SPSC relaxed store | `stack_writer.cpp` | 📝 标注，等 aarch64 真机 |

已 push 至 `cyc_nativehook` 和 `yt_nativehook` master。
