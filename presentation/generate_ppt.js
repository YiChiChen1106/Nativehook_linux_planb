const PptxGenJS = require("pptxgenjs");

const pptx = new PptxGenJS();
pptx.layout = "LAYOUT_WIDE";
pptx.author = "陈一驰";
pptx.title = "native_hook Producer Hot Path Progress";

const C = {
  bg:       "F4F6FB",
  white:    "FFFFFF",
  ink:      "1A1A2E",
  dim:      "4A4A6A",
  faint:    "8888A8",
  line:     "E0E4EC",
  blue:     "2563EB",
  blueBg:   "EEF2FF",
  cyan:     "0891B2",
  cyanBg:   "ECFEFF",
  green:    "059669",
  greenBg:  "ECFDF5",
  orange:   "EA580C",
  orangeBg: "FFF7ED",
  purple:   "7C3AED",
  purpleBg: "F5F3FF",
  pink:     "DB2777",
  pinkBg:   "FDF2F8",
  gold:     "D97706",
  goldBg:   "FFFBEB",
};

function hdr(slide, num, title, sub) {
  slide.addShape(pptx.ShapeType.rect, { x: 0, y: 0, w: 13.33, h: 1.05, fill: { color: C.white } });
  slide.addShape(pptx.ShapeType.rect, { x: 0, y: 1.02, w: 13.33, h: 2/72, fill: { color: C.line } });
  slide.addText(String(num).padStart(2, "0"), {
    x: 0.4, y: 0.15, w: 0.8, h: 0.6,
    fontSize: 28, bold: true, color: C.blue, fontFace: "Consolas", align: "right",
  });
  slide.addText(title, {
    x: 1.4, y: 0.15, w: 10, h: 0.55,
    fontSize: 24, bold: true, color: C.ink,
  });
  if (sub) slide.addText(sub, {
    x: 1.4, y: 0.62, w: 10, h: 0.3,
    fontSize: 12, color: C.faint,
  });
}

function tbl(slide, rows, x, y, w) {
  const n = rows[0].length;
  const cw = (w || 12.3) / n;
  const hdrRow = rows[0].map((h, i) => ({
    text: h, options: { bold: true, fontSize: 12, color: C.blue, fill: { color: C.blueBg }, align: i === 0 ? "left" : "center" }
  }));
  const datRows = rows.slice(1).map((r, ri) =>
    r.map((c, ci) => ({
      text: String(c), options: {
        fontSize: 12, color: ci === n - 1 ? C.green : C.dim,
        fill: { color: ri % 2 ? C.bg : C.white }, align: ci === 0 ? "left" : "center",
      }
    }))
  );
  slide.addTable([hdrRow, ...datRows], {
    x: x || 0.4, y: y || 1.35, w: w || 12.4,
    border: { type: "solid", color: C.line, pt: 0.5 },
    colW: Array(n).fill(cw), rowH: 0.42, margin: [2, 6, 2, 6],
  });
}

function blt(slide, lines, x, y, w, h, fs) {
  slide.addText(lines.map(t => ({
    text: t, options: { bullet: { code: "2022" }, fontSize: fs || 14, color: C.dim, breakType: "none", paraSpaceAfter: 4 }
  })), {
    x: x || 0.6, y: y || 1.4, w: w || 12, h: h || 5, lineSpacing: 24, valign: "top",
  });
}

function big(slide, val, sub, x, y) {
  slide.addText(val, { x, y, w: 2.5, h: 0.9, fontSize: 46, bold: true, color: C.blue, align: "center", fontFace: "Consolas" });
  slide.addText(sub, { x, y: y + 0.85, w: 2.5, h: 0.3, fontSize: 11, color: C.faint, align: "center" });
}

function card(slide, tag, title, body, x, y, w, h, color) {
  slide.addShape(pptx.ShapeType.roundRect, { x, y, w, h, fill: { color: C.white }, rectRadius: 0.1, line: { color: color, width: 1.2, dashType: "solid" } });
  slide.addShape(pptx.ShapeType.rect, { x: x, y: y, w: w, h: 3/72, fill: { color: color } });
  slide.addText(tag, { x: x + 0.15, y: y + 0.15, w: 1.5, h: 0.28, fontSize: 10, bold: true, color: color, fontFace: "Consolas" });
  slide.addText(title, { x: x + 0.15, y: y + 0.48, w: w - 0.3, h: 0.45, fontSize: 15, bold: true, color: C.ink });
  slide.addText(body, { x: x + 0.15, y: y + 1.0, w: w - 0.3, h: h - 1.2, fontSize: 12, color: C.dim, lineSpacing: 20 });
}

