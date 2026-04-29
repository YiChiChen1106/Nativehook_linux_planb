# native_hook 服务器侧推进计划与工作量评估

日期：2026-04-24

## 1. 文档目的

这份文档用于说明当前在服务器上推进 `native_hook` 的技术判断、后续计划、需要修改的部分，以及大致工作量，便于后续向 leader 汇报。

当前重点不是立即拿到正式 `with-hook` 结果，而是先回答两个问题：

- 服务器上能不能尽快跑起更完整的 `native_hook`
- “将 `native_hook` 性能开销压到 1% 以下”这个目标从当前已知情况看是否有希望

## 2. 当前结论

### 2.1 已完成工作

- 已在 Fedora Linux 服务器上完成 `no-hook baseline` 测试
- 已补做 `proxy experiment`，拆分了 `timestamp / atomic / stack / buffer / combined` 等典型固定开销
- 已确认 `native_hook` 的真实链路不是单文件测试程序，而是：

`native_daemon -> HookService -> Unix socket -> shared memory + eventfd -> target process 内的 native_hook`

### 2.2 这次排查得到的关键信息

- 服务器上的 `developtools_profiler` 最初是 sparse checkout，只拉了 `device/plugins/native_hook` 和 `device/plugins/native_daemon`
- 补全组件树后，已经能看到 `device/base`、`device/services`、`interfaces`、`protos` 等目录
- 但该仓库仍然不是一个可直接在 Fedora 上原样构建的完整 OpenHarmony 顶层源码树
- 当前服务器上有 `clang++`、`ninja`，但没有 `gn`、没有 `hb`
- `BUILD.gn` 依赖的 `build/ohos.gni` 并不在当前组件仓内，说明 upstream 默认是在更大的 OH 顶层树中构建

### 2.3 为什么原版 native_hook 不能直接在 Fedora 上“完整跑”

服务器上的最小编译探测已经说明，当前阻塞点不在测试程序，而在接入层和 OpenHarmony 依赖：

- `device/plugins/native_hook/include/hook_client.h` 直接依赖 `musl_malloc_dispatch.h`
- `device/plugins/native_hook/src/hook_client.cpp` 最小编译首先卡在 `dfx_regs_get.h`
- `musl_malloc_dispatch.h`、`musl_preinit_common.h`、`dfx_regs_get.h` 在当前仓库和系统头文件里都不存在
- `native_hook` / `native_daemon` 的 `BUILD.gn` 还依赖以下 OH 侧库或系统能力：
  - `faultloggerd:libunwinder`
  - `ffrt`
  - `hilog`
  - `ipc_core`
  - `samgr_proxy`
  - `hisysevent`
  - `init:libbegetutil`

结论是：

- 在 Fedora 上直接完整迁移原版 `native_hook`，不是“小改一下测试程序”就能完成
- 最大问题不在 `perf_test_data.cpp`
- 最大问题在 `hook` 接入层、OH 运行时依赖、以及构建体系

## 3. 对 1% 性能目标的当前判断

从当前 `baseline + proxy experiment` 的结果看，初步判断如下：

- 如果是默认、较完整、较高保真的 `native_hook` 配置，想把总体开销压到 `1%` 以下，难度很大
- 如果后续采用明显偏“轻量模式”的配置，`1%` 不是完全没有可能
- 影响最大的高风险项大概率仍然是：
  - 抓栈
  - 共享状态竞争
  - 频繁 flush / 唤醒
  - `free` 路径的额外工作

因此，后续判断 `1%` 是否可行，关键不是继续猜测，而是尽快准备一套更接近真实链路的实验框架。

## 4. 推荐推进思路

当前更适合采用“两层推进”的方式：

### 4.1 最终验证目标

最终如果要正式回答“`native_hook` 开销能不能压到 1% 以下”，仍然应该以**尽量接近原版链路**的方案为准。

原因是：

- 只有真实链路才能覆盖真实接入层开销
- 只有真实链路才能验证 `sample_interval`、`filter_size`、`fp_unwind`、`free_stack_report`、`blocked` 等配置组合
- 最终汇报更有说服力

### 4.2 当前服务器阶段的现实选择

按当前服务器条件看，后续大致有两种推进方式可以考虑：

- 先把服务器侧技术阻塞点收口
- 再做一个 Linux 上“更接近真实链路”的最小验证框架
- 用它提前判断哪些路径最可能把 overhead 卡死
- 后续一旦条件更完整，再切回尽量接近原版的正式验证

