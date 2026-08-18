# 阶段性实验汇报讲稿

对应 HTML：`group_meeting_2026-08-11_static_dynamic_benchmark.html`

## 第 1 页：汇报总览

这次主要讲两部分：新增的六个游戏语义 case，以及围绕这些场景展开的两条实验线。后续会把这六个 case 作为统一测试负载，分别运行 Valgrind、LSan/ASan、BCC memleak 和 heaptrack，比较四种方案的检测效果；Ghidra、BinAbsInspector、Dr. Memory 和 bpftrace 作为二进制分析补充实验保留。Benchmark 页面这里只介绍场景设计，暂不展示新增 case 的测试结果。

## 第 2 页：Benchmark 扩展

考虑到后续验收负载主要和《王者荣耀》相关，Benchmark 先初步加入了几个游戏语义 case，作为后续真实业务负载设计的起点。本轮把原来偏基础的分配行为，扩展成场景资源、帧对象、异步缓冲和 UI 混合四类游戏语义路径。每个 case 都有固定入口、生命周期标签和真值字段，后续主 Benchmark 的四个方案会在这些 case 上统一测试。

## 第 3 页：六个新增 case

六个 case 分别覆盖场景正常卸载、场景泄漏、场景持续增长、异步延迟释放、帧临时对象和 UI 与异步混合。它们先作为可重复的游戏负载代理，后续会用于 Valgrind、LSan/ASan、BCC memleak 和 heaptrack 的统一检测对比。

## 第 4 页：游戏语义代理路径

这一页把游戏语义 case 具体落到函数路径上。图中先从场景加载进入分配路径，再分成几种结果：一条路径正常卸载，一条路径进入泄漏或持续增长，另一条路径交给回收线程延迟处理。帧对象和 UI 混合路径也沿用同样的思路，分别对应高频释放和多种对象同时存在。

这样设计的原因是，单纯的随机 malloc 只能验证最基本的分配和释放配对；加入明确的函数名和调用关系后，静态方法可以追踪“哪里分配、哪里释放”，动态方法也可以观察对象最终是否回收。当前这些是可重复的游戏语义代理，还不能替代真实《王者荣耀》进程，但可以作为后续接入真实负载前的统一起点。

## 第 5 页：六个 case 的生命周期图

这一页用六张小图把 case 的生命周期画出来。绿色表示对象最后正常释放，橙色表示对象残留或持续增长，蓝色表示中间经过异步队列。箭头表示对象从创建到回收的过程。

前三个是场景资源：`game_scene_no_leak` 完整走完加载、使用、卸载；`game_scene_leak` 跳过卸载，留下对象；`game_scene_growth` 重复切换场景，让残留对象逐步增加。

后面三个关注不同的运行方式：`game_async_delayed_free` 由回收线程延迟释放；`game_frame_no_leak` 每帧创建、使用并在帧末清理；`game_mixed` 把 UI 正常释放和异步残留放在同一个场景里。

这六类图把正常释放、明确泄漏、持续增长、延迟释放和混合路径区分开，后续主 Benchmark 的四个方案会针对同一组生命周期进行观察和比较。

## 第 6 页：Ghidra 与 BinAbsInspector

这组实验是为了给报告第 4 章“通过二进制分析内存泄漏故障的方案调研”补充一手实验材料，重点看不同符号信息下的函数恢复、路径定位和风险扫描能力。Ghidra 主要恢复函数和调用关系，BinAbsInspector 在路径上扫描潜在风险。本轮三种 ELF 样本都完成了分析，静态结果可以帮助定位可疑路径，但无法直接给出运行时泄漏的 blocks 和 bytes。

## 第 7 页：动态运行时方案

这一页直接汇报基础 6 个 case 的已有结果，新增游戏 case 暂不展开测试数据。

Dr. Memory 的 precision 和 recall 都是 100%，说明基础 case 中检测结果和真值能够对齐，泄漏组也能定位到 Benchmark 函数和源码行。

bpftrace 的原始快照在 `no_leak` 中也有约 77,824 bytes，在明确泄漏场景中是 78,848 bytes。扣除稳定基线后，分别得到 0 和 1,024 bytes，说明它能观察到 Benchmark 的泄漏信号，但原始结果需要经过基线校准才能解释。

因此，Dr. Memory 的结果更适合确认最终泄漏集合，bpftrace 的结果更适合观察运行时变化；六个游戏 case 后续再接入这两种方法。

方法备注：Dr. Memory 通过运行时插桩记录每次分配、释放和调用栈，程序结束后根据配对结果汇总仍未释放的对象，所以更适合线下确认最终泄漏集合。bpftrace 基于 eBPF 用户态探针观察 libc 的 `malloc/free` 事件，可以在运行过程中查看内存变化和 outstanding 快照，但结果更容易受到 attach 时机、快照时点以及进程自身分配的影响，需要结合基线和观察窗口解释。

## 第 8 页：综合结论与后续安排

主 Benchmark 将在六个游戏语义 case 上统一比较 Valgrind、LSan/ASan、BCC memleak 和 heaptrack；Ghidra、BinAbsInspector、Dr. Memory 和 bpftrace 作为二进制分析补充，分别提供路径、风险和运行时证据。六个 case 为两条实验线提供统一入口和对照场景。

下一步先让 Valgrind、LSan/ASan、BCC memleak 和 heaptrack 接入六个 case，再统一记录场景名、生命周期、线程关系、调用栈、blocks 和 bytes，最后进行重复运行和输出对比；二进制分析补充线继续整理 Ghidra、BinAbsInspector、Dr. Memory 和 bpftrace 的输出差异。

本轮的核心产出是：Benchmark 新增了六个可复用的游戏语义场景，主对比方案和二进制分析补充方案边界已经明确，后续围绕同一组场景开展统一实验。