// === SLIDES ===

// 1 — Title
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  s.addText("native_hook", {
    x: 1, y: 1.8, w: 11, h: 1.2,
    fontSize: 72, bold: true, color: C.ink, align: "center",
  });
  s.addText("Producer Hot Path 优化进展", {
    x: 1, y: 3.1, w: 11, h: 0.6,
    fontSize: 32, color: C.blue, align: "center",
  });
  s.addShape(pptx.ShapeType.rect, { x: 4.5, y: 3.7, w: 4.3, h: 3/72, fill: { color: C.orange } });
  s.addText("2026.06.05  组会汇报  ·  陈一驰", {
    x: 1, y: 4.1, w: 11, h: 0.4,
    fontSize: 16, color: C.faint, align: "center",
  });
})();

// 2 — 反馈闭环
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 1, "上次组会反馈闭环", "四个问题，逐一回应");

  const cards = [
    { tag: "RESOLVED", c: C.green, t: "▸ \"4T比1T多1.8s\"", b: "ring共享状态竞争\n→ batch解决后 4T: 2.72s → 0.86s" },
    { tag: "UPGRADED", c: C.blue, t: "▸ \"跑一下perf\"", b: "ablation替代perf\n→ sub-stage 34/35 拆到模块内部" },
    { tag: "COMPLETED", c: C.orange, t: "▸ \"fork Gitee→GitLab\"", b: "5个核心函数全加batch\n→ gitlab.youtune.tech/cychi/cyc_nativehook" },
    { tag: "EXPLAINED", c: C.purple, t: "▸ \"4T eBPF异常快\"", b: "固定总负载指标，多线程并行就该比单线程快\neBPF单线程开销重，4T并行优势体现\n本轮聚焦producer端，eBPF暂停" },
  ];

  cards.forEach((c, i) => {
    const col = i % 2, row = Math.floor(i / 2);
    card(s, c.tag, c.t, c.b, 0.35 + col * 6.35, 1.3 + row * 2.85, 6.1, 2.55, c.c);
  });
})();

// 3 — Writer/Ring
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 2, "Writer/Ring Impact 拆解实验", "sub-stage 28~33 逐段测量，固定100万次malloc/free pair");

  tbl(s, [
    ["Sub-stage", "测量内容", "1T", "4T", "8T", "16T"],
    ["28 no_ring", "基线（无ring操作）", "—", "—", "—", "—"],
    ["29 mutex_only", "仅拿锁 + tracking", "0.28s", "0.62s", "0.85s", "1.12s"],
    ["30 ring_index", "+ ring index检查", "0.45s", "1.35s", "1.92s", "2.48s"],
    ["31 record_copy", "+ 记录拷贝到共享内存", "0.68s", "2.10s", "2.85s", "3.52s"],
    ["32 atomic_pub", "+ atomic write_index发布", "0.75s", "2.35s", "3.10s", "3.80s"],
    ["33 full_notify", "+ eventfd + consumer", "1.24s", "2.72s", "3.12s", "3.41s"],
  ], 0.3, 1.3, 12.6);
  blt(s, [
    "多线程额外开销主要来自 ring index/copy 和 atomic publish——共享状态竞争逐步放大",
    "notify/consumer 交互在 8T/16T 下显著抬头，是第二大开销源",
  ], 0.5, 4.9, 12, 1.5, 11);
})();

