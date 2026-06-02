const PptxGenJS = require("pptxgenjs");

const pptx = new PptxGenJS();
pptx.layout = "LAYOUT_WIDE";
pptx.author = "陈一驰";
pptx.title = "native_hook Producer Hot Path Progress";

// ====== TECH PALETTE ======
const C = {
  black:   "0A0E17",
  dark:    "111827",
  panel:   "1A2332",
  cyan:    "00E5FF",
  cyanBg:  "0D2B3E",
  green:   "00FF88",
  greenBg: "0D3320",
  orange:  "FF6D3A",
  orangeBg:"331A0D",
  purple:  "A78BFA",
  purpleBg:"1E1833",
  pink:    "FF6B9D",
  pinkBg:  "331825",
  white:   "F0F4FF",
  gray:    "64748B",
  line:    "1E293B",
  glow:    "00E5FF40",
};

function techHeader(slide, num, title, sub) {
  slide.addText(String(num).padStart(2,"0"), {
    x: 0.4, y: 0.2, w: 1.0, h: 0.55,
    fontSize: 24, color: C.cyan, fontFace: "Consolas", align: "right",
  });
  slide.addShape(pptx.ShapeType.rect, { x: 1.5, y: 0.25, w: 1.5/72, h: 0.5, fill: { color: C.cyan } });
  slide.addText(title, {
    x: 1.7, y: 0.2, w: 10, h: 0.4,
    fontSize: 18, bold: true, color: C.white, fontFace: "Microsoft YaHei",
  });
  if (sub) {
    slide.addText(sub, {
      x: 1.7, y: 0.55, w: 10, h: 0.3,
      fontSize: 10, color: C.gray,
    });
  }
  slide.addShape(pptx.ShapeType.rect, { x: 0, y: 0.95, w: 13.33, h: 0.5/72, fill: { color: C.line } });
}

function techBullets(slide, items, x, y, w, h, fs) {
  slide.addText(items.map(t => {
    if (typeof t === "string") return { text: t, options: { bullet: { code: "25B6" }, fontSize: fs || 11, color: C.gray, breakType: "none", paraSpaceAfter: 4 } };
    return t;
  }), {
    x: x || 0.6, y: y || 1.3, w: w || 12, h: h || 5.2,
    lineSpacing: 22, valign: "top",
  });
}

function techTable(slide, rows, x, y, w) {
  const cols = rows[0].length;
  const cw = (w || 12.2) / cols;
  const headerRow = rows[0].map((h, i) => ({
    text: h, options: { bold: true, fontSize: 9, color: C.cyan, fill: { color: C.dark }, align: i === 0 ? "left" : "center" }
  }));
  const dataRows = rows.slice(1).map((r, ri) =>
    r.map((c, ci) => ({
      text: String(c), options: {
        fontSize: 9, color: ci === cols - 1 ? C.green : C.gray,
        fill: { color: ri % 2 ? C.panel : C.black },
        align: ci === 0 ? "left" : "center",
      }
    }))
  );
  slide.addTable([headerRow, ...dataRows], {
    x: x || 0.5, y: y || 1.3, w: w || 12.2,
    border: { type: "solid", color: C.line, pt: 0.3 },
    colW: Array(cols).fill(cw), rowH: 0.34, margin: [2, 6, 2, 6],
  });
}

function metricKPI(slide, value, unit, label, x, y) {
  slide.addText(value, {
    x, y, w: 2.2, h: 0.7,
    fontSize: 36, bold: true, color: C.cyan, align: "center", fontFace: "Consolas",
  });
  slide.addText(unit, {
    x: x + 2.0, y: y + 0.05, w: 0.6, h: 0.3,
    fontSize: 11, color: C.gray,
  });
  slide.addText(label, {
    x, y: y + 0.7, w: 2.8, h: 0.3,
    fontSize: 9, color: C.gray, align: "center",
  });
}

// ==================== SLIDES ====================

