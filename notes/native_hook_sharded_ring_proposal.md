# Sharded Ring Buffer 提案

基于原型 ablation 实验的 OH `ShareMemoryBlock` 改进方案。

## 动机

### 数据（原型 pink benchmark, 1M mixed3 iters, sub=36 全链）

| 线程数 | 当前 (mutex) | Sharded (16 片) | 改善 |
|---|---|---|---|
| 1T | 0.320s | 0.230s | 28% |
| 4T | 0.555s | 0.345s | **38%** |
| 8T | 1.007s | 0.767s | **24%** |
| 16T | 1.038s | 0.727s | **30%** |

### 根因

原型 sub=34（纯 ring write）的 16T/1T = 3.0x——`ShareMemoryBlock` 内部锁是多线程竞争的主要来源。CAS 替代 mutex 在 8T+ 无效（CAS retry 的 cache line bouncing 和 mutex 一样贵）。

真正的解决方案：**per-CPU ring buffer，单写者无锁。**

## 现有架构

```
hook_malloc (hook_client.cpp:574-629)
  │
  ├─ FpUnwind()          // 栈回溯
  ├─ rawdata fill        // pid/tid/size/addr/ts
  ├─ weakClient.lock()   // 连接检查
  ├─ AddAllocAddr()      // 地址追踪
  └─ SendStackWithPayload → ShareMemoryBlock::PutWithPayloadTimeout
       │                      ↑ 所有线程争同一个内部锁
       └─ EventNotifier::Post
```

## 提案架构

```
hook_malloc
  │
  ├─ FpUnwind()
  ├─ rawdata fill
  ├─ weakClient.lock()
  ├─ AddAllocAddr()
  └─ SendStackWithPayload → ShareMemoryBlock::PutWithPayloadTimeout
       │                      ↑ shard_idx = tid % N
       │                      ↑ 每片独立 write_index，无锁
       └─ EventNotifier::Post  ↑ 阈值通知不变
```

### ShareMemoryBlock 改动

```
当前:                                 改后:
┌──────────────────────┐              ┌──────────────────────┐
│ ShmHeader             │              │ ShmHeader             │
│  write_index (global) │              │  num_shards = N       │
│  read_index (global)  │              │  Shard[0] { w, r }   │
├──────────────────────┤              │  Shard[1] { w, r }   │
│ HookRecord[4096]      │              │  ...                  │
│  (所有线程共享)        │              │  Shard[N-1] { w, r } │
└──────────────────────┘              ├──────────────────────┤
                                       │ Shard[0] records      │
                                       │ Shard[1] records      │
                                       │ ...                   │
                                       │ Shard[N-1] records    │
                                       └──────────────────────┘
```

每片容量 = 总容量 / N。分片数建议默认 16（匹配典型 CPU 核数），可配置。

### hook_client.cpp 改动点

1. **`hook_malloc`（行 628）**：`SendStackWithPayload` 调用不变，内部路由到对应 shard
2. **`ShareMemoryBlock::PutWithPayloadTimeout`**：新增 `shard_idx` 参数（由上层用 `tid % num_shards` 计算），shard 内写入无锁
3. **TID 缓存**：`thread_local` 缓存 `cached_shard_idx`，避免每次 `syscall(SYS_gettid)`
4. **Consumer 侧**：`ShmConsumer` 轮询所有 shard，汇总 metrics

### EventNotifier 改动

`PrepareFlush()` 和 `Flush()` 的 `FLUSH_FLAG=20` 阈值逻辑不变。但每 shard 维护独立的 `pending_count`，跨 shard 汇总后写入 eventfd。

## 与 eBPF 方案对比

| | LD_PRELOAD + Sharded | eBPF |
|---|---|---|
| FpUnwind (aarch64) | ✅ 保留 | ❌ 不能做 |
| 无锁 ring write | ✅ per-CPU shard | ✅ per-CPU ringbuf |
| AddressHandler | ✅ 完整 | ⚠️ BPF map 受限 |
| 部署 | LD_PRELOAD | 需 root + BPF |
| 原型 16T | 0.727s | 0.779s |

**结论：Sharded ring 在保持 LD_PRELOAD 全部功能的前提下，达到了 eBPF 级别的无锁扩展性，且不需要 root 权限或内核 BPF 支持。**

## 风险 & 待验证

1. **分片数**：16 片在 16T 下接近完美分布。真实场景线程数可能 > 16，碰撞概率增加。建议用质数分片或动态分片。
2. **Consumer 轮询开销**：原型 consumer 轮询 16 片的开销在 ~423ns/wakeup，可忽略。
3. **兼容性**：`ShmHeader` 需要新增字段。通过 `num_shards=0` 保持向后兼容。
4. **编译验证**：需要 OH SDK 编译环境到位后在 `cyc_nativehook` 上验证。
