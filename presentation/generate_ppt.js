const PptxGenJS = require("pptxgenjs");

const pptx = new PptxGenJS();
pptx.layout = "LAYOUT_WIDE";
pptx.author = "陈一驰";
pptx.title = "native_hook Producer Hot Path Progress";

// ====== COLOR PALETTE ======
const C = {
  navy:      "1B1B3A",
  darkNavy:  "0D0D24",
  teal:      "00A8A8",
  accent:    "FF6B35",
  green:     "2ECC71",
  red:       "E74C3C",
  yellow:    "F1C40F",
  white:     "FFFFFF",
  offWhite:  "F7F9FC",
  lightGray: "ECF0F1",
  midGray:   "95A5A6",
  darkGray:  "2C3E50",
  textDark:  "1a1a2e",
  textMid:   "4A4A6A",
};

// ====== SLIDE MASTER ======
const MASTER = {
  HEADER_H: 0.85,
  HEADER_Y: 0,
  SIDEBAR_W: 0.12,
  BODY_X: 0.7,
  BODY_Y: 1.2,
  BODY_W: 12.2,
};

// ====== HELPERS ======
function addHeader(slide, title, subtitle) {
  slide.addShape(pptx.ShapeType.rect, {
    x: 0, y: 0, w: 13.33, h: MASTER.HEADER_H,
    fill: { color: C.navy },
  });
  slide.addShape(pptx.ShapeType.rect, {
    x: 0, y: MASTER.HEADER_H - 3/72, w: 13.33, h: 3/72,
    fill: { color: C.teal },
  });
  slide.addText(title, {
    x: 0.5, y: 0.1, w: 12, h: 0.5, fontSize: 22, bold: true, color: C.white,
  });
  if (subtitle) {
    slide.addText(subtitle, {
      x: 0.5, y: 0.5, w: 12, h: 0.3, fontSize: 11, color: C.midGray,
    });
  }
  slide.addText("native_hook Plan B", {
    x: 10.5, y: 0.15, w: 2.5, h: 0.4, fontSize: 10, color: C.teal, align: "right", italic: true,
  });
}

function addBullet(slide, items, opts = {}) {
  const x = opts.x || MASTER.BODY_X;
  const y = opts.y || MASTER.BODY_Y;
  const w = opts.w || MASTER.BODY_W;
  const h = opts.h || 5.2;
  const fs = opts.fs || 15;
  slide.addText(items.map(t => {
    if (typeof t === "string") return { text: t, options: { bullet: true, fontSize: fs } };
    return t;
  }), {
    x, y, w, h, fontSize: fs, color: C.textDark, lineSpacing: 26, valign: "top",
    paraSpaceAfter: 6,
  });
}

function addKeyBox(slide, text, x, y, w, color) {
  slide.addShape(pptx.ShapeType.roundRect, {
    x, y, w, h: 0.55,
    fill: { color: color || C.teal }, rectRadius: 0.08,
  });
  slide.addText(text, {
    x: x + 0.15, y, w: w - 0.3, h: 0.55,
    fontSize: 14, bold: true, color: C.white, valign: "middle",
  });
}

function metricTable(slide, rows, opts = {}) {
  const x = opts.x || 0.4;
  const y = opts.y || MASTER.BODY_Y;
  const w = opts.w || 12.5;
  const header = rows[0];
  const data = rows.slice(1);
  const colW = header.map(() => w / header.length);

  const headerRow = header.map((h, i) => ({
    text: h, options: {
      bold: true, color: C.white, fill: { color: C.navy }, fontSize: 11,
      align: i === 0 ? "left" : "center",
    }
  }));

  const dataRows = data.map((row, ri) =>
    row.map((c, ci) => ({
      text: String(c), options: {
        fontSize: 11, color: C.textDark,
        fill: { color: ri % 2 === 0 ? C.offWhite : C.white },
        align: ci === 0 ? "left" : "center",
        bold: ci === row.length - 1 && row[ci] ? true : false,
      }
    }))
  );

  slide.addTable([headerRow, ...dataRows], {
    x, y, w,
    border: { type: "solid", color: C.lightGray, pt: 0.5 },
    colW, rowH: 0.38,
    margin: [2, 6, 2, 6],
  });
}

function addCard(slide, title, body, x, y, w, h, color) {
  slide.addShape(pptx.ShapeType.roundRect, {
    x, y, w, h, fill: { color: C.white },
    shadow: { type: "outer", blur: 6, offset: 1, color: "000000", opacity: 0.1 },
    rectRadius: 0.08,
  });
  slide.addShape(pptx.ShapeType.rect, {
    x: x, y: y, w: w, h: 4/72, fill: { color: color || C.teal },
  });
  slide.addText(title, {
    x: x + 0.2, y: y + 0.1, w: w - 0.4, h: 0.4,
    fontSize: 14, bold: true, color: C.navy,
  });
  slide.addText(body, {
    x: x + 0.2, y: y + 0.5, w: w - 0.4, h: h - 0.6,
    fontSize: 11, color: C.textMid, lineSpacing: 18,
  });
}