// 4 — 优化路线
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 3, "Producer 端三项优化", "逐级降低 per-record 共享路径开销");

  [
    { n: "01", t: "Notify Outside Writer Mutex", c: C.cyan, lines: [
      "eventfd 写入移出 writer 锁临界区",
      "8T: 3.12s→2.85s    16T: 3.41s→2.98s",
      "减少持锁时间，多线程有效",
    ]},
    { n: "02", t: "Record Fill Outside Writer Mutex", c: C.blue, lines: [
      "record 元数据填充移出锁",
      "4T: 3.51s→2.72s (22.5%)    8T: 3.64s→3.12s (14.3%)",
      "缩短临界区 = 减少竞争窗口",
    ]},
    { n: "★", t: "Stage 6  Batch  Publish", c: C.orange, lines: [
      "per-thread buffer → 一次拿锁批量写 → 一次 atomic publish → 一次 notify",
      "1T: 1.55s→1.22s    8T: 3.12s→1.28s    16T: 3.41s→1.34s",
      "env gate: LNHV1_STAGE6_BATCH_SIZE = <1..64>",
    ]},
  ].forEach((o, i) => {
    const y = 1.3 + i * 1.95;
    s.addShape(pptx.ShapeType.roundRect, { x: 0.3, y, w: 12.7, h: 1.7, fill: { color: C.white }, rectRadius: 0.1, line: { color: o.c, width: 1.2 } });
    s.addShape(pptx.ShapeType.rect, { x: 0.3, y, w: 0.12, h: 1.7, fill: { color: o.c } });
    s.addText(o.n, { x: 0.65, y: y + 0.2, w: 0.7, h: 0.7, fontSize: 30, bold: true, color: o.c, fontFace: "Consolas" });
    s.addText(o.t, { x: 1.5, y: y + 0.15, w: 6, h: 0.4, fontSize: 15, bold: true, color: C.ink });
    blt(s, o.lines, 1.7, y + 0.55, 10.8, 1.0, 11);
  });
})();

// 5 — Batch 数据
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 4, "Batch Publish — Pink 验证数据", "100万次固定负载  ·  Stage 6 full notify");

  big(s, "21.2%", "1 Thread", 0.3, 1.35);
  big(s, "68.4%", "4 Threads", 3.5, 1.35);
  big(s, "59.1%", "8 Threads", 6.7, 1.35);
  big(s, "60.7%", "16 Threads", 9.9, 1.35);

  tbl(s, [
    ["Threads", "No Batch", "Batch = 64", "提升"],
    ["1", "1.545s", "1.217s", "↓ 21.2%"],
    ["4", "2.718s", "0.859s", "↓ 68.4%"],
    ["8", "3.121s", "1.278s", "↓ 59.1%"],
    ["16", "3.408s", "1.340s", "↓ 60.7%"],
  ], 0.3, 2.55, 12.6);
  blt(s, [
    "Batch-size sweep (8T): 4→1.83s  8→1.58s  16→1.44s  32→1.36s  64→1.26s",
    "Default-off 确认: env未设时 8T=3.39s, 16T=3.36s（行为不变）",
  ], 0.5, 4.8, 12, 1.5, 11);
})();

// 6 — 模块级
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 5, "StackWriter 模块级瓶颈定位", "sub-stage 34/35  ·  ring write 内锁 vs eventfd 开销分离");

  tbl(s, [
    ["Sub-stage", "测量内容", "1T", "4T", "8T", "16T"],
    ["34 write_only", "ring write + inner mutex", "8.60s", "20.94s", "27.40s", "32.25s"],
    ["35 flush_only", "write + eventfd 通知", "9.15s", "19.60s", "23.94s", "31.97s"],
    ["33 + batch64", "批处理后完整链路", "1.24s", "—", "1.26s", "1.37s"],
  ], 0.3, 1.3, 12.6);
  blt(s, [
    "RING WRITE 内锁占比 ~97%（8.60s vs 9.15s）— eventfd 仅 ~3%，内锁是明确主瓶颈",
    "per-record 锁竞争随线程恶化：8T write 比 1T 慢 3.2 倍",
    "BATCH64 消除 per-record 锁: 1T 9.15s→1.24s (7.4x)   8T 23.94s→1.26s (19x)",
    "对标真实代码: ShareMemoryBlock::PutWithPayloadTimeout / EventNotifier::Post",
  ], 0.5, 4.8, 12, 2, 11);
})();

// 7 — 架构
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 6, "Prototype  ↔  OpenHarmony  架构对齐", "热路径模块映射");

  tbl(s, [
    ["热路径操作", "Prototype (Plan B)", "OpenHarmony (hook_client.cpp)"],
    ["malloc / filter / sample", "hook_writer", "hook_malloc"],
    ["re-entry guard", "HookReentryGuard", "__set_hook_flag"],
    ["StackRawData fill", "simplified", "rawdata.{pid, tid, size, addr, ts}"],
    ["AddressHandler", "address_handler.h  [NEW]", "AddAllocAddr()"],
    ["StackWriter write", "stack_writer.cpp  [NEW]", "WriteWithPayloadTimeout"],
    ["StackWriter flush", "Flush / FlushEventFd  [NEW]", "Flush → EventNotifier::Post"],
  ], 0.3, 1.3, 12.6, 0.65);
})();

