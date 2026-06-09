import PptxGenJS from "pptxgenjs";

const pptx = new PptxGenJS();
pptx.layout = "LAYOUT_WIDE";
pptx.author = "陈一驰";
pptx.title = "native_hook 优化进展 — 2026.06.11";

// ---- Color system ----
const K = {
  dark:    "1E293B",
  body:    "334155",
  muted:   "64748B",
  blue:    "2563EB",
  blueBg:  "EFF6FF",
  green:   "059669",
  greenBg: "ECFDF5",
  red:     "DC2626",
  redBg:   "FEF2F2",
  orange:  "D97706",
  orangeBg:"FFFBEB",
  purple:  "7C3AED",
  purpleBg:"F5F3FF",
  white:   "FFFFFF",
  border:  "E2E8F0",
  bg:      "F8FAFC",
  navy:    "0F172A",
};

// ---- Reusable helpers ----
function header(s, num, title, subtitle = "") {
  // Top bar
  s.addShape("rect", { x: 0, y: 0, w: 13.33, h: 0.08, fill: { color: K.blue } });
  // Number badge
  s.addShape("rect", { x: 0.55, y: 0.35, w: 0.65, h: 0.55, fill: { color: K.blue }, rectRadius: 0.08 });
  s.addText(String(num), { x: 0.55, y: 0.35, w: 0.65, h: 0.55, fontSize: 22, bold: true, color: K.white, align: "center", fontFace: "Arial" });
  // Title text
  s.addText(title, { x: 1.45, y: 0.32, w: 11, h: 0.55, fontSize: 24, bold: true, color: K.dark, fontFace: "Microsoft YaHei" });
  if (subtitle) {
    s.addText(subtitle, { x: 1.45, y: 0.78, w: 11, h: 0.35, fontSize: 13, color: K.muted });
  }
  // Divider line
  s.addShape("rect", { x: 0.5, y: 1.15, w: 12.33, h: 0.015, fill: { color: K.border } });
}

function table(s, rows, opts = {}) {
  const y = opts.y || 1.4;
  const w = opts.w || 12.3;
  const colW = opts.colW || rows[0].map(() => w / rows[0].length);
  const headerRows = opts.headerRows || 1;
  const dataRows = rows.slice(headerRows);

  const tableRows = [
    // Header row — styled
    rows[0].map((cell, i) => ({
      text: cell, options: {
        bold: true, color: K.blue, fontSize: 12, fontFace: "Arial",
        fill: { color: K.blueBg }, align: "center", valign: "middle",
        border: { type: "solid", pt: 0.5, color: K.border },
      }
    })),
    // Data rows
    ...dataRows.map(row => row.map((cell, ci) => ({
      text: String(cell), options: {
        color: K.body, fontSize: 12, fontFace: "Consolas",
        fill: { color: K.white }, align: ci === 0 ? "left" : "center", valign: "middle",
        border: { type: "solid", pt: 0.5, color: K.border },
      }
    })))
  ];

  s.addTable(tableRows, {
    x: 0.55, y, w: 12.3, colW, rowH: 0.38,
    border: { type: "solid", pt: 0.5, color: K.border },
    autoPage: false,
  });
  return y + tableRows.length * 0.38 + 0.15;
}

function bullets(s, items, y = 1.4) {
  const bulletItems = items.map(t => {
    if (typeof t === "string") return { text: t, options: { bullet: { code: "2022" }, fontSize: 14, color: K.body, lineSpacing: 24 } };
    return { text: t.text, options: { bullet: t.highlight ? false : { code: "2022" }, fontSize: t.highlight ? 15 : 14, color: t.highlight ? K.blue : K.body, bold: t.highlight || false, lineSpacing: 24 } };
  });
  s.addText(bulletItems, { x: 0.65, y, w: 12, h: 5.5, valign: "top" });
}