function addFooter(slide) {
  slide.addText("陈一驰 | 2026-06-05 组会", {
    x: 0.5, y: 7.0, w: 12, h: 0.3, fontSize: 9, color: C.midGray, align: "center",
  });
}

// ==================== SLIDES ====================

// --- SLIDE 1: Title ---
{
  const s = pptx.addSlide();
  s.background = { fill: C.navy };
  s.addShape(pptx.ShapeType.rect, {
    x: 0, y: 0, w: 13.33, h: 7.5, fill: { color: C.navy },
  });

  s.addText("native_hook", {
    x: 1, y: 1.5, w: 11, h: 1.2,
    fontSize: 60, bold: true, color: C.white, align: "center",
    fontFace: "Arial",
  });
  s.addText("Producer Hot Path Progress", {
    x: 1, y: 2.6, w: 11, h: 0.8,
    fontSize: 28, color: C.teal, align: "center",
  });
  s.addShape(pptx.ShapeType.rect, {
    x: 4.5, y: 3.6, w: 4.3, h: 3/72, fill: { color: C.accent },
  });
  s.addText("2026-06-05  组会汇报", {
    x: 1, y: 4.1, w: 11, h: 0.5,
    fontSize: 16, color: C.midGray, align: "center",
  });
  s.addText("陈一驰", {
    x: 1, y: 4.7, w: 11, h: 0.5,
    fontSize: 14, color: C.midGray, align: "center",
  });
}

// --- SLIDE 2: 上次反馈闭环 ---
{
  const s = pptx.addSlide();
  s.background = { fill: C.offWhite };
  addHeader(s, "上次组会反馈闭环", "黄总上次提出的四个问题，逐一回应");

  const cards = [
    { title: '① "4T比1T多1.8s"', body: '已定位：ring共享状态竞争\n→ batch解决后 4T: 2.72s → 0.86s', color: C.green },
    { title: '② "跑一下perf"', body: '用更精确的ablation替代\n→ sub-stage 34/35 拆到模块内部', color: C.teal },
    { title: '③ "fork Gitee到GitLab"', body: '已完成：5个核心函数已加batch\n→ gitlab.youtune.tech/cychi/cyc_nativehook', color: C.accent },
    { title: '④ "eBPF异常"', body: '本轮聚焦producer端\n→ eBPF线暂停, 待后续', color: C.midGray },
  ];

  cards.forEach((c, i) => {
    const col = i % 2;
    const row = Math.floor(i / 2);
    addCard(s, c.title, c.body, 0.4 + col * 6.3, 1.2 + row * 2.8, 6.0, 2.5, c.color);
  });

  addFooter(s);
}

// --- SLIDE 3: Writer/Ring 拆解 ---
{
  const s = pptx.addSlide();
  s.background = { fill: C.offWhite };
  addHeader(s, "Writer/Ring Impact 拆解实验", "sub-stage 28~33 逐段测量，定位多线程瓶颈来源");

  metricTable(s, [
    ["Sub-stage", "测量内容", "1T", "4T", "8T", "16T"],
    ["28 no_ring", "基线（无ring操作）", "—", "—", "—", "—"],
    ["29 mutex_only", "仅拿锁 + tracking", "0.28s", "0.62s", "0.85s", "1.12s"],
    ["30 ring_index", "+ ring index检查", "0.45s", "1.35s", "1.92s", "2.48s"],
    ["31 record_copy", "+ 记录拷贝到共享内存", "0.68s", "2.10s", "2.85s", "3.52s"],
    ["32 atomic_pub", "+ atomic write_index发布", "0.75s", "2.35s", "3.10s", "3.80s"],
    ["33 full_notify", "+ eventfd通知 + consumer交互", "1.24s", "2.72s", "3.12s", "3.41s"],
  ], { y: 1.2 });

  addBullet(s, [
    "多线程额外开销主要来自 ring index/copy 和 atomic publish（共享状态竞争逐渐放大）",
    "notify/consumer 交互是额外大项，8T/16T 下显著",
  ], { y: 4.5, h: 2, fs: 13 });

  addFooter(s);
}

