const PptxGenJS = require("pptxgenjs");

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
  header(s, 2, "上次反馈闭环 + 死锁修复");

  // === LEFT PANEL: 上次反馈 ===
  s.addShape("rect", { x: 0.5, y: 1.35, w: 5.9, h: 0.45, fill: { color: K.blueBg }, rectRadius: 0.05 });
  s.addText("上次组会遗留问题闭环", { x: 0.7, y: 1.35, w: 5.5, h: 0.45, fontSize: 13, bold: true, color: K.blue, valign: "middle" });

  const feedback = [
    { num: "1", title: "热点定位", desc: "ring 共享状态竞争 → 批量发布解决" },
    { num: "2", title: "代码移植", desc: "Gitee fork 到公司 GitLab 并完成" },
  ];
  feedback.forEach((f, i) => {
    const y = 2.1 + i * 1.0;
    s.addShape("rect", { x: 0.55, y, w: 0.45, h: 0.45, fill: { color: K.blue }, rectRadius: 0.22 });
    s.addText(f.num, { x: 0.55, y, w: 0.45, h: 0.45, fontSize: 14, bold: true, color: K.white, align: "center", valign: "middle", fontFace: "Consolas" });
    s.addText(f.title, { x: 1.15, y, w: 1.3, h: 0.45, fontSize: 14, bold: true, color: K.dark, valign: "middle" });
    s.addText(f.desc, { x: 2.5, y, w: 3.7, h: 0.45, fontSize: 13, color: K.body, valign: "middle" });
  });

  // === RIGHT PANEL: 死锁修复 ===
  s.addShape("rect", { x: 6.8, y: 1.35, w: 6.0, h: 0.45, fill: { color: K.redBg }, rectRadius: 0.05 });
  s.addText("子阶段 36 死锁修复", { x: 7.0, y: 1.35, w: 5.6, h: 0.45, fontSize: 13, bold: true, color: K.red, valign: "middle" });

  // Before/after boxes
  s.addShape("rect", { x: 6.8, y: 2.0, w: 2.7, h: 1.4, fill: { color: K.redBg }, rectRadius: 0.08 });
  s.addText("修复前", { x: 6.9, y: 2.05, w: 2.5, h: 0.3, fontSize: 10, color: K.red, align: "center" });
  s.addText("8.60s", { x: 6.9, y: 2.4, w: 2.5, h: 0.6, fontSize: 28, bold: true, color: K.red, align: "center", fontFace: "Consolas" });
  s.addText("子阶段 34 · 1 线程", { x: 6.9, y: 2.95, w: 2.5, h: 0.3, fontSize: 9, color: K.muted, align: "center" });

  s.addShape("rect", { x: 10.1, y: 2.0, w: 2.7, h: 1.4, fill: { color: K.greenBg }, rectRadius: 0.08 });
  s.addText("修复后", { x: 10.2, y: 2.05, w: 2.5, h: 0.3, fontSize: 10, color: K.green, align: "center" });
  s.addText("0.28s", { x: 10.2, y: 2.4, w: 2.5, h: 0.6, fontSize: 28, bold: true, color: K.green, align: "center", fontFace: "Consolas" });
  s.addText("子阶段 34 · 1 线程", { x: 10.2, y: 2.95, w: 2.5, h: 0.3, fontSize: 9, color: K.muted, align: "center" });

  // Arrow and improvement
  s.addText("→", { x: 9.5, y: 2.3, w: 0.6, h: 0.8, fontSize: 24, color: K.muted, align: "center", valign: "middle" });

  // Root cause box
  s.addShape("rect", { x: 6.8, y: 3.65, w: 6.0, h: 1.4, fill: { color: K.white }, rectRadius: 0.08, shadow: { type: "outer", blur: 3, offset: 1, color: "000000", opacity: 0.04 } });
  s.addText("根因", { x: 7.0, y: 3.7, w: 5.6, h: 0.3, fontSize: 11, bold: true, color: K.muted });
  s.addText("外层 Lock() + Write() 内部重复加锁同一非递归互斥锁", { x: 7.0, y: 3.95, w: 5.6, h: 0.35, fontSize: 13, color: K.dark });
  s.addText("→ POSIX 未定义行为 → 子阶段 34/35 旧数据作废重采", { x: 7.0, y: 4.3, w: 5.6, h: 0.35, fontSize: 12, color: K.red });
  s.addText("修复：去掉外层多余的 Lock/Unlock，Write() 内部已有锁", { x: 7.0, y: 4.65, w: 5.6, h: 0.3, fontSize: 11, color: K.muted });

  // Bottom callout
  calloutBox(s, "子阶段 28-33 不受影响（HookWriter 锁）· 34/35 数据已修复重采", 0.55, 5.5, 12.3, K.blue, K.blueBg);
}