function metricBox(s, label, value, unit, x, y, color) {
  s.addShape("rect", { x, y, w: 2.6, h: 1.2, fill: { color: K.white }, rectRadius: 0.1, shadow: { type: "outer", blur: 4, offset: 1, color: "000000", opacity: 0.06 } });
  s.addText(value, { x, y: y + 0.1, w: 2.6, h: 0.7, fontSize: 28, bold: true, color, align: "center", fontFace: "Consolas" });
  s.addText(unit, { x, y: y + 0.75, w: 2.6, h: 0.3, fontSize: 11, color: K.muted, align: "center" });
  s.addText(label, { x, y: y + 0.98, w: 2.6, h: 0.25, fontSize: 10, color: K.muted, align: "center" });
}

function calloutBox(s, text, x, y, w, color, bgColor) {
  s.addShape("rect", { x, y, w, h: 0.45, fill: { color: bgColor }, rectRadius: 0.06 });
  s.addText(text, { x, y, w, h: 0.45, fontSize: 13, bold: true, color, align: "center", fontFace: "Arial" });
}

// ========================================================================
// SLIDES
// ========================================================================

// --- Slide 1: Title ---
{
  const s = pptx.addSlide();
  s.addShape("rect", { x: 0, y: 0, w: 13.33, h: 7.5, fill: { color: K.navy } });
  s.addShape("rect", { x: 0, y: 0, w: 13.33, h: 0.06, fill: { color: K.blue } });
  s.addText("native_hook Producer 端", { x: 0.5, y: 1.6, w: 12.3, h: 1.0, fontSize: 40, bold: true, color: K.white, align: "center" });
  s.addText("优化进展与分片环形区提案", { x: 0.5, y: 2.5, w: 12.3, h: 0.8, fontSize: 28, color: "93C5FD", align: "center" });
  s.addShape("rect", { x: 4.5, y: 3.6, w: 4.3, h: 0.03, fill: { color: "334155" } });
  s.addText("组会汇报  ·  2026.06.11  ·  陈一驰", { x: 0.5, y: 3.9, w: 12.3, h: 0.5, fontSize: 14, color: K.muted, align: "center" });
}

// --- Slide 2: Feedback + Deadlock ---
{
  const s = pptx.addSlide();
  header(s, 2, "上次反馈闭环 + 子阶段 36 死锁修复");
  bullets(s, [
    { text: "上次四个问题全部闭环", highlight: true },
    "热点定位 → 批量发布解决；perf 分析 → 逐层拆解替代",
    "Gitee fork 到公司 GitLab（yt_nativehook + cyc_nativehook）",
    "eBPF 高线程数据补充：8T/16T 下 eBPF 反超 2.5~3x",
    { text: "Sub-stage 36 死锁修复", highlight: true },
    "根因：外层 Lock() + Write() 内部重复加锁同一非递归互斥锁 = 未定义行为",
    "修复：删除外层 Lock/Unlock，commit e19571b",
    "旧数据作废：double-lock UB 导致 31x 性能退化（8.60s → 0.28s）",
  ], 1.4);
}

// --- Slide 3: Ablation Data ---
{
  const s = pptx.addSlide();
  header(s, 3, "StackWriter 三层拆解", "固定 100 万次 mixed3 迭代，per-record 模式，修复后重采集");
  let y = 1.4;
  table(s, [
    ["子阶段", "含义", "1 线程", "4 线程", "8 线程", "16 线程"],
    ["34 纯写入", "环形写入 + 内部锁", "0.28s", "0.35s", "0.59s", "0.73s"],
    ["35 加通知", "+ eventfd 系统调用", "0.82s", "0.71s", "0.76s", "0.81s"],
    ["36 全链路", "+ 消费者消费", "0.84s", "0.79s", "0.85s", "0.88s"],
  ], { y: 1.4 });
  // Metric boxes
  metricBox(s, "环形写入内锁", "0.28s", "子阶段 34 · 单线程", 0.55, 3.5, K.blue);
  metricBox(s, "eventfd 系统调用", "0.54s", "子阶段 35-34 差值", 3.4, 3.5, K.orange);
  metricBox(s, "消费者消费", "0.03s", "子阶段 36-35 差值", 6.25, 3.5, K.green);
  metricBox(s, "16 线程竞争退化", "2.6×", "子阶段 34 · 16线/单线", 9.1, 3.5, K.purple);
}

