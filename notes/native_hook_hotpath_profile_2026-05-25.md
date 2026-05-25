# native_hook hot-path 直接打点实验

## 实验目的

leader 问“每个 stage 大概占多少时间”时，之前的回答是用吞吐反推 `ns/op`。这次补一个代码内直接打点口径，用 `LNHV1_HOTPATH_PROFILE=1` 在 producer hot path 内累计 segment cycle，并在进程退出时输出 CSV。

注意：这个 profile 模式会明显降低吞吐，所以它主要用于回答“hook 内部各段谁更重”，不和普通 ablation 的吞吐数字直接混用。

正式结果：

- 服务器：`pink`
- 目录：`/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`
- CSV：`results/hook_hotpath_profile_server_2026-05-25.csv`
- 本地副本：`F:\codex_workspace\native_hook\linux_native_hook_v1\results\hook_hotpath_profile_server_2026-05-25.csv`

## Stage 累计耗时口径

这里的 `ns/op` 是 profile 模式下，用高层 segment 反推的一次 malloc/free pair 内部耗时：

- Stage 1/2：`hook_entry + guard`
- Stage 3..6：`hook_entry + guard + record_alloc_total + record_free_total`

### 1 线程

| Stage | 含义 | 直接打点 ns/op | 相邻新增 |
|---:|---|---:|---:|
| 1 | hook_entry | 45.59 | 45.59 |
| 2 | guard | 92.97 | 47.39 |
| 3 | mutex | 369.71 | 276.74 |
| 4 | tracking | 1278.67 | 908.96 |
| 5 | record_write | 4615.36 | 3336.69 |
| 6 | notify | 4627.51 | 12.14 |

### 4 线程

| Stage | 含义 | 直接打点 ns/op | 相邻新增 |
|---:|---|---:|---:|
| 1 | hook_entry | 48.10 | 48.10 |
| 2 | guard | 98.18 | 50.08 |
| 3 | mutex | 3289.38 | 3191.20 |
| 4 | tracking | 11490.36 | 8200.98 |
| 5 | record_write | 62189.91 | 50699.56 |
| 6 | notify | 64328.76 | 2138.85 |

## 关键 segment

### mutex

- 1 线程 Stage 3：
  - `writer_mutex_wait`: 23.99 ns/call
  - `writer_mutex_hold`: 21.67 ns/call
- 4 线程 Stage 3：
  - `writer_mutex_wait`: 837.63 ns/call
  - `writer_mutex_hold`: 26.20 ns/call

解释：单线程下 mutex 本身不算特别重；4 线程下主要变成等待锁，说明 global mutex 的问题主要是竞争。

### tracking

- 1 线程 Stage 4：
  - `tracking_insert`: 281.25 ns/call
  - `tracking_lookup`: 82.88 ns/call
  - `tracking_erase`: 250.65 ns/call
- 4 线程 Stage 4：
  - `writer_mutex_wait`: 3501.04 ns/call
  - `tracking_insert`: 457.30 ns/call
  - `tracking_lookup`: 277.35 ns/call
  - `tracking_erase`: 312.22 ns/call

解释：tracking 不是单点成本，而是 set 操作加 global mutex 竞争一起放大。4 线程里锁等待已经比单个 set 操作更显眼。

### record_write / metadata

- 1 线程 Stage 5：
  - `metadata_pid`: 608.24 ns/call
  - `metadata_tid`: 602.70 ns/call
  - `metadata_clock`: 37.47 ns/call
  - `ring_index_check`: 32.66 ns/call
  - `shm_record_copy`: 24.49 ns/call
  - `atomic_index_update`: 25.71 ns/call
- 4 线程 Stage 5：
  - `writer_mutex_wait`: 22941.36 ns/call
  - `metadata_pid`: 1304.33 ns/call
  - `metadata_tid`: 1288.41 ns/call
  - `tracking_insert`: 794.33 ns/call
  - `tracking_erase`: 621.60 ns/call

解释：Stage 5 的单线程重心仍然是 `getpid/gettid` 这类 metadata syscall；4 线程下 writer mutex wait 会进一步放大。

### notify

- 1 线程 Stage 6：
  - `notify`: 1509.29 ns/call
  - 但 notify 不是每条 record 都调用，count 约等于 flush 次数。
- 4 线程 Stage 6：
  - `notify`: 2945.41 ns/call

解释：eventfd notify 单次不便宜，但因为它按 batch 触发，所以从 Stage 5 到 Stage 6 的累计新增没有 Stage 4/5 那么大。

## 组会回答口径

可以这样讲：

> 我补了一个代码内直接打点版本。这个版本会扰动 hot path，所以我不把它当最终吞吐结果，而是用来解释各段内部耗时。结果和之前 ablation 的方向一致：多线程下 global mutex 主要体现在 wait time；tracking 的 insert/lookup/erase 都有成本，且会被锁竞争放大；Stage 5 里面 pid/tid syscall 明显重，ring index、shm copy 和 atomic index 单次成本反而不是最大项。notify 单次有成本，但因为 batch 触发，不是最大新增来源。

## 后续整体改动测量

下一步可以单独做“整体优化收益”实验，不再看内部 segment，而是看完整 Stage 5/6 吞吐：

- `global + cache off`
- `global + pid/tid cache on`
- `sharded + pid/tid cache on`
- `thread_local_fallback + pid/tid cache on`
- `thread_local_only + pid/tid cache on` 只作为 same-thread 上界

这个实验回答的是“之前组会提到的改动叠起来，对整体能提升多少”，和本次 profile 的问题分开。
