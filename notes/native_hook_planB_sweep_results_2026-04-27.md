# Plan B Sweep Results

日期：2026-04-27

## 1. 本次新增的脚本

- 批跑脚本：
  - [run_hook_sweep.sh](/F:/codex_workspace/native_hook/linux_native_hook_v1/scripts/run_hook_sweep.sh)
- 作用：
  - 自动跑 baseline
  - 自动跑 with-hook
  - 自动拉起 / 停止 consumer
  - 自动解析 consumer 最后一条 `wake=...` 统计
  - 自动输出 CSV

本次服务器上生成的原始结果文件路径：

- `sample_interval_sweep_2026-04-27.csv`
- `filter_size_sweep_2026-04-27.csv`

## 2. `sample_interval` 扫点结果

实验配置：

- `threads=1`
- `size=32`
- `duration=1`
- `filter_size=-1`

baseline：

- `baseline_ops = 17241121.00`

结果表：

| sample_interval | with_hook_ops | overhead_pct | records | flush |
| --- | ---: | ---: | ---: | ---: |
| 1 | 314497.00 | 98.1759 | 629624 | 31476 |
| 10 | 1993077.00 | 88.4400 | 399003 | 19948 |
| 100 | 4269369.00 | 75.2373 | 85460 | 4273 |
| 1000 | 4860177.00 | 71.8106 | 9720 | 486 |
| 10000 | 4906909.00 | 71.5395 | 981 | 49 |
| 100000 | 4940880.00 | 71.3425 | 101 | 5 |

直接观察：

- `sample_interval` 从 `1` 拉到 `100000` 后，record 数量从 `629624` 掉到 `101`，下降非常明显。
- 吞吐量也确实明显恢复，从 `0.31M` 提升到了约 `4.94M`。
- 但吞吐恢复到 `4.9M` 左右后基本进入平台区，没有继续逼近 `17.24M` 的 baseline。

## 3. `filter_size` 扫点结果

实验配置：

- `threads=1`
- `duration=1`
- `sample_interval=1`

### 3.1 `size=32`

baseline：

- `baseline_ops = 17159818.00`

| filter_size | with_hook_ops | overhead_pct | records |
| --- | ---: | ---: | ---: |
| -1 | 325552.00 | 98.1028 | 651761 |
| 32 | 320858.00 | 98.1302 | 642361 |
| 64 | 5269462.00 | 69.2919 | 0 |
| 128 | 4726301.00 | 72.4572 | 0 |
| 256 | 5276411.00 | 69.2514 | 0 |

### 3.2 `size=64`

baseline：

- `baseline_ops = 17303614.00`

| filter_size | with_hook_ops | overhead_pct | records |
| --- | ---: | ---: | ---: |
| -1 | 330631.00 | 98.0892 | 661922 |
| 32 | 314284.00 | 98.1837 | 629200 |
| 64 | 326702.00 | 98.1119 | 654060 |
| 128 | 5281870.00 | 69.4753 | 0 |
| 256 | 5226624.00 | 69.7946 | 0 |

### 3.3 `size=256`

baseline：

- `baseline_ops = 17271186.00`

| filter_size | with_hook_ops | overhead_pct | records |
| --- | ---: | ---: | ---: |
| -1 | 323647.00 | 98.1261 | 647944 |
| 32 | 315789.00 | 98.1716 | 632204 |
| 64 | 325379.00 | 98.1161 | 651405 |
| 128 | 325289.00 | 98.1166 | 651224 |
| 256 | 324834.00 | 98.1192 | 650321 |

直接观察：

- 当 `filter_size` 还没有真正挡住当前 workload 时，吞吐几乎不变，仍然停留在 `0.32M` 左右。
- 一旦 `filter_size` 足够大，能把当前 alloc 全部挡住，record 会直接变成 `0`。
- 即使 record 已经变成 `0`，吞吐也只是恢复到 `4.7M ~ 5.3M`，仍然明显低于 baseline 的 `17M+`。

## 4. 当前最重要的结论

### 4.1 `sample_interval` 和 `filter_size` 已经证明“能控流量”

这一点现在可以比较明确地说：

- 它们不是只存在于 config 里的占位字段
- 它们已经能在 producer 侧真实改变 record 产生量
- 它们也确实能显著提升当前 V1 的吞吐

### 4.2 但当前瓶颈已经不主要是 shared memory / consumer 这条链路

这是本次结果里最关键的判断。

因为现在已经看到：

- `sample_interval=100000` 时，record 只剩 `101`
- 某些 `filter_size` 组合下，record 甚至已经是 `0`

如果主要瓶颈还在“写共享内存、flush、唤醒 consumer”，那在 record 变成极少甚至 `0` 时，吞吐应该继续明显逼近 baseline。

但实际没有。

当前吞吐仍然大致停在：

- `4.7M ~ 5.3M`

这说明当前更值得怀疑的成本，已经转移到了 producer 本地热路径本身，例如：

- `LD_PRELOAD` 拦截本身
- reentry guard
- `HookWriter::Instance()` / 入口包装
- `pthread_mutex_lock/unlock`
- sampled alloc 跟踪集合
- 即使最后不写 record，也仍然执行了一部分 hook 包装逻辑

### 4.3 仅靠“把 record 数量继续压低”，已经不太可能直接逼近 `1%` 目标

至少从当前这版 Plan B V1 看，情况更像是：

- 第一阶段瓶颈是“全量记事件太重”
- 这一阶段已经通过 sample/filter 部分缓解
- 但第二阶段瓶颈是“本地 hook 包装路径本身还是太重”

所以后面如果继续想往 `1%` 以下逼近，重点大概率不该再放在“继续减少 record 数量”这一件事上，而是应该开始拆 producer 本地热路径。

## 5. 建议的下一步实验

### 5.1 做 producer 本地热路径的分层 ablation

建议下一阶段不要优先补 OpenHarmony 依赖，也不要急着补 stack unwind，而是先做几档更细的“空心化”实验：

- 档 A：
  - 只保留 `LD_PRELOAD` 拦截和真实 `malloc/free`
  - 不进 `HookWriter`
- 档 B：
  - 进入 `HookWriter`
  - 只做 reentry guard
  - 不加 mutex，不做 tracking，不写 record
- 档 C：
  - reentry guard + mutex
  - 不做 tracking，不写 record
- 档 D：
  - reentry guard + mutex + tracking
  - 不写 record
- 档 E：
  - 再恢复当前 sample/filter + record 写入

这样可以更快回答：

> 当前剩下的那大约 `70%` 级别开销，到底主要卡在 hook 包装、锁、地址跟踪，还是仍然卡在数据链路后半段。

### 5.2 再补线程扩展实验

在确认单线程热点之后，再挑 2 到 3 个代表配置跑：

- `threads=1,4,16,64`

这样可以判断：

- 当前更像固定成本问题
- 还是会在线程升高后出现新的竞争瓶颈

## 6. 当前建议

从现在的结果看，下一步最值得继续做的是：

1. 给 `hook_preload` / `HookWriter` 增加几档“分层空心化开关”
2. 用同一套 sweep 脚本跑一轮 ablation
3. 再决定是否值得继续往更完整 upstream 方向补 stack、mmap、tag 等内容

如果目标是判断“压到 `1%` 以下有没有希望”，那这一步比继续补功能更关键。