// --- Slide 4: Negative Experiments ---
{
  const s = pptx.addSlide();
  header(s, 4, "三项否定实验", "均指向同一结论：共享状态是根因，换锁不够");
  table(s, [
    [  "实验", "方法", "结果", "根因"],
    ["StackWriter\n批量发布", "线程本地缓冲\n批量写入 4~64", "全在噪声范围\n无效果", "内部锁临界区\n仅 ~25 纳秒"],
    ["锁延迟模拟\nLNHV1_LOCK_DELAY_NS", "临界区注入\nbusy-wait 0→1000ns", "16T/1T 始终 ~3x\n无恶化", "锁持有时间\n不是瓶颈"],
    ["CAS 锁替换\nLNHV1_LOCK_FREE_RING", "CAS 替代\npthread_mutex_t", "4T: +32%\n8T+: 无效果", "CAS retry 的 cache\nline bouncing = mutex"],
  ], { y: 1.4, rowH: 0.85, headerRows: 1 });
  calloutBox(s, "→ 需要消除共享状态，而不是换一种锁", 0.55, 5.5, 4.5, K.red, K.redBg);
}

// --- Slide 5: Consumer Profile + FT ---
{
  const s = pptx.addSlide();
  header(s, 5, "Consumer Profiling + Flush Threshold Sweep");
  table(s, [
    ["阶段", "每次 wakeup 耗时", "占比"],
    ["eventfd read", "3961 ns", "66%"],
    ["ring drain（遍历 ~20 条）", "423 ns", "7%"],
    ["printf 输出", "1610 ns", "27%"],
  ], { y: 1.4, colW: [5, 4, 3.3] });
  table(s, [
    ["flush_threshold", "1T", "4T", "8T", "16T"],
    ["1  (per-record)", "1.058s", "0.966s", "1.125s", "1.093s"],
    ["20 (OH FLUSH_FLAG)", "0.318s", "0.929s", "0.941s", "1.033s"],
    ["50", "0.282s", "0.426s", "0.982s", "1.046s"],
    ["200", "0.244s", "0.451s", "0.957s", "0.975s"],
  ], { y: 3.5 });
  calloutBox(s, "Consumer drain 几乎免费 · OH FLUSH_FLAG=20 合理 · 提至 50 可省 12%", 0.55, 6.0, 9, K.green, K.greenBg);
}

// --- Slide 6: eBPF Re-comparison ---
{
  const s = pptx.addSlide();
  header(s, 6, "eBPF vs LD_PRELOAD 重对比", "1M mixed3 iters, libbpf_ring_output mode");
  table(s, [
    ["", "T=1", "T=4", "T=8", "T=16"],
    ["LD_PRELOAD (优化后)", "0.40s", "1.13s", "1.47s", "1.43s"],
    ["eBPF libbpf_ring_output", "4.28s", "1.13s", "0.92s", "0.78s"],
    ["胜者", "LD 10.8×", "平手", "eBPF 1.6×", "eBPF 1.8×"],
  ], { y: 1.5 });
  bullets(s, [
    { text: "旧 8T: LD 3.12s vs eBPF 1.71s（1.8×）→ 新: 1.47s vs 0.92s（1.6×），差距缩小", highlight: false },
    "低线程 LD_PRELOAD 碾压（1T: 10.8×）",
    "高线程 eBPF per-CPU ringbuf 无锁优势仍在，但幅度减小",
  ], 3.5);
}

// --- Slide 7: Sharded Ring ---
{
  const s = pptx.addSlide();
  header(s, 7, "Sharded Ring — 突破性发现", "TID-based per-CPU 分片，完全无锁写入");

  // Key metric
  metricBox(s, "Sharded 16T", "0.727s", "sub=36 · vs mutex -30%", 0.55, 1.45, K.green);
  metricBox(s, "Sharded 4T", "0.319s", "sub=36 · vs mutex -39%", 3.4, 1.45, K.green);
  metricBox(s, "vs eBPF 16T", "反超", "0.727s < 0.779s", 6.25, 1.45, K.blue);
  metricBox(s, "vs batch64 16T", "0.587s", "batch64 仍最优", 9.1, 1.45, K.purple);

  table(s, [
    ["sub=36 对比", "mutex", "CAS", "Sharded (16片)", "eBPF"],
    ["16T 耗时", "1.038s", "1.014s", "0.727s", "0.779s"],
    ["vs mutex", "—", "—", "↓ 30%", "↓ 25%"],
    ["4T 耗时", "0.555s", "—", "0.345s", "—"],
    ["vs mutex", "—", "—", "↓ 39%", "—"],
  ], { y: 3.2 });
  calloutBox(s, "LD_PRELOAD 也能做到 per-CPU 无锁 —— 不需要引入 eBPF", 0.55, 6.0, 8, K.green, K.greenBg);
}

