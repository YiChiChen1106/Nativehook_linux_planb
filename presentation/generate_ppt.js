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
  hdr(s, 2, "上次组会反馈闭环", "四个问题，逐一回应");

  const cards = [
    { tag: "RESOLVED", c: C.green, t: "▸ \"4T比1T多1.8s\"", b: "ring共享状态竞争\n→ batch解决后 4T: 2.72s → 0.86s" },
    { tag: "UPGRADED", c: C.blue, t: "▸ \"跑一下perf\"", b: "ablation替代perf\n→ sub-stage 34/35 拆到模块内部" },
    { tag: "COMPLETED", c: C.orange, t: "▸ \"fork Gitee→GitLab\"", b: "5个核心函数全加batch\n→ gitlab.youtune.tech/cychi/cyc_nativehook" },
    { tag: "VERIFIED", c: C.orange, t: "▸ \"4T eBPF异常快\"", b: "高线程验证: 8T eBPF=1.71s vs LD=4.27s (2.5x)\n16T eBPF=1.47s vs LD=4.39s (3.0x)\neBPF per-CPU ringbuf 无共享竞争 → 详见 Slide 6" },
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
  hdr(s, 3, "Writer/Ring Impact 拆解实验", "sub-stage 28~33 逐段测量，固定100万次malloc/free pair");

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
  hdr(s, 4, "Producer 端两项优化", "缩短 per-record 共享路径开销");

  [
    { n: "01", t: "Record Fill Outside Writer Mutex", c: C.blue, lines: [
      "移出锁的操作: pid, tid, timestamp, type, size, addr 填充 + thread-name 更新",
      "这些操作不碰共享状态——pid/tid 是 thread-local cache，timestamp 是 clock_gettime",
      "原来锁内做: lock → fill → write_ring → notify → unlock",
      "优化后: fill → lock → write_ring → notify → unlock",
      "4T: 3.51s→2.72s (22.5%)    8T: 3.64s→3.12s (14.3%)",
      "核心收益: 缩短锁内工作量 → 减少其他线程排队等锁时间",
    ]},
    { n: "02", t: "Stage 6  Batch  Publish", c: C.orange, lines: [
      "per-thread buffer (array<HookRecord, 65>) + counter",
      "BufferStage6Record 累积 record; count >= batch_size → FlushStage6Batch",
      "Flush 内: 拿一次锁 → 批量写 ring → 一次 atomic publish → 一次 notify",
      "析构函数在线程退出时自动 Flush 残留",
      "env gate: LNHV1_STAGE6_BATCH_SIZE=1~64（默认0=关闭）",
      "效果: 1T 1.55s→1.22s    8T 3.12s→1.28s    16T 3.41s→1.34s",
    ]},
  ].forEach((o, i) => {
    const y = 1.4 + i * 2.7;
    s.addShape(pptx.ShapeType.roundRect, { x: 0.3, y, w: 12.7, h: 2.7, fill: { color: C.white }, rectRadius: 0.1, line: { color: o.c, width: 1.2 } });
    s.addShape(pptx.ShapeType.rect, { x: 0.3, y, w: 0.12, h: 2.7, fill: { color: o.c } });
    s.addText(o.n, { x: 0.65, y: y + 0.25, w: 0.7, h: 0.7, fontSize: 30, bold: true, color: o.c, fontFace: "Consolas" });
    s.addText(o.t, { x: 1.5, y: y + 0.2, w: 6, h: 0.4, fontSize: 15, bold: true, color: C.ink });
    blt(s, o.lines, 1.7, y + 0.7, 10.8, 1.7, 12);
  });
})();

