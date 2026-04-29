# native_hook Plan B 进展记录

日期：2026-04-27

## 1. 本次完成的改动

本次工作重点是把 Plan B V1 里原来只保留字段、但尚未真正生效的 `sample_interval` 和 `filter_size` 做成可运行逻辑，并在服务器上验证它们是否真的影响 producer 侧的 record 产生。

### 1.1 `sample_interval` 真正生效

- 修改位置：
  - `linux_native_hook_v1/producer_hook/hook_writer.cpp`
  - `linux_native_hook_v1/producer_hook/hook_writer.h`
- 当前实现方式：
  - 在 preload producer 路径上加入按线程计数的简化采样逻辑
  - 当前行为是“约每 N 次 alloc 保留 1 次”
  - 只有通过采样的 alloc 才会写入 shared memory

### 1.2 `filter_size` 真正生效

- 修改位置：
  - `linux_native_hook_v1/producer_hook/hook_writer.cpp`
  - `linux_native_hook_v1/producer_hook/hook_writer.h`
- 当前实现方式：
  - 在 alloc record 构造前先按 size 做阈值过滤
  - 当前使用的是“requested allocation size”进行判断
  - 小于 `filter_size` 的 alloc 直接跳过，不进入 record 写入路径

### 1.3 `free` 与保留下来的 `alloc` 对齐

- 修改位置：
  - `linux_native_hook_v1/producer_hook/hook_writer.cpp`
  - `linux_native_hook_v1/producer_hook/hook_writer.h`
- 当前实现方式：
  - producer 侧记录“哪些 alloc 真正被保留下来”
  - 只有匹配到这些已保留 alloc 地址时，才发送对应的 `free`
- 这样做的原因：
  - 避免 consumer 侧出现大量“看到了 free，但前面没有 alloc”的失配数据
  - 让 sampled / filtered 数据流更接近真实可解释的事件链

### 1.4 consumer 配置入口补齐

- 修改位置：
  - `linux_native_hook_v1/consumer/server_main.cpp`
- 新增参数：
  - `--sample-interval`
  - `--filter-size`

### 1.5 benchmark 脚本补齐

- 修改位置：
  - `linux_native_hook_v1/scripts/run_hook_benchmark.sh`
- 新增环境变量：
  - `LNHV1_SAMPLE_INTERVAL`
  - `LNHV1_FILTER_SIZE`

### 1.6 fake producer 也同步支持简化 sample/filter

- 修改位置：
  - `linux_native_hook_v1/producer_fake/fake_writer.cpp`
  - `linux_native_hook_v1/producer_fake/fake_writer.h`
- 目的：
  - 让 fake producer 路径和 preload producer 路径在配置行为上更一致

## 2. 服务器验证结果

服务器路径：

- repo: `/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`

本次已完成重新同步、重新编译和小规模验证。

### 2.1 编译通过

执行：

```bash
cd /mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1
cmake -S . -B build
cmake --build build -j
```

结果：

- `consumer`
- `hook_preload.so`
- `perf_test_data_linux`
- `producer_fake`

均重新编译成功。

### 2.2 baseline 参考值

执行：

```bash
./build/perf_test_data_linux --threads 1 --duration 1 --size 32
```

结果：

```text
threads=1 duration=1 alloc_size=32 total_iterations=16379661 throughput_ops=16379661.00
```

### 2.3 `sample_interval` 验证

consumer 配置：

- `sample_interval=100000`
- `filter_size=-1`

workload：

```bash
LNHV1_SOCKET_PATH=/tmp/linux_native_hook_v1.sock \
LD_PRELOAD=./build/hook_preload.so \
./build/perf_test_data_linux --threads 1 --duration 1 --size 32
```

workload 结果：

```text
threads=1 duration=1 alloc_size=32 total_iterations=4947762 throughput_ops=4947762.00
```

consumer 日志关键输出：

```text
flush_threshold=20 sample_interval=100000 filter_size=-1 max_stack_depth=0 clock_id=0 is_blocked=0
wake=1 records=21 alloc=10 free=9 thread_name=2 flush=1 dropped=0 avg_batch=21
wake=1 records=41 alloc=20 free=19 thread_name=2 flush=2 dropped=0 avg_batch=20.5
wake=1 records=61 alloc=30 free=29 thread_name=2 flush=3 dropped=0 avg_batch=20.3333
wake=1 records=81 alloc=40 free=39 thread_name=2 flush=4 dropped=0 avg_batch=20.25
wake=1 records=101 alloc=50 free=49 thread_name=2 flush=5 dropped=0 avg_batch=20.2
```

