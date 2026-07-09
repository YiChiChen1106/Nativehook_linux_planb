# linux_native_hook_v1

Plan B v1 的目标是先打通 Linux 侧最小闭环：

- consumer 负责：
  - Unix socket 控制面
  - fd 传递
  - shared memory 创建
  - `eventfd` 通知消费
  - 统计输出
- producer_fake 负责：
  - 连接 consumer
  - 接收 shared memory fd 和 `eventfd`
  - 写入最小 `malloc/free` record
  - 触发 `eventfd`

当前默认路径覆盖：

- 单 consumer
- 单 producer
- 最小 `malloc/free` record
- 单机 Linux 环境
- opt-in `LD_PRELOAD` malloc/free hook
- opt-in stack capture

当前版本暂不覆盖：

- sample/filter
- `mmap/munmap`
- 多进程

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run fake demo

先启动 consumer：

```bash
./build/consumer
```

再启动 fake producer：

```bash
./build/producer_fake
```

## Nativehook-like analyzer

`tools/nativehook_like_analyzer.py` 把 consumer `--verbose` 输出转换成接近 OH nativehook 语义的离线泄漏报告。

输入格式兼容两种记录：

```text
VERBOSE,type,tid,addr,size,pid,tv_sec,tv_nsec
VERBOSE,type,tid,addr,size,pid,tv_sec,tv_nsec,stack_id,callsite
STACKMAP,stack_id,depth,pc0,pc1,...
```

其中 `type=0` 表示 malloc/apply，`type=1` 表示 free/release。分析器会按地址维护 pending map，free 命中时删除，结束时输出 outstanding allocations，并按 `stack_id -> callsite -> tid` 的优先级聚合统计。`STACKMAP` 记录保存 raw PC 调用栈，analyzer 会把这些 frame 附到对应 `stack_id` 的统计组上。

示例：

```bash
python3 tools/nativehook_like_analyzer.py \
  examples/nativehook_like_verbose.txt \
  --json results/nativehook_like_report.json \
  --markdown results/nativehook_like_report.md
```

Level 3 stack 示例：

```bash
python3 tools/nativehook_like_analyzer.py \
  examples/nativehook_like_stack_verbose.txt \
  --json results/nativehook_like_stack_report.json \
  --markdown results/nativehook_like_stack_report.md
```

启用重版 stack capture：

```bash
LNHV1_STACK_CAPTURE=1 LNHV1_MAX_STACK_DEPTH=16 \
LD_PRELOAD=./hook_preload.so ./your_program
```

当前版本已经覆盖 alloc/free 配对、unmatched free、duplicate alloc、outstanding allocation、`STACKMAP` 解析和按 `stack_id` 聚合。采集侧使用 `backtrace()` 获取 raw PC，默认关闭，适合做 Level 3 能力验证和 benchmark 对比；PC 符号化建议放在离线后处理阶段。
