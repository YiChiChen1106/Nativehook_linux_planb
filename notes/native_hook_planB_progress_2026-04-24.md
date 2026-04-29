# native_hook Plan B 进展记录

日期：2026-04-24

## 1. 本次目标

本次工作的目标是按方案 B 推进，先在 Linux 服务器上做出一个“半真实 native_hook”最小版本，验证：

- `consumer + producer + Unix socket + fd 传递 + shared memory + eventfd`
  这条链路能不能先跑起来
- 后续能不能把 fake producer 换成真实的 `LD_PRELOAD malloc/free` hook

本次不追求完整复刻 OpenHarmony 原版 `native_hook`，重点是先把关键链路打通，并逐步往 upstream 的结构和协议靠拢。

## 2. 已完成的事情

## 2.1 已完成本地工程骨架搭建

已在本地工作区新增：

- [linux_native_hook_v1](F:\codex_workspace\native_hook\linux_native_hook_v1)

当前工程内已经有：

- `common/`
- `consumer/`
- `producer_fake/`
- `producer_hook/`
- `tests/`
- `scripts/`

顶层构建文件：

- [CMakeLists.txt](F:\codex_workspace\native_hook\linux_native_hook_v1\CMakeLists.txt)

## 2.2 已完成最小链路版本

已实现最小版本的：

- Unix socket 控制面
- fd 传递
- shared memory 数据面
- `eventfd` 通知
- consumer 侧统计
- fake producer 写入

关键文件：

- [common/socket_fd.cpp](F:\codex_workspace\native_hook\linux_native_hook_v1\common\socket_fd.cpp)
- [consumer/server_main.cpp](F:\codex_workspace\native_hook\linux_native_hook_v1\consumer\server_main.cpp)
- [consumer/control_server.cpp](F:\codex_workspace\native_hook\linux_native_hook_v1\consumer\control_server.cpp)
- [consumer/shm_consumer.cpp](F:\codex_workspace\native_hook\linux_native_hook_v1\consumer\shm_consumer.cpp)
- [producer_fake/fake_writer.cpp](F:\codex_workspace\native_hook\linux_native_hook_v1\producer_fake\fake_writer.cpp)

## 2.3 已在服务器上验证 fake producer 链路

服务器路径：

- `/mnt/hdd/users/cychi/research/native_hook_planB_v1/linux_native_hook_v1`

已在服务器上成功验证：

- consumer 能启动
- fake producer 能连上 consumer
- fd 传递成功
- producer 能写共享内存
- `eventfd` 通知成功
- consumer 能稳定读到 record 并输出 batch 统计

这说明方案 B 的最小数据链路已经不是设计阶段，而是实际跑通了。

## 2.4 已实现真实 `LD_PRELOAD` hook producer

已实现：

- `malloc`
- `free`
- `calloc`
- `realloc`

的 Linux `LD_PRELOAD` 入口。

关键文件：

- [producer_hook/hook_preload.cpp](F:\codex_workspace\native_hook\linux_native_hook_v1\producer_hook\hook_preload.cpp)
- [producer_hook/hook_guard.cpp](F:\codex_workspace\native_hook\linux_native_hook_v1\producer_hook\hook_guard.cpp)
- [producer_hook/hook_writer.cpp](F:\codex_workspace\native_hook\linux_native_hook_v1\producer_hook\hook_writer.cpp)

## 2.5 已解决 preload 退出阶段崩溃

此前 `LD_PRELOAD` 版本运行 workload 时，退出码曾是：

- `RC=139`

属于 preload 场景常见的退出阶段崩溃。

已经通过以下方式修复：

- 将 `HookWriter` 单例改成常驻对象
- 避免在 preload 析构阶段做不稳定的资源回收

修复后，服务器上复测结果为：

- `RC=0`

说明 preload 版本已经能稳定跑完最小 workload。

## 2.6 已在服务器上验证真实 hook 路径

已经验证：

- `LD_PRELOAD=./build/hook_preload.so`
- `./build/perf_test_data_linux`

这条路径能正常运行，并且 consumer 确实能收到真实 `malloc/free` 记录。

也就是说，现在方案 B 已经不是 fake 数据验证，而是已经接上了真实 workload。

## 3. 本次向 upstream 靠拢的改动

本次没有一比一照抄源码，但已经把几个“低工作量、能明显更像 upstream”的地方对齐了。

## 3.1 flush 默认阈值对齐到 upstream

upstream `stack_writer.cpp` 中：

- `FLUSH_FLAG = 20`

现在 V1 默认也改成了：

- `flush_threshold = 20`

且 consumer 日志里已经能看到 batch 大小逐步收敛到 `20` 左右。

## 3.2 握手顺序更接近 upstream

现在 producer 连接 consumer 后，会：