// --- Slide 3: Ablation Data ---
{
  const s = pptx.addSlide();
  header(s, 3, "StackWriter 层三层拆解", "热路径最后一段：环形写入 → eventfd 通知 → consumer drain · 固定 100 万次迭代 · 修复后重采");

  // Left: stacked bar showing 1T decomposition
  const barData = [
    { name: "环形写入 + 内部锁", labels: ["1 线程"], values: [0.28] },
    { name: "eventfd 系统调用", labels: ["1 线程"], values: [0.54] },
    { name: "consumer drain", labels: ["1 线程"], values: [0.03] },
  ];
  s.addChart(pptx.charts.BAR, barData, {
    x: 0.5, y: 1.4, w: 5.8, h: 2.6,
    barDir: "bar", barGrouping: "stacked",
    chartColors: [K.blue, K.orange, K.green],
    catAxisLabelFontSize: 12, valAxisLabelFontSize: 10,
    valAxisTitle: "秒", valAxisTitleFontSize: 10,
    valAxisMaxVal: 1.0, valAxisMinVal: 0,
    plotArea: { fill: { color: K.white } },
  });

  // Right annotation: key numbers
  s.addShape("rect", { x: 6.5, y: 1.5, w: 6.3, h: 0.55, fill: { color: K.blueBg }, rectRadius: 0.05 });
  s.addText("环形写入仅 0.28s · 占总量 33%", { x: 6.7, y: 1.5, w: 5.9, h: 0.55, fontSize: 13, color: K.blue, valign: "middle" });

  s.addShape("rect", { x: 6.5, y: 2.2, w: 6.3, h: 0.55, fill: { color: K.orangeBg }, rectRadius: 0.05 });
  s.addText("eventfd 系统调用 0.54s · 占总量 64%", { x: 6.7, y: 2.2, w: 5.9, h: 0.55, fontSize: 13, color: K.orange, valign: "middle" });

  s.addShape("rect", { x: 6.5, y: 2.9, w: 6.3, h: 0.55, fill: { color: K.greenBg }, rectRadius: 0.05 });
  s.addText("consumer drain 0.03s · 基本免费", { x: 6.7, y: 2.9, w: 5.9, h: 0.55, fontSize: 13, color: K.green, valign: "middle" });

  // Bottom: thread scaling line chart
  const lineData = [
    { name: "子阶段 34（纯写入）", labels: ["1 线程", "4 线程", "8 线程", "16 线程"], values: [0.28, 0.35, 0.59, 0.73] },
    { name: "子阶段 35（+eventfd）", labels: ["1 线程", "4 线程", "8 线程", "16 线程"], values: [0.82, 0.71, 0.76, 0.81] },
    { name: "子阶段 36（全链路）", labels: ["1 线程", "4 线程", "8 线程", "16 线程"], values: [0.84, 0.79, 0.85, 0.88] },
  ];
  s.addChart(pptx.charts.LINE, lineData, {
    x: 0.5, y: 3.9, w: 12.3, h: 3.0,
    chartColors: [K.blue, K.orange, K.green],
    showMarker: true, markerSize: 6,
    lineSize: 2.5,
    catAxisLabelFontSize: 11, valAxisLabelFontSize: 10,
    valAxisTitle: "秒", valAxisTitleFontSize: 10,
    valAxisMinVal: 0,
    plotArea: { fill: { color: K.white } },
  });

  calloutBox(s, "eventfd 系统调用是最大单一开销（65%）· consumer drain几乎免费 · 子阶段 35/36 线程扩展平坦", 0.55, 7.05, 12.3, K.blue, K.blueBg);
}