// Slide 1 — Title
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.black };

  // Animated grid lines
  for (let i = 0; i < 6; i++) {
    s.addShape(pptx.ShapeType.rect, { x: 0, y: 1.2 * i, w: 13.33, h: 0.3/72, fill: { color: C.line } });
  }

  s.addShape(pptx.ShapeType.rect, { x: 1.5, y: 2.0, w: 0.8, h: 3.0, fill: { color: C.cyanBg } });
  s.addText("native_hook", {
    x: 2.5, y: 2.1, w: 9, h: 1.2,
    fontSize: 56, bold: true, color: C.white, fontFace: "Consolas",
  });
  s.addText("PRODUCER HOT PATH ANALYSIS", {
    x: 2.5, y: 3.2, w: 9, h: 0.5,
    fontSize: 16, color: C.cyan, fontFace: "Consolas",
  });
  s.addShape(pptx.ShapeType.rect, { x: 2.5, y: 3.85, w: 5, h: 1.5/72, fill: { color: C.cyan } });
  s.addText("2026.06.05  |  GROUP MEETING  |  陈一驰", {
    x: 2.5, y: 4.2, w: 9, h: 0.4,
    fontSize: 11, color: C.gray, fontFace: "Consolas",
  });

  s.addShape(pptx.ShapeType.rect, { x: 0, y: 7.3, w: 13.33, h: 1/72, fill: { color: C.cyan } });
})();

// Slide 2 — 反馈闭环
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.black };
  techHeader(s, 1, "上次组会反馈闭环", "FEEDBACK LOOP");

  const cards = [
    { tag: "RESOLVED", c: C.green, bg: C.greenBg, q: "\"4T比1T多1.8s\"", a: "ring共享状态竞争 → batch解决后4T: 2.72s→0.86s" },
    { tag: "UPGRADED", c: C.cyan, bg: C.cyanBg, q: "\"跑一下perf\"", a: "用ablation替代perf → sub-stage 34/35拆到模块内部" },
    { tag: "COMPLETED", c: C.orange, bg: C.orangeBg, q: "\"fork Gitee到GitLab\"", a: "5个核心函数全加batch → gitlab.youtune.tech/cychi/cyc_nativehook" },
    { tag: "PAUSED", c: C.gray, bg: C.panel, q: "\"4T eBPF异常快\"", a: "本轮聚焦producer端，eBPF线暂停待后续" },
  ];

  cards.forEach((c, i) => {
    const col = i % 2, row = Math.floor(i / 2);
    const x = 0.4 + col * 6.3, y = 1.2 + row * 2.8;
    s.addShape(pptx.ShapeType.roundRect, { x, y, w: 6.0, h: 2.5, fill: { color: c.bg }, rectRadius: 0.06 });
    s.addShape(pptx.ShapeType.rect, { x, y, w: 0.12, h: 2.5, fill: { color: c.c } });
    s.addText(c.tag, { x: x + 0.3, y: y + 0.15, w: 1.5, h: 0.25, fontSize: 7, bold: true, color: c.c, fontFace: "Consolas" });
    s.addText(c.q, { x: x + 0.3, y: y + 0.45, w: 5.4, h: 0.4, fontSize: 13, bold: true, color: C.white });
    s.addText(c.a, { x: x + 0.3, y: y + 1.1, w: 5.4, h: 1.2, fontSize: 11, color: C.gray, lineSpacing: 20 });
  });
})();

// Slide 3 — Writer/Ring 拆解
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.black };
  techHeader(s, 2, "Writer/Ring Impact 拆解实验", "SUB-STAGE 28~33  ·  100万次 malloc/free pair");

  techTable(s, [
    ["Sub", "测量内容", "1T", "4T", "8T", "16T"],
    ["28 no_ring", "基线（无ring操作）", "—", "—", "—", "—"],
    ["29 mutex_only", "仅拿锁 + tracking", "0.28s", "0.62s", "0.85s", "1.12s"],
    ["30 ring_index", "+ ring index检查", "0.45s", "1.35s", "1.92s", "2.48s"],
    ["31 record_copy", "+ 记录拷贝", "0.68s", "2.10s", "2.85s", "3.52s"],
    ["32 atomic_pub", "+ atomic write_index", "0.75s", "2.35s", "3.10s", "3.80s"],
    ["33 full_notify", "+ eventfd+consumer", "1.24s", "2.72s", "3.12s", "3.41s"],
  ], 0.4, 1.25, 12.4);

  techBullets(s, [
    "多线程额外开销主要来自 ring index/copy 和 atomic publish——共享状态竞争逐步放大",
    "notify/consumer 交互在 8T/16T 下显著抬头",
  ], 0.5, 4.6, 12, 1.5, 11);
})();

