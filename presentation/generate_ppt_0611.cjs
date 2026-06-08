const PptxGenJS = require("pptxgenjs");

const pptx = new PptxGenJS();
pptx.layout = "LAYOUT_WIDE";
pptx.author = "陈一驰";
pptx.title = "native_hook Producer Hot Path Progress — 2026.06.11";

const C = {
  blue: "2563EB", green: "059669", red: "DC2626", orange: "EA580C",
  gray: "6B7280", dark: "111827", white: "FFFFFF", lightBg: "F3F4F6",
  blueBg: "EEF2FF", greenBg: "ECFDF5",
};

function hdr(s, num, title) {
  s.addText(String(num).padStart(2, "0"), { x: 0.4, y: 0.2, w: 0.7, h: 0.5, fontSize: 24, bold: true, color: C.blue, align: "right" });
  s.addText(title, { x: 1.3, y: 0.2, w: 11, h: 0.5, fontSize: 22, bold: true, color: C.dark });
  s.addShape("rect", { x: 0, y: 0.85, w: 13.33, h: 0.02, fill: { color: "E5E7EB" } });
}

function tbl(s, rows, y = 1.1) {
  s.addTable(rows, {
    x: 0.5, y, w: 12.3, fontSize: 13, border: { type: "solid", pt: 0.5, color: "D1D5DB" },
    rowH: 0.38, colW: rows[0].map(() => 12.3 / rows[0].length),
    autoPage: false,
  });
}

function bullets(s, items, y = 1.2) {
  s.addText(items.map(t => ({ text: t, options: { bullet: true } })),
    { x: 0.6, y, w: 12, h: 5.5, fontSize: 15, color: C.dark, lineSpacing: 26 });
}

// === Slide 1: Title ===
{
  const s = pptx.addSlide();
  s.addText("native_hook Producer 端\n优化进展与 Sharded Ring 提案", {
    x: 0.5, y: 1.5, w: 12, h: 2, fontSize: 32, bold: true, color: C.dark, align: "center" });
  s.addText("陈一驰  2026.06.11", {
    x: 0.5, y: 3.8, w: 12, h: 0.5, fontSize: 16, color: C.gray, align: "center" });
}

// === Slide 2: Feedback + Deadlock Fix ===
{
  const s = pptx.addSlide();
  hdr(s, 2, "上次反馈闭环 + Sub-stage 36 死锁修复");
  bullets(s, [
    "上次四个问题全部闭环：热点定位→ batch publish 解决；perf → ablation 替代；fork 完成；eBPF 高线程数据补充",
    "新发现：sub-stage 36 死锁 — StackWriter 外部 Lock() + Write() 内部 pthread_mutex_lock 同一非递归 mutex",
    "修复：删除外层 Lock/Unlock，Write() 自行管理锁",
    "副作用：旧 sub=34/35 数据在 double-lock UB 下采集，31x 性能退化，全部作废并重新采集",
  ]);
}

// === Slide 3: 34/35/36 Ablation Data ===
{
  const s = pptx.addSlide();
  hdr(s, 3, "StackWriter 三层拆解（修复后数据）");
  tbl(s, [
    ["sub-stage", "含义", "1T", "4T", "8T", "16T"],
    ["34 write_only", "ring write + inner_mutex_", "0.28s", "0.35s", "0.59s", "0.73s"],
    ["35 flush_only", "34 + eventfd 通知", "0.82s", "0.71s", "0.76s", "0.81s"],
    ["36 full", "35 + consumer drain", "0.84s", "0.79s", "0.85s", "0.88s"],
  ]);
  bullets(s, [
    "1T 逐层拆解：ring write 0.28s → +eventfd 0.54s → +consumer 0.03s",
    "Eventfd syscall 是最大单一开销（65%），consumer drain 几乎免费",
    "Inner_mutex_ 临界区极小（~25ns），16T 竞争退化仅 2.6x",
  ], 3.2);
}