## 5. 后续计划

### 5.1 阶段 1：收口当前技术结论并固化材料

目标：

- 把“为什么 Fedora 上不能直接完整跑原版 native_hook”说明白
- 形成一版可对外汇报的书面结论

主要工作：

- 整理当前服务器排查结果
- 保留关键证据：
  - sparse checkout 情况
  - 缺少 `gn/hb`
  - `build/ohos.gni` 不存在
  - `hook_client.h` 依赖 `musl_malloc_dispatch.h`
  - `hook_client.cpp` 编译卡在 `dfx_regs_get.h`
- 形成当前结论和后续路线建议

工作量：

- 小
- 约 `1 ~ 2` 天

产出：

- 当前这份计划文档
- 后续可整理为 1 页汇报结论页

### 5.2 阶段 2：设计 Linux 侧“半真实 native_hook”验证框架

目标：

- 不追求完全等价原版 OH 实现
- 但尽量保留真实 `native_hook` 中最关键的链路要素

建议保留的部分：

- 高频 `malloc/free` workload
- hook 前台采集逻辑
- socket / shared memory / eventfd 传输链路
- 后台消费与 flush 逻辑
- 可切换的抓栈、采样、过滤、batching 配置

建议暂时替换的部分：

- OH/musl 专用 hook 接入层
- OH 专用日志/系统服务能力
- OH 专用 DFX / unwinder 接口

主要工作：

- 设计 Linux 侧最小可跑架构
- 明确哪些模块保留、哪些模块替换、哪些模块先裁掉
- 确定实验指标、配置矩阵和输出格式

工作量：

- 中
- 约 `3 ~ 5` 天

产出：

- 一版架构说明
- 一版实验配置表

### 5.3 阶段 3：实现 Linux 侧最小可跑链路

目标：

- 在当前服务器上做出“比 proxy experiment 更接近真实 native_hook”的验证版本

建议实现内容：

- Linux 可用的 `malloc/free` hook 接入方式
- 最小版前台采集模块
- 最小版共享内存写入
- `eventfd` 通知
- 最小版后台读取与统计
- 对接现有 `perf_test_data` 风格 workload

主要工作：

- 新增 Linux 可编译入口
- 替换 `musl_malloc_dispatch.h` 这套 OH/musl 专用接入
- 裁掉或替换 `hilog`、`hisysevent`、`samgr`、`ipc_core` 等 OH 依赖
- 用 Linux 可用方案替代 `dfx_regs_get.h` / OH 专用 unwinder 相关路径
- 新增本地编译与运行脚本

工作量：

- 中到大
- 约 `2 ~ 4` 周

产出：

- 一版可在 Fedora 上跑的“半真实 with-hook”版本
- 第一轮 baseline / with-hook 对照数据

### 5.4 阶段 4：跑配置对照，提前判断 1% 目标的可行性

目标：

- 不等正式环境，先把最关键的配置趋势摸清

建议优先验证的配置项：

- `sample_interval`
- `filter_size`
- `fp_unwind`
- `free` 路径是否抓栈
- flush / batching 策略
- 是否同步发送

主要工作：

- 设计实验矩阵
- 每组跑多次
- 异常值处理
- 统一计算 overhead
- 画图并形成结论

工作量：

- 中
- 约 `1 ~ 2` 周

产出：

- 一版“哪些设置最有希望接近 1%”的预判断
- 一版更适合正式实验的配置优先级

## 6. 需要改哪些东西

这一部分按两条技术路径拆开说明。

### 6.1 如果坚持在 Fedora 上直接跑“原版完整 native_hook”

需要改动或补齐的部分：

### 6.1.1 构建体系

- 补齐或替代 OH 顶层构建环境
- 解决 `build/ohos.gni` 缺失
- 补 `gn` / `hb` 或者自己重建 host build 方式

工作量：

- `1 ~ 2` 周

### 6.1.2 接入层

- 处理 `musl_malloc_dispatch.h`
- 处理 `musl_preinit_common.h`
- 把 OH/musl hook 接入迁移到 Fedora/glibc 可用方案

这是最大的改造点，因为它决定 hook 能不能真正接到 `malloc/free`

工作量：

- `2 ~ 4` 周

### 6.1.3 OH 依赖替换或裁剪