// 5 — Batch 可视化
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 5, "Batch Publish — 工作原理", "Per-Record  vs  Per-Batch");

  const L = C.line, B = C.blue, G = C.green, O = C.orange, W = C.white, D = C.dim, I = C.ink;

  function rect(x, y, w, h, c, txt, fs, fc) {
    s.addShape(pptx.ShapeType.roundRect, { x, y, w, h, fill: { color: c }, rectRadius: 0.04, line: { color: L, width: 0.5 } });
    if (txt) s.addText(txt, { x, y, w, h, fontSize: fs||9, color: fc||W, align:"center", valign:"middle", bold:true });
  }
  function arrow(x1, y1, x2, y2) {
    s.addShape(pptx.ShapeType.line, { x: x1, y: y1, w: x2-x1, h: y2-y1, line: { color: D, width: 1.2, endArrowType: "triangle" } });
  }
  function arrowR(x, y, w) {
    s.addShape(pptx.ShapeType.line, { x, y: y+0.05, w, h: 0, line: { color: D, width: 1.2, endArrowType: "triangle" } });
  }

  // === LEFT: Per-Record (before) ===
  s.addText("Per-Record（优化前）", { x: 0.3, y: 1.25, w: 5.8, h: 0.35, fontSize: 13, bold: true, color: I });
  const ry = 1.75;
  for (let i = 0; i < 3; i++) {
    const y = ry + i * 0.75;
    rect(0.3, y, 1.0, 0.45, B, "record " + (i+1), 9, W);
    arrowR(1.3, y, 0.4);
    rect(1.7, y, 0.7, 0.45, "D0D8E8", "lock", 8, D);
    arrowR(2.4, y, 0.4);
    rect(2.8, y, 0.9, 0.45, "D0D8E8", "write", 8, D);
    arrowR(3.7, y, 0.4);
    rect(4.1, y, 0.9, 0.45, "D0D8E8", "notify", 8, D);
  }
  rect(0.3, ry + 3 * 0.75 + 0.15, 1.0, 0.35, "F0F0F8", "...", 9, D);
  rect(0.3, ry + 3 * 0.75 + 0.6, 4.7, 0.35, C.blueBg, "record → 每 percord 走一遍拿锁、写ring、通知", 9, D);

  // === RIGHT: Per-Batch (after) ===
  s.addText("Per-Batch（batch=64 优化后）", { x: 7.2, y: 1.25, w: 5.8, h: 0.35, fontSize: 13, bold: true, color: I });
  const ry2 = 1.75;
  for (let i = 0; i < 3; i++) {
    rect(7.2, ry2 + i * 0.75, 1.0, 0.45, G, "record " + (i+1), 9, W);
  }
  rect(7.2, ry2 + 3 * 0.75 + 0.15, 1.0, 0.35, "F0F0F8", "...", 9, D);
  rect(7.2, ry2 + 3 * 0.75 + 0.15 + 0.15, 1.0, 0.45, G, "record 64", 9, W);

  // Buffer box
  const bx = 8.5, by = 1.9, bw = 1.6, bh = 3.1;
  s.addShape(pptx.ShapeType.roundRect, { x: bx, y: by, w: bw, h: bh, fill: { color: "F0FDF4" }, rectRadius: 0.06, line: { color: G, width: 1.5, dashType:"dash" } });
  s.addText("buffer\n64 records", { x: bx, y: by+0.5, w: bw, h: 0.8, fontSize: 11, bold: true, color: G, align:"center" });
  s.addText("count++", { x: bx, y: by+1.3, w: bw, h: 0.4, fontSize: 8, color: D, align:"center" });
  s.addText("满 64 → flush", { x: bx, y: by+1.8, w: bw, h: 0.4, fontSize: 9, bold: true, color: O, align:"center" });
  s.addText("析构 → flush 残留", { x: bx, y: by+2.3, w: bw, h: 0.4, fontSize: 9, color: D, align:"center" });

  // Arrows into buffer
  for (let i = 0; i < 3; i++) {
    arrow(8.2, ry2 + i * 0.75 + 0.22, bx, by + 0.5 + i * 0.5);
  }

  // After buffer: one lock + write + notify
  const ay = by + 0.3;
  arrow(bx + bw, ay + 0.15, 10.4, ay + 0.15);
  rect(10.4, ay - 0.1, 0.9, 0.5, W, "lock\n1次", 8, O);
  arrowR(11.3, ay, 0.3);
  rect(11.6, ay - 0.1, 0.9, 0.5, W, "write\n64条", 8, O);
  arrowR(12.6, ay - 0.1, 0.15); // wrap

  // Second line: publish + notify
  const a2y = ay + 1.2;
  arrow(bx + bw, a2y + 0.15, 10.4, a2y + 0.15);
  rect(10.4, a2y - 0.1, 1.2, 0.5, W, "publish\n1次", 8, O);
  arrowR(11.6, a2y, 0.3);
  rect(11.9, a2y - 0.1, 0.9, 0.5, W, "notify\n1次", 8, O);

  // ===== Summary row =====
  rect(0.3, 6.2, 12.6, 0.65, C.orangeBg, "", 0, W);
  s.addText("即：100万次锁操作 → 1.5万次锁操作    100万次 atomic publish → 1.5万次    ~5万次 eventfd → ~780次", {
    x: 0.5, y: 6.2, w: 12.2, h: 0.65, fontSize: 13, color: W, align: "center", valign: "middle", bold: true,
  });
})();