1. 先发送 `pid`
2. consumer 收到 pid 后，再创建并发送资源

这更接近 upstream `HookSocketClient -> HookService` 的握手顺序。

## 3.3 shared memory 命名风格对齐

现在 shared memory 名称已改成：

- `/hooknativesmb_<pid>:0`

而不是之前的自定义名称。

这与 upstream 的：

- `hooknativesmb_<pid>:<index>`

风格一致。

## 3.4 config 结构继续往 `ClientConfig` 靠

当前 consumer 下发的 config 已包含：

- `ring_capacity`
- `flush_threshold`
- `sample_interval`
- `filter_size`
- `max_stack_depth`
- `clock_id`
- `is_blocked`

其中：

- `sample_interval`
- `filter_size`
- `max_stack_depth`
- `is_blocked`

目前主要是为了让协议更贴近 upstream，逻辑还没真正实现。

## 3.5 record 结构更接近 upstream

当前 record 已不再是最早的极简版，而是已经往 upstream `BaseStackRawData / NameData` 靠：

- `timespec ts`
- `addr`
- `size`
- `pid`
- `tid`
- `type`
- `tag_id`
- `name[32]`

## 3.6 已补简化版 `THREAD_NAME_MSG`

参考 upstream `UpdateThreadName()` 的思路，当前 V1 已补一个简化版 thread-name 消息：

- 连接建立后会发 thread name record
- consumer 侧统计里已经能看到：
  - `thread_name=1`

## 4. 当前服务器上已验证到的结果

## 4.1 基础链路验证通过

当前可以确认：

- fake producer 链路通
- 真实 preload producer 链路通
- consumer 能稳定收到真实 `malloc/free` 事件

## 4.2 已记录的一组 baseline / with-hook 数字

曾跑到的一组最小对照：

- baseline
  - `threads=1 duration=1 alloc_size=32 total_iterations=17176799`
- 早期 with-hook
  - `threads=1 duration=1 alloc_size=32 total_iterations=70967`

这组结果说明：

- 当前最初版 v1 的开销极大
- 说明“全量记录 + 全局串行写 + 高频 flush”这条路径非常重

后续在继续向 upstream 协议贴近后，又跑到一组较新的最小验证：

- with-hook
  - `threads=1 duration=1 alloc_size=32 total_iterations=347426`

这说明当前版本虽然仍然很重，但至少在不断修正和稳定。

## 4.3 当前结论

目前可以明确：

- 方案 B 的链路已经做出来了
- 真实 `malloc/free` 已经能进入 consumer
- 当前版本的 overhead 仍然非常大
- 当前阶段更像“链路打通版 + 初步贴近 upstream 协议版”
- 还远不是“轻量化版本”

## 5. 当前版本和真实 native_hook 的关系

当前 V1 最接近 upstream 的部分：

- 控制面 + 数据面分离
- Unix socket 传 fd
- shared memory 传 record
- `eventfd` 做通知
- flush 触发逻辑
- pid 握手
- shared memory 命名
- 基础 config 结构
- thread name 消息

当前 V1 仍然明显不同的部分：

- hook 接入方式仍然是 Linux `LD_PRELOAD`
- 不是 OH/musl 的 `musl_malloc_dispatch`
- 还没有真正的 sample/filter 逻辑
- 还没有 stack unwind
- 还没有 `AddressHandler`
- 还没有 `memtrace/restrace/tag`
- 还没有完整后台处理链

所以当前 V1 的定位仍然是：

> 一个保留关键数据链路、并逐步向 upstream 协议和结构靠拢的 Linux 近似验证框架。

## 6. 当前未完成的事情

下面这些还没做：

- `sample_interval` 真逻辑
- `filter_size` 真逻辑
- `is_blocked` 真逻辑
- stack capture
- `mmap/munmap`
- `memtrace/restrace/tag`
- 更系统的 benchmark 脚本化输出

## 7. 下周一建议继续做什么

建议下周一优先继续做两件事：

### 7.1 先做 `sample_interval`

原因：

- 它已经在 config 里了
- 它是最直接影响 record 密度和 overhead 的项之一
- 工作量相对可控

### 7.2 再做 `filter_size`

原因：

- 它同样已经在 config 里
- 比 stack 小很多
- 对减开销判断也很关键

不建议下周一一上来就做：

- stack unwind
- mmap/munmap
- tag / memtrace / restrace

这些都太容易把复杂度拉高。

## 8. 一句话总结

截至 2026-04-24，方案 B 已经从“设计阶段”进入“可运行阶段”：

- 最小链路已在服务器上跑通
- 真实 `LD_PRELOAD malloc/free` 已经能进 consumer
- 一批低成本 upstream 对齐项已经完成
- 当前剩余主要问题不再是“能不能跑”，而是“怎样把开销压下来”