// --- Slide 8: Architecture Comparison ---
{
  const s = pptx.addSlide();
  header(s, 8, "架构对比：LD_PRELOAD + Sharded Ring vs eBPF");

  table(s, [
    ["对比维度", "LD_PRELOAD + Sharded Ring", "eBPF"],
    ["无锁 ring write", "TID-based per-CPU shard", "per-CPU ringbuf"],
    ["FpUnwind 栈回溯 (aarch64)", "✓ 完整支持", "✗ 不能做"],
    ["AddressHandler 追踪", "✓ 完整", "⚠ BPF map 受限"],
    ["部署方式", "LD_PRELOAD (已有)", "需 root + BPF 权限"],
    ["原型 16T sub=36", "0.727s", "0.779s"],
    ["功能完整度", "无损失", "损失 FpUnwind"],
  ], { y: 1.5 });

  bullets(s, [
    { text: "结论：不需要引入 eBPF。Sharded ring 在保留全部功能的前提下，性能和部署都优于 eBPF。", highlight: true },
    "eBPF 的沙箱限制（不能调用户态函数、不能做栈回溯）让它无法完整替代 hook 热路径",
    "而 sharded ring 直接在现有 LD_PRELOAD 架构上消除了共享锁竞争",
  ], 5.2);
}

// --- Slide 9: OH Code Change ---
{
  const s = pptx.addSlide();
  header(s, 9, "OH 代码改动提案", "已在 cyc_nativehook / yt_nativehook 上用 SHARDED_RING: 标注");

  table(s, [
    ["位置", "当前实现", "改为"],
    ["hook_client.cpp:628\nSendStackWithPayload", "addr % g_sharedMemCount\n(地址分片)", "tid % g_sharedMemCount\n(TID 分片) + thread_local 缓存"],
    ["stack_writer.cpp:89\nPutWithPayloadTimeout", "单 ring 写入\n内部 mutex", "新增 shard_idx 参数\nShareMemoryBlock 按 shard 无锁写"],
    ["stack_writer.cpp:94\nPrepareFlush / Flush", "全局 dataCount_\nglobal flushCount_", "每 shard 独立计数器\n汇总后 Post 一次"],
  ], { y: 1.5, rowH: 0.95 });

  bullets(s, [
    { text: "改动集中在 StackWriter 层，不影响上层 hook_malloc/hook_free 等函数", highlight: true },
    "兼容性：num_shards=0 时保持现有行为，向后兼容",
    "等待 OH SDK 环境到位后编译验证",
  ], 5.2);
}

// --- Slide 10: Summary Table ---
{
  const s = pptx.addSlide();
  header(s, 10, "实验汇总");

  table(s, [
    ["实验", "结论", "状态"],
    ["Sub-stage 36 死锁修复", "旧数据作废，修复后 30-40× 更快", "✓"],
    ["34/35/36 三层拆解", "eventfd (65%) 最大开销，consumer 免费", "✓"],
    ["StackWriter batch publish", "内锁临界区太小，batch 无效", "✗"],
    ["锁延迟模拟", "临界区长度不是瓶颈", "✗"],
    ["CAS 锁替换", "4T 有效，8T+ cache line bouncing 抵消", "△"],
    ["Consumer profiling", "ring drain 423ns/~20rec，几乎免费", "✓"],
    ["FT sweep (1→200)", "FLUSH_FLAG=20 合理，50 有 12% 空间", "✓"],
    ["eBPF 重对比", "高线程仍有 1.6× 优势，差距缩小", "✓"],
    ["Sharded ring", "4T: -39%, 16T: -30%, 反超 eBPF", "★"],
  ], { y: 1.4 });
}