// === Slide 4: Negative Experiments ===
{
  const s = pptx.addSlide();
  hdr(s, 4, "三项否定实验");
  tbl(s, [
    ["实验", "方法", "结果", "结论"],
    ["StackWriter batch", "per-thread buffer, batch write", "batch 4~64 全在噪声内", "内锁临界区太小(<25ns)"],
    ["锁延迟模拟", "注入 busy-wait 到临界区", "delay 0→1000ns, 16T/1T 始终~3x", "临界区长度不是瓶颈"],
    ["CAS 锁替换", "CAS 替代 mutex", "4T 改善 32%, 8T+ 无效", "cache line bouncing = mutex"],
  ]);
  bullets(s, [
    "共同指向：锁本身不是问题，共享状态才是。需要消除共享状态，不是换锁。",
  ], 3.6);
}

// === Slide 5: Consumer Profile + FT Sweep ===
{
  const s = pptx.addSlide();
  hdr(s, 5, "Consumer Profiling + Flush Threshold Sweep");
  tbl(s, [["阶段", "耗时/wakeup", "占比"], ["eventfd read", "3961ns", "66%"], ["ring drain", "423ns", "7%"], ["printf", "1610ns", "27%"]]);
  tbl(s, [["ft", "1T", "4T", "8T", "16T"], ["1 (per-record)", "1.058s", "0.966s", "1.125s", "1.093s"],
    ["20 (OH 默认)", "0.318s", "0.929s", "0.941s", "1.033s"],
    ["50", "0.282s", "0.426s", "0.982s", "1.046s"],
    ["200", "0.244s", "0.451s", "0.957s", "0.975s"]], 3.1);
  bullets(s, ["Consumer drain ~免费（423ns 处理 ~20 条）", "OH FLUSH_FLAG=20 合理，提至 50 有 12% 空间"], 5.5);
}

// === Slide 6: eBPF Re-comparison ===
{
  const s = pptx.addSlide();
  hdr(s, 6, "eBPF vs LD_PRELOAD 重对比");
  tbl(s, [["", "T=1", "T=4", "T=8", "T=16"],
    ["LD_PRELOAD (优化后)", "0.40s", "1.13s", "1.47s", "1.43s"],
    ["eBPF libbpf_ring_output", "4.28s", "1.13s", "0.92s", "0.78s"],
    ["胜者", "LD 10.8x", "平手", "eBPF 1.6x", "eBPF 1.8x"]]);
  bullets(s, [
    "旧 8T: LD 3.12s vs eBPF 1.71s (1.8x) → 新: 1.47s vs 0.92s (1.6x)，差距缩小",
    "低线程 LD 碾压，高线程 eBPF per-CPU ringbuf 无锁优势仍在",
  ], 3.2);
}

// === Slide 7: Sharded Ring Breakthrough ===
{
  const s = pptx.addSlide();
  hdr(s, 7, "Sharded Ring — 突破性发现");
  tbl(s, [["sub=36 16T", "mutex", "CAS", "Sharded (16片)", "eBPF"],
    ["耗时", "1.038s", "1.014s", "0.727s", "0.779s"],
    ["vs mutex", "—", "—", "30% ↓", "25% ↓"]]);
  tbl(s, [["sub=36 4T", "mutex", "Sharded"],
    ["耗时", "0.555s", "0.345s"],
    ["改善", "—", "38% ↓"]], 3.1);
  bullets(s, [
    "Sharded ring: TID-based per-CPU 分片，每个线程独立 write_index，完全无锁",
    "16T 反超 eBPF（0.727s vs 0.779s），4T 改善 38%",
    "LD_PRELOAD 也能做到 per-CPU 无锁，不需要 eBPF",
  ], 4.6);
}