// 8 — Fork
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 7, "OpenHarmony Fork — 优化移植状态", "gitlab.youtune.tech/cychi/cyc_nativehook");

  tbl(s, [
    ["函数", "调用场景", "record-fill-before-lock", "batch publish"],
    ["hook_malloc", "主分配路径", "✓", "✓"],
    ["hook_calloc", "C++ new 底层调用", "✓", "✓"],
    ["hook_realloc", "vector 扩容（free+alloc）", "✓", "✓"],
    ["hook_aligned_alloc", "对齐分配", "✓", "—"],
    ["hook_free", "释放路径", "✓", "✓"],
  ], 0.3, 1.3, 12.6);
  blt(s, [
    "待 OpenHarmony 编译环境到位后可 benchmark 验证",
    "未移植: PID/TID cache（已有）、tracking fallback（架构不同）",
  ], 0.5, 4.2, 12, 1.5, 11);
})();

// 9 — 教训+三步法
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 8, "经验教训 & 三步预检方法", "两个无效优化如何发生，以及后续怎么避免");

  // Left
  s.addShape(pptx.ShapeType.roundRect, { x: 0.3, y: 1.35, w: 5.85, h: 4.5, fill: { color: C.pinkBg }, rectRadius: 0.1, line: { color: C.pink, width: 1.2 } });
  s.addText("失败的优化", { x: 0.55, y: 1.5, w: 4, h: 0.35, fontSize: 16, bold: true, color: C.pink });
  blt(s, [
    "PID/TID cache",
    "真实代码已有 pthread_getspecific + atomic load",
    "优化方向正确，但冗余",
    "",
    "tracking fallback",
    "free 记录直接发给 daemon 做匹配",
    "producer 端不维护查表路径",
    "优化了一个原型里才存在的瓶颈",
  ], 0.55, 1.85, 5.2, 3.8, 11);

  // Right
  s.addShape(pptx.ShapeType.roundRect, { x: 6.45, y: 1.35, w: 6.55, h: 4.5, fill: { color: C.greenBg }, rectRadius: 0.1, line: { color: C.green, width: 1.2 } });
  s.addText("三步预检法  [AGENTS.md]", { x: 6.7, y: 1.5, w: 5, h: 0.35, fontSize: 16, bold: true, color: C.green });
  blt(s, [
    "①  画映射表",
    "    原型每个模块 ↔ 真实代码 函数+行号",
    "",
    "②  结构对齐",
    "    原型补缺失模块，数据精确对标",
    "",
    "③  三问预检",
    "    Q1: 改哪个原型模块？",
    "    Q2: 对应哪个真实函数？",
    "    Q3: 真实代码走不走这条路径？",
    "    → 答不上来，不动手",
  ], 6.7, 1.85, 5.8, 3.8, 11);
})();

// 10 — Next
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 9, "Next Steps");

  [
    { n: "01", t: "Producer 端继续拆解", d: "consumer 侧 profiling，完善 sub-stage 36 完整链测量数据", c: C.blue },
    { n: "02", t: "真实代码验证", d: "等待 OpenHarmony 编译环境 → GitLab fork benchmark → 合 master", c: C.green },
    { n: "03", t: "eBPF 线（如需继续）", d: "上次遗留: 4T 异常快 → 更高线程数重复实验确认", c: C.purple },
  ].forEach((it, i) => {
    const y = 1.5 + i * 1.85;
    s.addShape(pptx.ShapeType.roundRect, { x: 1.0, y, w: 11.3, h: 1.5, fill: { color: C.white }, rectRadius: 0.1, line: { color: it.c, width: 1.5 } });
    s.addShape(pptx.ShapeType.rect, { x: 1.0, y, w: 0.12, h: 1.5, fill: { color: it.c } });
    s.addText(it.n, { x: 1.35, y: y + 0.15, w: 0.8, h: 0.8, fontSize: 34, bold: true, color: it.c, fontFace: "Consolas" });
    s.addText(it.t, { x: 2.3, y: y + 0.2, w: 5, h: 0.4, fontSize: 16, bold: true, color: C.ink });
    s.addText(it.d, { x: 2.3, y: y + 0.7, w: 9, h: 0.5, fontSize: 13, color: C.dim });
  });
})();

pptx.writeFile({ fileName: "native_hook_progress_2026-06-05.pptx" }).then(() => console.log("OK"));
