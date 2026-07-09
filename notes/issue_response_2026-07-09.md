# Issue 回复记录 — 2026.07.09

## Issue 1: FlushStage6Batch 仍使用 addr-based 分片

**问题**：`FlushStage6Batch`（`hook_client.cpp:154`）仍然使用 `reinterpret_cast<uint64_t>(rec.addr) % g_sharedMemCount`，其他 10+ 处调用点已改为 `GetShardIndex()`（TID-based）。导致同一线程的批处理和非批处理记录可能被路由到不同 shard。

**修复**：`FlushStage6Batch` 中的 `addr % g_sharedMemCount` 改为 `GetShardIndex()`。同时补上了 `hook_realloc` 和 `hook_free` 中遗漏的两处。commit `c93231e24`。

---

## Issue 2: GetShardCount 返回的 shard 数与实际 StackWriter 数量不一致

**问题**：`GetShardCount()` 在热路径上 lazy 调用 `sysconf(_SC_NPROCESSORS_ONLN)`，可能返回大于实际 `stackWriterList_.size()` 的值。连接建立时只创建了 1 个 StackWriter（`g_sharedMemCount = 1`），但 `GetShardCount()` 可能返回 8（CPU 核数），导致 `GetShardIndex()` 返回的索引超出 `stackWriterList_` 范围，记录被丢弃。

**修复**：将 auto 检测从 `GetShardCount()` 移至 `HookSocketClient` 初始化 (`hook_socket_client.cpp:87,134`)，在连接建立时就确定分片数并创建对应数量的 StackWriter。`GetShardCount()` 简化为直接返回 `g_sharedMemCount`。commit `5e4a77dda`。

---

## Issue 3: 批处理路径丢失 regs/ip 数据

**问题**：`FlushStage6Batch` 发送时硬编码 `rsize = sizeof(BaseStackRawData)`，而非批处理路径会根据 `fpunwind` 模式动态计算 `realSize`（包含 `rawdata.ip[]` 或 `rawdata.regs`）。consumer 收到不完整的记录，栈回溯可能失败。

**修复**：在 `Stage6RecordBatch` 中新增 `realSizes[]` 数组，每条记录入批时存储对应的 `realSize`，`FlushStage6Batch` 使用存储的值发送。commit `55e01c435`。

---

## Ring Buffer 注释解释

**背景**：`stack_writer.cpp` 中存在 `SHARDED_RING` 相关注释，但无对应的 ring buffer 代码改动。

**解释**：OpenHarmony 的环形缓冲区实现在 `ShareMemoryBlock` 中，属于外部依赖，本项目无法修改。`SHARDED_RING` 注释标记的是"改动点到此为止，下面的架构已经支持"——即 OH 本身通过多个 `StackWriter` + `ShareMemoryBlock` 实例实现了分片，每个 StackWriter 自带独立计数器和内部锁，无需额外修改。原型上的 ring buffer 实验（CAS 替换、lock-free、SPSC relaxed store）均为原型验证，对 OH 代码无直接改动。SPSC 注释已删除（属于未实现的备忘，保留会引起误解）。