// === Slide 8: Architecture Comparison ===
{
  const s = pptx.addSlide();
  hdr(s, 8, "架构对比：Sharded Ring vs eBPF");
  tbl(s, [["", "LD_PRELOAD + Sharded", "eBPF"],
    ["无锁 ring write", "✅ TID-based shard", "✅ per-CPU ringbuf"],
    ["FpUnwind 栈回溯", "✅ 完整", "❌ 不能做"],
    ["AddressHandler", "✅ 完整", "⚠️ BPF map 受限"],
    ["部署", "LD_PRELOAD", "需 root + BPF"],
    ["原型 16T", "0.727s", "0.779s"]]);
  bullets(s, [
    "不需要引入 eBPF。LD_PRELOAD + TID-based sharded ring 在保留全部功能的前提下，达到甚至超过 eBPF 的性能。",
  ], 4.0);
}

// === Slide 9: OH Code Change Proposal ===
{
  const s = pptx.addSlide();
  hdr(s, 9, "OH 代码改动提案");
  bullets(s, [
    "已在 cyc_nativehook 和 yt_nativehook 上用 SHARDED_RING: 标注改动点",
    "改动 1: hook_client.cpp:628 — addr % N → tid % N，加 thread_local 缓存",
    "改动 2: stack_writer.cpp:89 — PutWithPayloadTimeout 新增 shard_idx 参数",
    "改动 3: stack_writer.cpp:94-107 — PrepareFlush/Flush 每 shard 独立计数",
    "改动范围小，集中在 StackWriter 层，不影响上层 hook 函数",
    "等待 OH SDK 环境到位后编译验证",
  ]);
}

// === Slide 10: Experiment Summary ===
{
  const s = pptx.addSlide();
  hdr(s, 10, "实验汇总");
  tbl(s, [["实验", "结论"],
    ["Sub-stage 36 死锁修复", "旧数据作废，修复后 30-40x"],
    ["34/35/36 三层拆解", "eventfd 最大开销，consumer 免费"],
    ["StackWriter batch", "否定 — 内锁临界区太小"],
    ["锁延迟模拟", "否定 — 临界区长度不是瓶颈"],
    ["CAS 锁替换", "部分有效(4T)，8T+ 无效"],
    ["Consumer profiling", "drain ~免费，瓶颈是 eventfd"],
    ["FT sweep", "FLUSH_FLAG=20 合理，50 有 12% 空间"],
    ["eBPF 重对比", "高线程仍有优势，差距缩小"],
    ["Sharded ring", "突破：30-38% 改善，反超 eBPF"],
  ], 1.4);
}

// === Slide 11: Core Conclusion ===
{
  const s = pptx.addSlide();
  hdr(s, 11, "核心结论");
  s.addText("Per-CPU 无锁写入是正确的优化方向\nLD_PRELOAD + TID-based sharded ring 是最优解", {
    x: 0.5, y: 1.5, w: 12, h: 1.5, fontSize: 22, bold: true, color: C.blue, align: "center" });
  bullets(s, [
    "数据支撑：原型 16T 改善 30%，反超 eBPF",
    "功能完整：保留 FpUnwind、AddressHandler，不受 eBPF 沙箱限制",
    "部署简单：不引入新依赖，不需要 root，不需要内核 BPF",
  ], 3.5);
}

// === Slide 12: Next Steps ===
{
  const s = pptx.addSlide();
  hdr(s, 12, "后续计划");
  bullets(s, [
    "真实代码编译验证 — OH SDK 环境到位后，在 cyc_nativehook 上编译验证 sharded ring",
    "团队 review — 基于标注的改动点，在 yt_nativehook 上提交 MR",
    "性能验证 — 编译通过后在真实设备上跑 benchmark，对比原型数据",
  ]);
  s.addText("谢谢！请批评指正", { x: 0.5, y: 4.5, w: 12, h: 0.5, fontSize: 20, color: C.gray, align: "center" });
}

const out = "native_hook_progress_2026-06-11.pptx";
pptx.writeFile({ fileName: out });
console.log("wrote " + out);
