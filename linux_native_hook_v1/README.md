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

当前版本只覆盖：

- 单 consumer
- 单 producer
- 最小 `malloc/free` record
- 单机 Linux 环境

当前版本暂不覆盖：

- `LD_PRELOAD` hook
- stack capture
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