// 6 — Batch 数据
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 6, "Batch Publish — Pink 验证数据", "100万次固定负载  ·  Stage 6 full notify");

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

// 7 — eBPF 高线程
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 7, "eBPF 高线程验证 — 反超 LD_PRELOAD", "per-CPU ringbuf 无共享竞争，高线程下优势显著");

  tbl(s, [
    ["Threads", "Run", "eBPF uprobe", "LD_PRELOAD optimized", "eBPF 快多少"],
    ["8", "r1", "1.76s", "4.19s", "2.4x"],
    ["8", "r2", "1.67s", "4.27s", "2.6x"],
    ["8", "r3", "1.71s", "4.34s", "2.5x"],
    ["16", "r1", "1.47s", "4.38s", "3.0x"],
    ["16", "r2", "1.47s", "4.39s", "3.0x"],
    ["16", "r3", "1.48s", "4.40s", "3.0x"],
  ], 0.3, 1.3, 12.6);

  blt(s, [
    "上次组会: 4T eBPF 比 LD_PRELOAD 慢 0.18s，结论是\"不适合替代\"",
    "新增 8T/16T 实验 (三次重复): eBPF 全面反超，16T 下快 3 倍",
    "原因: eBPF per-CPU ringbuf 无共享锁竞争，而 LD_PRELOAD batch 仍有 per-batch 锁",
    "结论: 低线程 LD_PRELOAD 更好，高线程 eBPF 天花板更高",
  ], 0.5, 4.8, 12, 2.2, 13);
})();

// 8 — 模块级
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 8, "StackWriter 模块级瓶颈定位", "sub-stage 34/35  ·  ring write 内锁 vs eventfd 开销分离");

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

// 9 — 架构
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 9, "Prototype  ↔  OpenHarmony  架构对齐", "热路径模块映射");

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

// 10 — Fork
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 10, "OpenHarmony Fork — 优化移植状态", "gitlab.youtune.tech/cychi/cyc_nativehook");

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

// 11 — 教训+三步法
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 11, "经验教训 & 三步预检方法", "两个无效优化如何发生，以及后续怎么避免");

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

// 12 — Next
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 12, "Next Steps");

  [
    { n: "01", t: "Producer 端继续拆解", d: "consumer 侧 profiling，完善 sub-stage 36 完整链测量数据", c: C.blue },
    { n: "02", t: "真实代码验证", d: "等待 OpenHarmony 编译环境 → GitLab fork benchmark → 合 master", c: C.green },
    { n: "03", t: "eBPF 高线程验证结论", d: "8T/16T 已确认：eBPF per-CPU ringbuf 反超 LD_PRELOAD 2.5~3x\n如需进一步：评估 producer 端 eBPF 替代方案", c: C.purple },
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