// Slide 4 — 优化路线
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.black };
  techHeader(s, 3, "Producer 端三项优化", "PER-RECORD OVERHEAD REDUCTION");

  const opts = [
    { n: "01", t: "NOTIFY OUTSIDE MUTEX", clr: C.cyan, items: ["eventfd写入移出writer锁临界区", "8T: 3.12s→2.85s    16T: 3.41s→2.98s", "减少持锁时间，多线程有效"] },
    { n: "02", t: "RECORD FILL OUTSIDE MUTEX", clr: C.green, items: ["record元数据填充移出锁", "4T: 3.51s→2.72s (22.5%)    8T: 3.64s→3.12s (14.3%)", "缩短临界区 = 减少竞争"] },
    { n: "03", t: "STAGE 6 BATCH PUBLISH ★", clr: C.orange, items: ["per-thread buffer → 一次拿锁批量写 → 一次publish", "1T: 1.55s→1.22s    8T: 3.12s→1.28s    16T: 3.41s→1.34s", "env: LNHV1_STAGE6_BATCH_SIZE=<1..64>"] },
  ];

  opts.forEach((o, i) => {
    const y = 1.25 + i * 1.95;
    s.addShape(pptx.ShapeType.roundRect, { x: 0.4, y, w: 12.5, h: 1.7, fill: { color: C.dark }, rectRadius: 0.06 });
    s.addText(o.n, { x: 0.6, y: y + 0.2, w: 0.8, h: 0.8, fontSize: 28, bold: true, color: o.clr, fontFace: "Consolas" });
    s.addText(o.t, { x: 1.6, y: y + 0.15, w: 6, h: 0.35, fontSize: 12, bold: true, color: o.clr, fontFace: "Consolas" });
    techBullets(s, o.items, 1.8, y + 0.55, 10.8, 1.0, 11);
  });
})();

// Slide 5 — Batch 数据
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.black };
  techHeader(s, 4, "Batch Publish — Pink Benchmark", "100万次 malloc/free pair  ·  Stage 6 full notify");

  const y = 1.3;
  metricKPI(s, "21.2%", "1T", "1 Thread", 0.3, y);
  metricKPI(s, "68.4%", "4T", "4 Threads", 3.5, y);
  metricKPI(s, "59.1%", "8T", "8 Threads", 6.7, y);
  metricKPI(s, "60.7%", "16T", "16 Threads", 9.9, y);

  techTable(s, [
    ["Threads", "No Batch", "Batch=64", "Δ"],
    ["1", "1.545s", "1.217s", "↓ 21.2%"],
    ["4", "2.718s", "0.859s", "↓ 68.4%"],
    ["8", "3.121s", "1.278s", "↓ 59.1%"],
    ["16", "3.408s", "1.340s", "↓ 60.7%"],
  ], 0.4, 2.5, 12.4);

  techBullets(s, [
    "Batch-size sweep (8T): 4→1.83s  8→1.58s  16→1.44s  32→1.36s  64→1.26s",
    "Default-off 确认无误: env未设时 8T=3.39s, 16T=3.36s",
  ], 0.5, 4.8, 12, 1.5, 11);
})();

// Slide 6 — 模块级
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.black };
  techHeader(s, 5, "StackWriter 模块级瓶颈定位", "SUB-STAGE 34/35  ·  RING WRITE vs EVENTFD");

  techTable(s, [
    ["Sub", "测量内容", "1T", "4T", "8T", "16T"],
    ["34 write_only", "ring write + inner mutex", "8.60s", "20.94s", "27.40s", "32.25s"],
    ["35 flush_only", "write + eventfd", "9.15s", "19.60s", "23.94s", "31.97s"],
    ["33 batch64", "批处理后完整链", "1.24s", "—", "1.26s", "1.37s"],
  ], 0.4, 1.25, 12.4);

  techBullets(s, [
    "RING WRITE 内锁占比 ~97% (8.60s vs 9.15s) —— EVENTFD 仅 ~3%",
    "per-record 锁竞争: 8T 比 1T 慢 3.2x",
    "BATCH64: 1T 9.15s→1.24s (7.4x)    8T 23.94s→1.26s (19x)",
    "对标: ShareMemoryBlock::PutWithPayloadTimeout / EventNotifier::Post",
  ], 0.5, 4.6, 12, 2, 11);
})();

// Slide 7 — 架构对齐
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.black };
  techHeader(s, 6, "Prototype ↔ OpenHarmony 架构对齐", "HOT PATH MAPPING");

  techTable(s, [
    ["操作", "Prototype (Plan B)", "OpenHarmony (hook_client.cpp)"],
    ["malloc/filter/sample", "hook_writer", "hook_malloc"],
    ["re-entry guard", "HookReentryGuard", "__set_hook_flag"],
    ["StackRawData fill", "simplified", "rawdata.{pid,tid,size,addr,ts}"],
    ["AddressHandler", "address_handler.h  [NEW]", "AddAllocAddr()"],
    ["StackWriter write", "stack_writer.cpp  [NEW]", "WriteWithPayloadTimeout"],
    ["StackWriter flush", "Flush / FlushEventFd  [NEW]", "Flush → EventNotifier::Post"],
  ], 0.4, 1.25, 12.4, 0.6);
})();