// --- Slide 4: Negative Experiments ---
{
  const s = pptx.addSlide();
  header(s, 4, "三项否定实验", "均指向同一结论：共享状态是根因，换锁不够");

  const cards = [
    { icon: "✗", title: "批量发布", sub: "buffer + batch write", result: "batch 4~64 全在噪声内", reason: "内部锁临界区仅 25ns，省无可省", color: K.red },
    { icon: "✗", title: "锁延迟模拟", sub: "临界区注入忙等 0→1000ns", result: "16线/1线竞争比始终 ~3×", reason: "锁持有时间不是瓶颈", color: K.orange },
    { icon: "△", title: "原子操作替代", sub: "CAS 替换互斥锁", result: "4线 +32%，8线以上无效", reason: "重试时的缓存行抖动 = 互斥锁", color: K.orange },
  ];

  cards.forEach((c, i) => {
    const x = 0.55 + i * 4.15;
    const y = 1.4;
    // Card background
    s.addShape("rect", { x, y, w: 3.9, h: 3.8, fill: { color: K.white }, rectRadius: 0.1, shadow: { type: "outer", blur: 6, offset: 2, color: "000000", opacity: 0.08 } });
    // Icon
    s.addText(c.icon, { x, y: y + 0.15, w: 3.9, h: 0.7, fontSize: 36, color: c.color, align: "center", fontFace: "Arial" });
    // Title
    s.addText(c.title, { x: x + 0.2, y: y + 0.85, w: 3.5, h: 0.45, fontSize: 18, bold: true, color: K.dark, align: "center" });
    // Subtitle
    s.addText(c.sub, { x: x + 0.2, y: y + 1.3, w: 3.5, h: 0.35, fontSize: 11, color: K.muted, align: "center" });
    // Result
    s.addShape("rect", { x: x + 0.3, y: y + 1.85, w: 3.3, h: 0.55, fill: { color: K.redBg }, rectRadius: 0.05 });
    s.addText(c.result, { x: x + 0.4, y: y + 1.85, w: 3.1, h: 0.55, fontSize: 12, bold: true, color: K.red, align: "center", valign: "middle" });
    // Reason
    s.addText(c.reason, { x: x + 0.2, y: y + 2.6, w: 3.5, h: 0.6, fontSize: 11, color: K.muted, align: "center", valign: "top" });
  });

  calloutBox(s, "需要消除共享状态，而不是换一种锁", 0.55, 5.8, 5.5, K.red, K.redBg);
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
  header(s, 6, "eBPF vs LD_PRELOAD 重对比", "优化后 LD_PRELOAD 与 eBPF 环形输出模式，100 万次迭代");

  // Grouped bar chart
  const barData = [
    { name: "LD_PRELOAD（优化后）", labels: ["1 线程", "4 线程", "8 线程", "16 线程"], values: [0.40, 1.13, 1.47, 1.43] },
    { name: "eBPF 环形输出", labels: ["1 线程", "4 线程", "8 线程", "16 线程"], values: [4.28, 1.13, 0.92, 0.78] },
  ];
  s.addChart(pptx.charts.BAR, barData, {
    x: 0.5, y: 1.4, w: 8.5, h: 4.2,
    barDir: "col", barGrouping: "clustered",
    chartColors: [K.blue, K.red],
    catAxisLabelFontSize: 12, valAxisLabelFontSize: 10,
    valAxisTitle: "秒（越低越好）", valAxisTitleFontSize: 10,
    valAxisMinVal: 0,
    plotArea: { fill: { color: K.white } },
    legendPos: "b", legendFontSize: 11,
  });

  // Right annotations
  s.addShape("rect", { x: 9.5, y: 1.6, w: 3.3, h: 0.55, fill: { color: K.blueBg }, rectRadius: 0.05 });
  s.addText("1T: LD 碾压 10.8×", { x: 9.7, y: 1.6, w: 2.9, h: 0.55, fontSize: 13, color: K.blue, valign: "middle" });

  s.addShape("rect", { x: 9.5, y: 2.4, w: 3.3, h: 0.55, fill: { color: K.white }, rectRadius: 0.05 });
  s.addText("4T: 平手", { x: 9.7, y: 2.4, w: 2.9, h: 0.55, fontSize: 13, color: K.body, valign: "middle" });

  s.addShape("rect", { x: 9.5, y: 3.2, w: 3.3, h: 0.55, fill: { color: K.redBg }, rectRadius: 0.05 });
  s.addText("8T: eBPF 快 1.6×", { x: 9.7, y: 3.2, w: 2.9, h: 0.55, fontSize: 13, color: K.red, valign: "middle" });

  s.addShape("rect", { x: 9.5, y: 4.0, w: 3.3, h: 0.55, fill: { color: K.redBg }, rectRadius: 0.05 });
  s.addText("16T: eBPF 快 1.8×", { x: 9.7, y: 4.0, w: 2.9, h: 0.55, fontSize: 13, color: K.red, valign: "middle" });

  calloutBox(s, "旧 8T: LD 3.12s vs eBPF 1.71s（1.8×）→ 新: 1.47s vs 0.92s（1.6×）  差距缩小但趋势不变", 0.55, 6.0, 12.3, K.blue, K.blueBg);
}