// --- SLIDE 4: 优化路线 ---
{
  const s = pptx.addSlide();
  s.background = { fill: C.offWhite };
  addHeader(s, "Producer 端优化路线", "三项优化，逐级降低 per-record 共享路径开销");

  const cards = [
    {
      title: "优化 1: Notify Outside Mutex",
      body: "Flush(eventfd) 移出 writer 锁临界区\n8T: 3.12s → 2.85s\n16T: 3.41s → 2.98s\n减少持锁时间，8T+有效",
      color: C.teal,
    },
    {
      title: "优化 2: Record Fill Outside Mutex",
      body: "record 元数据填充移出锁\n4T: 3.51s → 2.72s (22.5%)\n8T: 3.64s → 3.12s (14.3%)\n缩短临界区在多线程下明显",
      color: C.teal,
    },
    {
      title: "★ 优化 3: Stage 6 Batch Publish",
      body: "per-thread buffer → 批量拿锁写入\n一次 atomic publish → 一次 notify\n1T: 1.55s → 1.22s (+21%)\n8T: 3.12s → 1.28s (+59%)",
      color: C.accent,
    },
  ];

  cards.forEach((c, i) => {
    const col = i;
    addCard(s, c.title, c.body, 0.4 + col * 4.25, 1.2, 4.0, 3.2, c.color);
  });

  addBullet(s, [
    "Batching 是对比后收益最大的优化：单次持锁处理多条记录，大幅降低 per-record 竞争",
    "env gate: LNHV1_STAGE6_BATCH_SIZE=<1..64>（默认0=off）",
  ], { y: 4.8, h: 1.5, fs: 13 });

  addFooter(s);
}

// --- SLIDE 5: Batch Publish 数据 ---
{
  const s = pptx.addSlide();
  s.background = { fill: C.offWhite };
  addHeader(s, "Batch Publish — Pink 验证数据", "固定100万次malloc/free pair，Stage6 full notify 配置");

  metricTable(s, [
    ["Threads", "No Batch (record-fill-outside-lock)", "Batch = 64", "提升幅度"],
    ["1", "1.545s", "1.217s", "↓ 21.2%"],
    ["4", "2.718s", "0.859s", "↓ 68.4%"],
    ["8", "3.121s", "1.278s", "↓ 59.1%"],
    ["16", "3.408s", "1.340s", "↓ 60.7%"],
  ], { y: 1.2 });

  addBullet(s, [
    "Batch-size sweep (8T): batch4=1.83s  batch8=1.58s  batch16=1.44s  batch32=1.36s  batch64=1.26s",
    "Default-off check (env unset): 8T=3.39s, 16T=3.36s——行为不变",
    "结论：per-record 共享 ring 发布是剩余最大 Stage 6 扩展成本，batch 大幅缓解",
  ], { y: 4.0, h: 2.5, fs: 13 });

  addFooter(s);
}

// --- SLIDE 6: StackWriter 模块级数据 ---
{
  const s = pptx.addSlide();
  s.background = { fill: C.offWhite };
  addHeader(s, "StackWriter 模块级瓶颈定位", "sub-stage 34/35：分离 ring write 内锁 与 eventfd 开销");

  metricTable(s, [
    ["Sub-stage", "测量内容", "1T", "4T", "8T", "16T"],
    ["34 write_only", "ring write + inner mutex", "8.60s", "20.94s", "27.40s", "32.25s"],
    ["35 flush_only", "write + eventfd 通知", "9.15s", "19.60s", "23.94s", "31.97s"],
    ["33+batch64", "批处理后完整链", "1.24s", "—", "1.26s", "1.37s"],
  ], { y: 1.2 });

  addBullet(s, [
    "ring write 内锁占 ~97%（8.60s vs 9.15s），eventfd 仅 ~3%——内锁是明确主瓶颈",
    "per-record 锁竞争随线程恶化：8T write 比 1T 慢 3.2x",
    "batch64 消除 per-record 锁：1T 9.15s → 1.24s (7.4x), 8T 23.94s → 1.26s (19x)",
    "数据直接对标真实代码 ShareMemoryBlock::PutWithPayloadTimeout + EventNotifier::Post",
  ], { y: 4.2, h: 2.5, fs: 13 });

  addFooter(s);
}

// --- SLIDE 7: 架构对齐 ---
{
  const s = pptx.addSlide();
  s.background = { fill: C.offWhite };
  addHeader(s, "Prototype ↔ OpenHarmony 热路径对齐", "重构后原型结构与真实代码在模块级别可比");

  metricTable(s, [
    ["热路径操作", "Prototype (Plan B)", "OpenHarmony (hook_client.cpp)", "对齐"],
    ["malloc/filter/sample", "hook_writer", "hook_malloc", "✅"],
    ["re-entry guard", "HookReentryGuard", "__set_hook_flag", "✅"],
    ["StackRawData fill", "simplified", "rawdata.{pid,tid,size,addr,ts}", "⚠️"],
    ["AddressHandler tracking", "address_handler.h (新增)", "AddAllocAddr()", "✅"],
    ["StackWriter write", "stack_writer.cpp (新增)", "WriteWithPayloadTimeout", "✅"],
    ["StackWriter flush", "Flush / FlushEventFd (新增)", "Flush → EventNotifier::Post", "✅"],
    ["FpUnwind", "— (x86限制)", "FpUnwind() aarch64", "❌"],
  ], { y: 1.2 });

  addFooter(s);
}

