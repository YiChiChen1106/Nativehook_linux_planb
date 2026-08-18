# Native Memory Leak Detection and Lightweight Profiling

这是一个面向 Linux native 程序的内存泄漏感知与轻量化采集项目作品集。

## 我负责的内容

- 分析 native hook 采集链路中的高频路径和数据写入开销。
- 设计统一 Benchmark，对比 Valgrind、LSan/ASan、BCC memleak 和 heaptrack 的检测效果、定位能力与工程开销。
- 设计六类基础泄漏场景，以及面向游戏业务的场景加载、异步资源、帧临时对象、网络包、音频解码和缓存淘汰等语义代理场景。
- 编写结果汇总脚本，把不同工具的日志和 profile 统一整理为 CSV/Markdown。
- 对静态二进制扫描、动态二进制插桩和 eBPF 运行时追踪方案进行调研，并形成报告章节和组会材料。

## 目录

- [`benchmark/`](benchmark/)：统一 workload、runner 和结果解析脚本。
- [`experiments/`](experiments/)：检测效果、定位能力和开销的汇总结果。
- [`presentations/`](presentations/)：项目阶段汇报 HTML 与讲稿。
- [`report/`](report/)：调研报告章节和 Benchmark 设计材料。

## 作品集说明

这个目录只保留适合公开展示的项目说明、实验方法、汇总结果和汇报材料。具体平台源码、公司内部仓库和内部数据不放在这里；相关实现仍以原工作仓库为准。

## 简历表述

负责 native 内存泄漏检测与轻量化采集方向的性能分析和实验评估，围绕高频采集链路进行性能优化，设计统一 Benchmark 对比多种内存泄漏检测工具，并完成游戏语义代理场景、输出解析和实验报告整理。