可以说明：

- consumer 端确实收到了 `sample_interval=100000`
- producer 端确实不再全量写 record，而是只留下很少一部分事件
- `flush_threshold=20` 的批量行为仍然保持
- 当前链路没有因为新增采样逻辑而断掉

### 2.4 `filter_size` 验证

consumer 配置：

- `sample_interval=1`
- `filter_size=64`

workload：

```bash
LNHV1_SOCKET_PATH=/tmp/linux_native_hook_v1.sock \
LD_PRELOAD=./build/hook_preload.so \
./build/perf_test_data_linux --threads 1 --duration 1 --size 32
```

workload 结果：

```text
threads=1 duration=1 alloc_size=32 total_iterations=5164860 throughput_ops=5164860.00
```

consumer 日志关键输出：

```text
flush_threshold=20 sample_interval=1 filter_size=64 max_stack_depth=0 clock_id=0 is_blocked=0
client connected, waiting for eventfd notifications
```

这次运行中没有出现任何 `wake=...` 行。

可以说明：

- consumer 端确实收到了 `filter_size=64`
- 当前 workload 主要是 `32B` alloc
- 这些 alloc 在 producer 侧就被过滤掉了，所以没有形成后续 batch/wake
- 说明 `filter_size` 已经真正参与 producer 决策，而不是仅仅停留在 config 字段里

## 3. 当前可以下的判断

### 3.1 现在的 Plan B V1 已经不是“只搭通链路”

它现在已经具备：

- 真正可调的 `sample_interval`
- 真正可调的 `filter_size`
- 与 sampled alloc 对齐的 `free` 行为

也就是说，当前版本已经开始进入“可以试探 producer 热路径减载效果”的阶段，而不再只是验证 socket/fd/shared memory/eventfd 能不能跑。

### 3.2 当前实现仍然是简化版，不应误认为已完全等同 upstream

当前边界仍然存在：

- `sample_interval` 现在是“按 alloc 次数计数”的简化逻辑
- 还不是完整 upstream sampling 实现
- `filter_size` 现在基于 requested size
- 还没有接入更完整的 upstream helper / address 相关路径
- 仍然没有 stack unwind、`mmap/munmap`、`memtrace/restrace/tag`

所以更准确的说法应当是：

> Plan B V1 现在已经具备了“可调 sample/filter 的 Linux 近似 native_hook 链路”，但还不是完整 upstream native_hook。

### 3.3 从本次小实验看，sample/filter 确实有希望显著降低当前 V1 的链路开销

之前这条“全量记事件”的 V1 with-hook 小 workload 吞吐量大约在几十万级到三十多万级。

本次仅做小规模验证时：

- `sample_interval=100000` 时吞吐回升到约 `4.95M`
- `filter_size=64` 且 workload 为 `32B` 时吞吐回升到约 `5.16M`

这还不能直接推出“最终一定能做到 1% 以下”，但至少说明：

- `sample_interval` 和 `filter_size` 这两个开关确实是值得优先推进的
- 它们已经不只是纸面设想，而是能在当前 Plan B 链路里实际改变 record 流量

## 4. 下一个阶段建议

### 4.1 做系统化参数扫点

建议下一步优先补一组更系统的对照，而不是立刻继续扩功能：

- `sample_interval = 1, 10, 100, 1000, 10000, 100000`
- `filter_size = -1, 32, 64, 128, 256`

并固定 workload：

- `threads=1`
- `duration=1` 或 `duration=3`
- `size=32 / 64 / 256`

这样可以先画出“吞吐恢复 vs 采样/过滤强度”的基础趋势。

### 4.2 再决定是否继续把 sample/filter 往 upstream 细节靠

后续如果要继续贴近 upstream，可以再讨论：

- `sample_interval` 是否改成更接近 upstream 的策略
- `filter_size` 是否需要更接近 upstream 的 usable-size / helper 口径

### 4.3 暂时不建议优先做的内容

在 sample/filter 扫点完成前，暂时不建议优先投入：

- stack unwind
- `mmap/munmap`
- `memtrace/restrace/tag`
- 完整 OpenHarmony 依赖补仓

原因是这些方向会让链路更完整，但不一定能最快回答当前最核心的问题：

> 在当前 Plan B 链路里，靠 sample/filter 这类轻量开关，native_hook 风格路径的开销是否有机会继续往 1% 目标逼近。