// Slide 8 — Fork
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.black };
  techHeader(s, 7, "OpenHarmony Fork — 移植状态", "gitlab.youtune.tech/cychi/cyc_nativehook");

  techTable(s, [
    ["函数", "场景", "fill-before-lock", "batch"],
    ["hook_malloc", "主分配", "✓", "✓"],
    ["hook_calloc", "C++ new", "✓", "✓"],
    ["hook_realloc", "vector扩容 (free+alloc)", "✓", "✓"],
    ["hook_aligned_alloc", "对齐分配", "✓", "—"],
    ["hook_free", "释放", "✓", "✓"],
  ], 0.4, 1.25, 12.4);

  techBullets(s, [
    "待 OpenHarmony 编译环境到位后可 benchmark 验证",
    "未移植: PID/TID cache (已有) / tracking fallback (架构不同)",
  ], 0.5, 4.2, 12, 1.5, 11);
})();

// Slide 9 — 教训
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.black };
  techHeader(s, 8, "方法论: 三步预检", "LESSONS LEARNED & PRE-FLIGHT CHECKLIST");

  // Mistakes
  s.addShape(pptx.ShapeType.roundRect, { x: 0.3, y: 1.3, w: 5.8, h: 4.5, fill: { color: C.pinkBg }, rectRadius: 0.06 });
  s.addText("FAILED OPTIMIZATIONS", { x: 0.6, y: 1.5, w: 4, h: 0.3, fontSize: 10, bold: true, color: C.pink, fontFace: "Consolas" });
  techBullets(s, [
    "PID/TID cache → 真实代码已有 pthread_getspecific + atomic load",
    "tracking fallback → free不查producer端表, 优化不存在的瓶颈",
  ], 0.7, 2.0, 5, 3, 11);

  // Method
  s.addShape(pptx.ShapeType.roundRect, { x: 6.5, y: 1.3, w: 6.5, h: 4.5, fill: { color: C.greenBg }, rectRadius: 0.06 });
  s.addText("3-STEP PRE-FLIGHT  [AGENTS.md]", { x: 6.8, y: 1.5, w: 5, h: 0.3, fontSize: 10, bold: true, color: C.green, fontFace: "Consolas" });
  techBullets(s, [
    "① 画映射表: 原型模块 ↔ 真实代码 函数+行号",
    "② 结构对齐: 补缺失模块, 数据精确对标",
    "③ 三问预检:",
    "   Q1 改哪个模块?  Q2 对哪个函数?  Q3 走这条路径?",
    "   答不上来 → 不动手",
  ], 6.9, 2.0, 5.8, 3.5, 11);
})();

// Slide 10 — Next
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.black };
  techHeader(s, 9, "Next Steps", "ROADMAP");

  [
    { n: "01", t: "Producer 继续拆解", d: "consumer侧 profiling · 完善 sub-stage 36 完整链测量", c: C.cyan },
    { n: "02", t: "真实代码验证", d: "等编译环境 → GitLab fork benchmark → 合 master", c: C.green },
    { n: "03", t: "eBPF 线 (如需)", d: "上次遗留: 4T异常快 → 更高线程数重复实验", c: C.purple },
  ].forEach((it, i) => {
    const y = 1.5 + i * 1.9;
    s.addShape(pptx.ShapeType.roundRect, { x: 1.0, y, w: 11.3, h: 1.5, fill: { color: C.dark }, rectRadius: 0.06 });
    s.addShape(pptx.ShapeType.rect, { x: 1.0, y, w: 0.1, h: 1.5, fill: { color: it.c } });
    s.addText(it.n, { x: 1.3, y: y + 0.2, w: 0.8, h: 0.8, fontSize: 30, bold: true, color: it.c, fontFace: "Consolas" });
    s.addText(it.t, { x: 2.3, y: y + 0.2, w: 5, h: 0.35, fontSize: 14, bold: true, color: C.white });
    s.addText(it.d, { x: 2.3, y: y + 0.65, w: 9, h: 0.5, fontSize: 11, color: C.gray });
  });
})();

// ==================== OUTPUT ====================
pptx.writeFile({ fileName: "native_hook_progress_2026-06-05.pptx" }).then(() => {
  console.log("OK");
});