// --- Slide 7: 架构对比：LD_PRELOAD + Sharded Ring vs eBPF ---
{
  const s = pptx.addSlide();
  header(s, 7, "架构对比：LD_PRELOAD + Sharded Ring vs eBPF");

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

// --- Slide 8: OH 代码改动提案 ---
{
  const s = pptx.addSlide();
  header(s, 8, "OH 代码改动提案", "三个改动点 · 集中在 StackWriter 层 · 不影响上层 hook 函数");

  function box(x, y, w, h, text, bg, tc = K.dark, fs = 10) {
    s.addShape("rect", { x, y, w, h, fill: { color: bg }, rectRadius: 0.05, line: { color: K.border, pt: 0.5 } });
    s.addText(text, { x, y, w, h, fontSize: fs, color: tc, align: "center", valign: "middle", fontFace: "Arial" });
  }
  function marker(n, x, y, c) {
    s.addShape("ellipse", { x, y, w: 0.28, h: 0.28, fill: { color: c } });
    s.addText(String(n), { x, y, w: 0.28, h: 0.28, fontSize: 9, bold: true, color: K.white, align: "center", valign: "middle", fontFace: "Consolas" });
  }

  // === TOP ROW: Current ===
  s.addText("当前：地址分片", { x: 0.5, y: 1.35, w: 3, h: 0.3, fontSize: 12, bold: true, color: K.muted });
  const flowY = 1.7, flowH = 0.5, gap = 0.25;
  let x = 0.4;
  box(x, flowY, 1.8, flowH, "hook_malloc\nFpUnwind + fill", K.blueBg); x += 1.8 + gap;
  s.addText("→", { x, y: flowY, w: gap, h: flowH, fontSize: 14, color: K.muted, align: "center", valign: "middle" }); x += gap;
  box(x, flowY, 2.0, flowH, "SendStackWithPayload\naddr % N → Block", K.white); marker(1, x + 0.85, flowY - 0.22, K.red); x += 2.0 + gap;
  s.addText("→", { x, y: flowY, w: gap, h: flowH, fontSize: 14, color: K.muted, align: "center", valign: "middle" }); x += gap;
  box(x, flowY, 1.8, flowH, "ShareMemoryBlock\n内部全局锁", K.orangeBg, K.orange); marker(2, x + 0.75, flowY - 0.22, K.red); x += 1.8 + gap;
  s.addText("→", { x, y: flowY, w: gap, h: flowH, fontSize: 14, color: K.muted, align: "center", valign: "middle" }); x += gap;
  box(x, flowY, 2.0, flowH, "PrepareFlush / Flush\nglobal counter → Post", K.white); marker(3, x + 0.85, flowY - 0.22, K.red); x += 2.0 + gap;
  s.addText("→", { x, y: flowY, w: gap, h: flowH, fontSize: 14, color: K.muted, align: "center", valign: "middle" }); x += gap;
  box(x, flowY, 2.2, flowH, "Consumer\n单个环形区遍历", K.greenBg);

  // === ARROW between ===
  s.addText("↓", { x: 6.2, y: 2.4, w: 0.8, h: 0.35, fontSize: 18, color: K.blue, align: "center", valign: "middle" });

  // === BOTTOM ROW: Proposed ===
  s.addText("改为：线程分片", { x: 0.5, y: 2.8, w: 3, h: 0.3, fontSize: 12, bold: true, color: K.green });
  x = 0.4; const flowY2 = 3.15;
  box(x, flowY2, 1.8, flowH, "hook_malloc\nFpUnwind + fill", K.blueBg); x += 1.8 + gap;
  s.addText("→", { x, y: flowY2, w: gap, h: flowH, fontSize: 14, color: K.muted, align: "center", valign: "middle" }); x += gap;
  box(x, flowY2, 2.0, flowH, "SendStackWithPayload\ntid % N → Block[shard]", K.greenBg, K.green); marker(1, x + 0.85, flowY2 - 0.22, K.green); x += 2.0 + gap;
  s.addText("→", { x, y: flowY2, w: gap, h: flowH, fontSize: 14, color: K.muted, align: "center", valign: "middle" }); x += gap;
  box(x, flowY2, 1.8, flowH, "ShareMemoryBlock\nshard 内无锁写入", K.greenBg, K.green); marker(2, x + 0.75, flowY2 - 0.22, K.green); x += 1.8 + gap;
  s.addText("→", { x, y: flowY2, w: gap, h: flowH, fontSize: 14, color: K.muted, align: "center", valign: "middle" }); x += gap;
  box(x, flowY2, 2.0, flowH, "PrepareFlush / Flush\nper-shard counter → Post", K.greenBg, K.green); marker(3, x + 0.85, flowY2 - 0.22, K.green); x += 2.0 + gap;
  s.addText("→", { x, y: flowY2, w: gap, h: flowH, fontSize: 14, color: K.muted, align: "center", valign: "middle" }); x += gap;
  box(x, flowY2, 2.2, flowH, "Consumer\n轮询所有分片", K.greenBg);

  // === Legend ===
  const LY = 4.1;
  const legend = [
    { n: 1, t: "hook_client.cpp:628 — addr % N → tid % N + thread_local 缓存 TID", c: K.green },
    { n: 2, t: "stack_writer.cpp:89 — PutWithPayloadTimeout 新增 shard_idx 参数，内部无锁", c: K.green },
    { n: 3, t: "stack_writer.cpp:94 — PrepareFlush/Flush 每 shard 独立计数，汇总 Post", c: K.green },
  ];
  legend.forEach((l, i) => {
    const ly = LY + i * 0.45;
    marker(l.n, 0.7, ly + 0.06, l.c);
    s.addText(l.t, { x: 1.15, y: ly, w: 11, h: 0.4, fontSize: 11, color: K.body, valign: "middle" });
  });

  calloutBox(s, "改动集中在 StackWriter 层 · 不影响 hook_malloc/hook_free · num_shards=0 时向后兼容", 0.55, 5.9, 12.3, K.blue, K.blueBg);
}

// --- Slide 9: 分片环形区 — 突破性发现 ---
{
  const s = pptx.addSlide();
  header(s, 9, "分片环形区 — 突破性发现", "基于线程 ID 的每核分片，完全无锁写入 · 子阶段 36 全链路");

  // Grouped bar: 4T and 16T, mutex vs sharded vs eBPF
  const barData = [
    { name: "互斥锁（当前）", labels: ["4 线程", "16 线程"], values: [0.555, 1.038] },
    { name: "分片环形区（新方案）", labels: ["4 线程", "16 线程"], values: [0.345, 0.727] },
    { name: "eBPF 环形输出", labels: ["4 线程", "16 线程"], values: [1.13, 0.779] },
  ];
  s.addChart(pptx.charts.BAR, barData, {
    x: 0.5, y: 1.4, w: 8.0, h: 4.5,
    barDir: "col", barGrouping: "clustered",
    chartColors: [K.muted, K.green, K.red],
    catAxisLabelFontSize: 13, valAxisLabelFontSize: 10,
    valAxisTitle: "秒（越低越好）", valAxisTitleFontSize: 10,
    valAxisMinVal: 0, valAxisMaxVal: 1.2,
    plotArea: { fill: { color: K.white } },
    legendPos: "b", legendFontSize: 11,
  });

  // Right: key takeaways
  const items = [
    { label: "4 线程改善", value: "↓ 39%", color: K.green },
    { label: "16 线程改善", value: "↓ 30%", color: K.green },
    { label: "反超 eBPF", value: "0.727 < 0.779", color: K.blue },
  ];
  items.forEach((it, i) => {
    const y = 1.6 + i * 1.2;
    s.addShape("rect", { x: 9.0, y, w: 3.8, h: 0.9, fill: { color: K.white }, rectRadius: 0.08, shadow: { type: "outer", blur: 4, offset: 1, color: "000000", opacity: 0.05 } });
    s.addText(it.value, { x: 9.2, y: y + 0.05, w: 3.4, h: 0.5, fontSize: 24, bold: true, color: it.color, fontFace: "Consolas" });
    s.addText(it.label, { x: 9.2, y: y + 0.55, w: 3.4, h: 0.3, fontSize: 11, color: K.muted });
  });

  calloutBox(s, "LD_PRELOAD 也能做到每核无锁写入 — 不需要引入 eBPF", 0.55, 6.4, 7.0, K.green, K.greenBg);
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
    { num: "01", title: "Shard + Batch 组合优化", desc: "回归显示 batch64 16T=0.587s 仍优于 sharded 0.727s\n两者组合（batch 减开销 + shard 消锁）可能进一步突破" },
    { num: "02", title: "Shard 数量 Sweep", desc: "当前固定 16 片，sweep 4/8/16/32/64\n找到不同线程数下的最佳分片数" },
    { num: "03", title: "代码合入 + 文档整理", desc: "实验代码已验证，合入 main 分支\n清理实验分支，更新工作记录" },
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
pptx.writeFile({ fileName: out });
console.log("wrote " + out);