- `dfx_regs_get.h`
- `faultloggerd:libunwinder`
- `ffrt`
- `hilog`
- `ipc_core`
- `samgr_proxy`
- `hisysevent`
- `init:libbegetutil`

工作量：

- `1 ~ 3` 周

### 6.1.4 运行时流程改造

- `GetDeveloperMode()` / `IsBetaVersion()` 这类环境检查
- `signal 36` 启动路径
- `native_daemon` / `HookService` / `HookSocketClient` 的服务器侧兼容性

工作量：

- `1 ~ 2` 周

### 6.1.5 联调和实验

- 编译联调
- 输出文件验证
- baseline / with-hook 对照

工作量：

- `1 ~ 2` 周

### 6.1.6 总体判断

- 总工作量：`6 ~ 10` 周
- 风险等级：高
- 风险不在测试程序，而在 libc 接入层和 OH 运行时依赖

### 6.2 如果做 Linux 侧“半真实 native_hook”

需要改动的部分：

### 6.2.1 Linux hook 接入

- 新增 Linux 可用的 `malloc/free` 拦截方式
- 让 workload 真实走到 hook 前台逻辑

工作量：

- `1 ~ 2` 周

### 6.2.2 保留并改造数据链路

- 尽量保留 shared memory
- 尽量保留 `eventfd`
- 尽量保留 flush / batching
- 让后台消费者能够收数据和统计

工作量：

- `1 ~ 2` 周

### 6.2.3 裁掉 OH 专用部分

- 去掉 OH 专有日志/系统服务调用
- 用 Linux 可用实现替代必要功能

工作量：

- `3 ~ 7` 天

### 6.2.4 实验脚本与结果整理

- 补运行脚本
- 统一输出格式
- 汇总图表

工作量：

- `3 ~ 7` 天

### 6.2.5 总体判断

- 总工作量：`3 ~ 6` 周
- 风险等级：中
- 适合当前服务器推进
- 更适合提前筛热点方向，但不能代替最终正式结论

## 7. 推荐优先级

如果目标是“现在就开始推进，并为后续正式实验做准备”，建议优先级如下：

1. 固化当前技术结论
2. 设计 Linux 侧“半真实 native_hook”框架
3. 实现最小可跑链路
4. 跑配置对照，筛出最有希望接近 `1%` 的设置
5. 后续条件成熟后，再进入更接近原版链路的正式验证

## 8. 当前可选推进方式

按当前排查看，后续可以考虑两种推进方式：

- 先投入较长时间去攻 Fedora 上更接近原版的完整链路
- 先在服务器上做 Linux 侧“半真实链路”验证，用来做准备和筛热点

两者的取舍点主要在于：

- 是更优先看重“尽量接近原版链路”
- 还是更优先看重“先产出方向性结果并缩小正式实验范围”

## 9. 需要 leader 帮忙拍板的事项

建议后续请 leader 帮忙确认以下几件事：

- 当前阶段是否接受先做 Linux 侧“半真实 native_hook”验证
- 是否值得投入 `6 ~ 10` 周去尝试 Fedora 上完整迁移原版 native_hook
- 后续是否能提供更接近 OpenHarmony 的构建或运行环境
- 后续正式实验是更看重“尽快出方向性结果”，还是更看重“尽量原版、尽量可信”

## 10. 一句话结论

从当前服务器条件看，原版 `native_hook` 不能直接完整跑起来；如果强行在 Fedora 上做完整迁移，工作量大、风险高。更现实的推进方式是：先在服务器上做 Linux 侧“半真实链路”验证，用来提前筛掉不可能满足 `1%` 目标的设置，并为后续更正式的验证做准备。

## 11. 向 leader 咨询方案时的建议话术

这一部分用于和 leader 讨论“当前该选方案 A 还是方案 B”。

### 11.1 可直接口头汇报的版本

“我这两天先把服务器上的 `native_hook` 依赖链、构建条件和最小编译情况做了一轮排查。按目前看到的情况，原版 OpenHarmony `native_hook` 在当前 Fedora 服务器上，短期内可能还不能直接完整跑起来。现在看到的主要卡点不在测试程序本身，而更像是在三层：一层是 OH 顶层构建环境，一层是 `musl_malloc_dispatch.h` 这类 hook 接入层，还有一层是 `dfx_regs_get.h`、`hilog`、`faultloggerd` 这些 OH 运行时依赖。”  