// --- SLIDE 8: Fork 移植 ---
{
  const s = pptx.addSlide();
  s.background = { fill: C.offWhite };
  addHeader(s, "OpenHarmony Fork — 优化移植状态", "五个核心函数已全部覆盖 record-fill-before-lock + batch publish");

  metricTable(s, [
    ["函数", "调用场景", "record-fill-before-lock", "batch publish"],
    ["hook_malloc", "主分配路径", "✅", "✅"],
    ["hook_calloc", "C++ new 底层", "✅", "✅"],
    ["hook_realloc", "vector 扩容（free+alloc双记录）", "✅", "✅"],
    ["hook_aligned_alloc", "对齐分配", "✅", "—"],
    ["hook_free", "释放路径", "✅", "✅"],
  ], { y: 1.2 });

  addBullet(s, [
    "仓库: gitlab.youtune.tech/cychi/cyc_nativehook  (master分支)",
    "待 OpenHarmony 编译环境到位后可 benchmark 验证",
    "未移植: PID/TID cache（已有）、thread_local_fallback tracking（架构不同，不适用）",
  ], { y: 4.2, h: 2, fs: 13 });

  addFooter(s);
}

// --- SLIDE 9: 教训 + 三步法 ---
{
  const s = pptx.addSlide();
  s.background = { fill: C.offWhite };
  addHeader(s, "经验教训 & 优化方法论", "两个无效优化如何发生，以及后续怎么避免");

  // Left: mistakes
  addCard(s,
    "两个无效优化的教训",
    "✗ PID/TID cache\n　 真实代码已有 pthread_getspecific\n　 原型重复开发，移植时发现冗余\n\n✗ thread_local_fallback tracking\n　 真实代码 free 不查 producer 端表\n　 优化了一个原型里才存在的瓶颈",
    0.4, 1.2, 5.8, 3.0, C.red
  );

  // Right: methodology
  addCard(s,
    "新的三步法（已写入 AGENTS.md）",
    "① 画映射表\n　 原型每个模块 ↔ 真实代码 函数+行号\n\n② 结构对齐\n　 原型补缺失模块，实验数据精确对标\n\n③ 三问预检\n　 改哪个模块？对哪个函数？走这条路径？\n　 答不上来 → 不动手",
    6.6, 1.2, 6.0, 4.5, C.green
  );

  addFooter(s);
}

// --- SLIDE 10: 后续计划 ---
{
  const s = pptx.addSlide();
  s.background = { fill: C.offWhite };
  addHeader(s, "Next Steps", "接下来聚焦三个方向");

  const items = [
    {
      title: "① Producer 端继续拆解",
      body: "consumer 侧 profiling：notify/consumer 交互还有剩余空间\n完善 sub-stage 36 的完整链测量",
    },
    {
      title: "② 真实代码验证",
      body: "等待 OpenHarmony 编译环境\nGitLab fork  benchmark 验证\n通过后合 master",
    },
    {
      title: "③ eBPF 线（如需）",
      body: "上次组会遗留：4T eBPF 异常快的问题\n更高线程数重复实验确认",
    },
  ];

  items.forEach((item, i) => {
    s.addShape(pptx.ShapeType.roundRect, {
      x: 0.5 + i * 4.2, y: 1.4, w: 3.9, h: 4.0,
      fill: { color: C.white },
      shadow: { type: "outer", blur: 6, offset: 1, color: "000000", opacity: 0.08 },
      rectRadius: 0.1,
    });
    s.addText(String(i + 1), {
      x: 0.7 + i * 4.2, y: 1.5, w: 1.0, h: 0.8,
      fontSize: 40, bold: true, color: i === 0 ? C.accent : i === 1 ? C.teal : C.midGray,
    });
    s.addText(item.title, {
      x: 0.7 + i * 4.2, y: 2.2, w: 3.5, h: 0.5,
      fontSize: 15, bold: true, color: C.navy,
    });
    s.addText(item.body, {
      x: 0.7 + i * 4.2, y: 2.8, w: 3.5, h: 2.5,
      fontSize: 12, color: C.textMid, lineSpacing: 20,
    });
  });

  addFooter(s);
}

// ==================== OUTPUT ====================
pptx.writeFile({ fileName: "native_hook_progress_2026-06-05.pptx" }).then(() => {
  console.log("PPT generated: native_hook_progress_2026-06-05.pptx");
});