// --- Slide 11: Core Message ---
{
  const s = pptx.addSlide();
  s.addShape("rect", { x: 0, y: 0, w: 13.33, h: 7.5, fill: { color: K.navy } });

  s.addText("核心结论", { x: 0.5, y: 1.2, w: 12.3, h: 0.6, fontSize: 16, color: K.muted, align: "center", fontFace: "Arial" });

  s.addText("Per-CPU 无锁写入\n是正确的优化方向", {
    x: 0.5, y: 1.8, w: 12.3, h: 1.6, fontSize: 34, bold: true, color: K.white, align: "center", lineSpacing: 48 });

  s.addShape("rect", { x: 4.5, y: 3.5, w: 4.3, h: 0.03, fill: { color: "334155" } });

  s.addText("LD_PRELOAD + TID-based Sharded Ring 是最优解", {
    x: 0.5, y: 3.8, w: 12.3, h: 0.8, fontSize: 24, color: "93C5FD", align: "center" });

  // Three pillars
  const pillars = [
    { label: "数据", value: "16T: -30%", sub: "反超 eBPF" },
    { label: "功能", value: "完整", sub: "保留 FpUnwind" },
    { label: "部署", value: "零依赖", sub: "无需 root/BPF" },
  ];
  pillars.forEach((p, i) => {
    const x = 2.0 + i * 3.5;
    s.addShape("rect", { x, y: 5.2, w: 2.8, h: 1.6, fill: { color: "1E3A5F" }, rectRadius: 0.1 });
    s.addText(p.label, { x, y: 5.3, w: 2.8, h: 0.35, fontSize: 12, color: K.muted, align: "center", fontFace: "Arial" });
    s.addText(p.value, { x, y: 5.6, w: 2.8, h: 0.7, fontSize: 22, bold: true, color: K.white, align: "center" });
    s.addText(p.sub, { x, y: 6.3, w: 2.8, h: 0.35, fontSize: 11, color: "93C5FD", align: "center" });
  });
}

// --- Slide 12: Next Steps ---
{
  const s = pptx.addSlide();
  header(s, 12, "后续计划");

  const steps = [
    { num: "01", title: "真实代码编译验证", desc: "OH SDK 环境到位后\n在 cyc_nativehook 上编译验证 sharded ring" },
    { num: "02", title: "团队 Review", desc: "基于 SHARDED_RING: 标注\n在 yt_nativehook 上提交 MR" },
    { num: "03", title: "性能验证", desc: "编译通过后在真实设备\n跑 benchmark 对比原型数据" },
  ];

  steps.forEach((st, i) => {
    const y = 1.8 + i * 1.7;
    // Number
    s.addShape("rect", { x: 0.7, y, w: 0.55, h: 0.55, fill: { color: K.blueBg }, rectRadius: 0.08 });
    s.addText(st.num, { x: 0.7, y, w: 0.55, h: 0.55, fontSize: 18, bold: true, color: K.blue, align: "center", fontFace: "Consolas" });
    // Title + desc
    s.addText(st.title, { x: 1.5, y: y - 0.05, w: 10, h: 0.4, fontSize: 17, bold: true, color: K.dark });
    s.addText(st.desc, { x: 1.5, y: y + 0.35, w: 10, h: 0.6, fontSize: 13, color: K.muted, lineSpacing: 18 });
    // Connector line
    if (i < 2) s.addShape("rect", { x: 0.97, y: y + 0.65, w: 0.015, h: 0.95, fill: { color: K.border } });
  });

  s.addShape("rect", { x: 0, y: 7.42, w: 13.33, h: 0.08, fill: { color: K.blue } });
  s.addText("谢谢！请批评指正", { x: 0.5, y: 6.6, w: 12.3, h: 0.5, fontSize: 16, color: K.muted, align: "center" });
}

// ---- Output ----
const out = "native_hook_progress_2026-06-11.pptx";
await pptx.writeFile({ fileName: out });
console.log("wrote " + out);