“所以我这边先整理成了两条可选方案，想听一下您的意见。方案 A 是尽量按原版链路去推进，目标是后面做尽量接近真实环境的正式验证。方案 B 是先做 Linux 侧半真实链路，先把后面正式实验前需要的准备工作和热点方向摸出来。”  

“如果考虑方案 A，主要可能要处理四块。第一块是构建体系，要补齐或替代 OH 顶层构建环境，像 `build/ohos.gni`、`gn`、`hb` 这些问题要先解决。第二块是 hook 接入层，要处理 `musl_malloc_dispatch.h`、`musl_preinit_common.h` 这类 OH/musl 专用接口，让 hook 能真正接到 `malloc/free`。第三块是 OH 依赖，要处理 `dfx_regs_get.h`、`faultloggerd:libunwinder`、`hilog`、`ipc_core`、`samgr_proxy`、`hisysevent` 等依赖。第四块是运行时流程，要兼容 `GetDeveloperMode()`、`IsBetaVersion()`、`signal 36`、`native_daemon -> HookService -> HookSocketClient` 这整条启动链。这个方案如果要推进，工作量按现在的理解可能要按 `6~10 周` 去估，甚至还要看中间环境问题会不会继续放大。”  

“如果考虑方案 B，主要也是三块。第一块是把 OH/musl 专用的 hook 接入层替换成 Linux 可用的 `malloc/free` 拦截方式。第二块是尽量保留真实 `native_hook` 里最关键的数据链路，比如 `shared memory`、`eventfd`、`flush/batching` 和后台消费者。第三块是把 OH 专有日志、系统服务、DFX 接口先裁掉或换成 Linux 可用实现，再补一套本地编译和运行脚本。这个方案工作量相对会小一些，但按保守估计可能也要 `3~6 周`，不像前面看起来那么快。”  

“不过方案 B 和真实环境之间还是会有几处比较关键的出入，我也想先跟您说明一下。第一，hook 接入方式不再是原版 OH/musl 的 `malloc dispatch`，而会换成 Linux 可用方案，所以入口层的真实成本不完全一致。第二，抓栈和 unwind 相关实现也会有差异，比如可能改成 Linux 可用的 `backtrace` 或其他替代方式，因此栈采集成本不能完全等同于 OH 真机。第三，像 `hilog`、`hisysevent`、`samgr`、developer mode、beta version` 这些 OH 系统能力，在方案 B 里大概率会被裁掉或弱化，所以运行时环境会比真实链路更简化。第四，目标进程启动和 `signal 36` 触发路径可能也会做简化，后台控制流程未必和原版完全一致。也就是说，方案 B 更适合提前筛热点和配置方向，但最后如果要形成比较正式的结论，可能还是要回到更接近原版的链路上去验证。”  

“所以我想先请您帮我判断一下，当前阶段更适合优先哪条方案：是先投入方案 A，尽量往原版链路上靠；还是先做方案 B，把服务器侧准备工作和热点方向先收出来。后面我可以按您觉得更合适的方向继续往下推进。”  

### 11.2 发消息时的精简版

“我这边先把服务器上的 `native_hook` 情况做了一轮排查。按目前看到的情况，当前 Fedora 环境下，原版 `native_hook` 短期内可能还不能直接完整跑，主要卡在 OH 顶层构建环境、OH/musl 的 hook 接入层，以及 `dfx_regs_get.h`、`hilog`、`faultloggerd` 这些运行时依赖。”  

“现在我先整理了两条方案，想请您帮我判断一下优先级。方案 A 是尽量按原版链路推进，主要要补构建体系、处理 `musl_malloc_dispatch.h` 这类 hook 接入、替换或补齐 OH 依赖、兼容 `signal 36` 和 `native_daemon -> HookService` 启动链，工作量按保守估计大概 `6~10 周`。方案 B 是先做 Linux 侧半真实链路，把 hook 接入换成 Linux 可用方案，同时尽量保留 `shared memory + eventfd + flush/batching + 后台消费者` 这条数据链，再把 OH 专有日志、系统服务和 DFX 接口裁掉或替换，工作量按保守估计大概 `3~6 周`。”  

“不过方案 B 和真实环境会有一些差异，比如 hook 接入方式、抓栈实现、系统能力和启动流程都不完全一样，所以它更适合提前筛热点和配置方向，未必能直接替代最终正式结论。想请您看看，当前阶段更适合优先走方案 A，还是先走方案 B 做准备。”  
