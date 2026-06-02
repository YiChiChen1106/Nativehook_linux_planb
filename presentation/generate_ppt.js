const PptxGenJS = require("pptxgenjs");

const pptx = new PptxGenJS();
pptx.layout = "LAYOUT_WIDE";
pptx.author = "陈一驰";
pptx.title = "native_hook Producer Hot Path Progress";

// ====== PALETTE: Warm Modern ======
const C = {
  bg:       "FAFAFC",
  white:    "FFFFFF",
  ink:      "1E1E2E",
  inkDim:   "585878",
  inkFaint: "9090A8",
  blue:     "4472C4",
  blueBg:   "EBF0FA",
  green:    "2D8C5E",
  greenBg:  "E8F5EE",
  orange:   "E07B3C",
  orangeBg: "FDF0E6",
  red:      "D64545",
  redBg:    "FCEAEA",
  purple:   "7B4FBF",
  purpleBg: "F0EBF8",
  line:     "E4E4EC",
};

function header(slide, title, sub) {
  slide.addText(title, {
    x: 0.6, y: 0.35, w: 12, h: 0.55,
    fontSize: 26, bold: true, color: C.ink, fontFace: "Microsoft YaHei",
  });
  if (sub) {
    slide.addText(sub, {
      x: 0.6, y: 0.9, w: 12, h: 0.35,
      fontSize: 12, color: C.inkFaint,
    });
  }
  slide.addShape(pptx.ShapeType.rect, {
    x: 0.6, y: 1.25, w: 4, h: 3/72, fill: { color: C.blue },
  });
}

function bullets(slide, lines, x, y, w, h, fs) {
  slide.addText(lines.map(t => ({
    text: t, options: { bullet: true, fontSize: fs || 13, color: C.inkDim, breakType: "none" }
  })), {
    x: x || 0.8, y: y || 1.6, w: w || 11.5, h: h || 5.2,
    lineSpacing: 24, valign: "top",
  });
}

function table(slide, rows, x, y, w, hls) {
  const cols = rows[0].length;
  const cw = (w || 12.0) / cols;
  const headerRow = rows[0].map(h => ({
    text: h, options: { bold: true, fontSize: 10, color: C.white, fill: { color: C.ink }, align: "center" }
  }));
  const dataRows = rows.slice(1).map((r, ri) =>
    r.map((c, ci) => ({
      text: String(c), options: {
        fontSize: 10, color: C.inkDim,
        fill: { color: ri % 2 ? C.bg : C.white },
        align: ci === 0 ? "left" : "center",
      }
    }))
  );
  const rowH = hls || 0.36;
  slide.addTable([headerRow, ...dataRows], {
    x: x || 0.5, y: y || 1.55, w: w || 12.2,
    border: { type: "solid", color: C.line, pt: 0.5 },
    colW: Array(cols).fill(cw), rowH, margin: [2, 5, 2, 5],
  });
}

function bigNum(slide, num, label, x, y) {
  slide.addText(num, { x, y, w: 2.5, h: 0.8, fontSize: 36, bold: true, color: C.blue, align: "center" });
  slide.addText(label, { x, y: y + 0.7, w: 2.5, h: 0.4, fontSize: 10, color: C.inkFaint, align: "center" });
}

function badgeBox(slide, text, color, x, y, w) {
  slide.addShape(pptx.ShapeType.roundRect, {
    x, y, w: w || 3.5, h: 0.35, fill: { color }, rectRadius: 0.06,
  });
  slide.addText(text, {
    x: x + 0.1, y, w: (w || 3.5) - 0.2, h: 0.35, fontSize: 9, bold: true, color: C.white, align: "center", valign: "middle",
  });
}

// ==================== SLIDES ====================

// Slide 1 — Title
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  s.addShape(pptx.ShapeType.rect, { x: 0, y: 0, w: 13.33, h: 7.5, fill: { color: C.ink } });
  s.addText("native_hook", {
    x: 1.5, y: 1.6, w: 10, h: 1.3,
    fontSize: 64, bold: true, color: C.white, align: "center",
  });
  s.addText("Producer Hot Path 优化进展", {
    x: 1.5, y: 2.9, w: 10, h: 0.7,
    fontSize: 24, color: C.inkFaint, align: "center",
  });
  s.addShape(pptx.ShapeType.rect, { x: 5, y: 3.8, w: 3.3, h: 3/72, fill: { color: C.orange } });
  s.addText("2026.06.05  组会汇报  ·  陈一驰", {
    x: 1.5, y: 4.2, w: 10, h: 0.5,
    fontSize: 14, color: C.inkFaint, align: "center",
  });
})();

// Slide 2 — 反馈闭环
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  header(s, "上次组会反馈闭环", "四个问题，逐一回应");

  const items = [
    { icon: "❶", title: "4T vs 1T 多 1.8s", body: "已定位：ring 共享状态竞争\n→ batch 解决后 4T: 2.72s → 0.86s", clr: C.greenBg, accent: C.green },
    { icon: "❷", title: "跑一下 perf 收集", body: "用 ablation 替代 perf\n→ sub-stage 34/35 拆到模块内部操作级", clr: C.blueBg, accent: C.blue },
    { icon: "❸", title: "fork Gitee → GitLab", body: "已完成，5 个核心函数加 batch\n→ gitlab.youtune.tech/cychi/cyc_nativehook", clr: C.orangeBg, accent: C.orange },
    { icon: "❹", title: "4T eBPF 异常快", body: "本轮聚焦 producer 端\n→ eBPF 线暂停，待后续", clr: C.purpleBg, accent: C.purple },
  ];

  items.forEach((it, i) => {
    const col = i % 2, row = Math.floor(i / 2);
    const x = 0.5 + col * 6.2, y = 1.6 + row * 2.7;
    s.addShape(pptx.ShapeType.roundRect, { x, y, w: 5.8, h: 2.4, fill: { color: it.clr }, rectRadius: 0.12 });
    s.addText(it.icon, { x: x + 0.2, y: y + 0.15, w: 0.5, h: 0.5, fontSize: 22, color: it.accent });
    s.addText(it.title, { x: x + 0.7, y: y + 0.2, w: 4.8, h: 0.4, fontSize: 15, bold: true, color: C.ink });
    s.addText(it.body, { x: x + 0.7, y: y + 0.7, w: 4.8, h: 1.5, fontSize: 12, color: C.inkDim, lineSpacing: 20 });
  });
})();

// Slide 3 — Writer/Ring 拆解
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  header(s, "Writer/Ring Impact 拆解实验", "sub-stage 28~33 逐段测量，固定 100 万次 malloc/free pair");
  table(s, [
    ["Sub", "测量内容", "1T", "4T", "8T", "16T"],
    ["28 no_ring", "基线（无 ring 操作）", "—", "—", "—", "—"],
    ["29 mutex_only", "仅拿锁 + tracking", "0.28s", "0.62s", "0.85s", "1.12s"],
    ["30 ring_index", "+ ring index 检查", "0.45s", "1.35s", "1.92s", "2.48s"],
    ["31 record_copy", "+ 记录拷贝到共享内存", "0.68s", "2.10s", "2.85s", "3.52s"],
    ["32 atomic_pub", "+ atomic write_index 发布", "0.75s", "2.35s", "3.10s", "3.80s"],
    ["33 full_notify", "+ eventfd + consumer", "1.24s", "2.72s", "3.12s", "3.41s"],
  ], 0.4, 1.55, 12.4, 0.34);
  bullets(s, [
    "多线程额外开销主要来自 ring index/copy 和 atomic publish（共享状态竞争逐步放大）",
    "notify/consumer 交互是额外大项，8T/16T 显著抬头",
  ], 0.6, 5.0, 12, 1.5, 13);
})();

// Slide 4 — 优化路线
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  header(s, "Producer 端三项优化", "逐级降低 per-record 共享路径开销");

  const bodyY = 1.9;
  [
    {
      icon: "01", title: "Notify outside mutex", clr: C.blue, bg: C.blueBg,
      items: ["eventfd 写入移出 writer 锁临界区", "8T: 3.12s→2.85s, 16T: 3.41s→2.98s", "减少持锁时间"],
      top: bodyY,
    },
    {
      icon: "02", title: "Record fill outside mutex", clr: C.green, bg: C.greenBg,
      items: ["record 元数据填充移出锁", "4T: 3.51s→2.72s (22.5%)", "缩短临界区在多线程下效果明显"],
      top: bodyY + 1.85,
    },
    {
      icon: "★", title: "Stage 6 Batch Publish", clr: C.orange, bg: C.orangeBg,
      items: ["per-thread buffer → 一次拿锁批量写 → 一次 atomic publish", "1T: 1.55s→1.22s  8T: 3.12s→1.28s  16T: 3.41s→1.34s", "env gate: LNHV1_STAGE6_BATCH_SIZE=<1..64>"],
      top: bodyY + 3.7,
    },
  ].forEach((opt, idx) => {
    const bgY = opt.top;
    s.addShape(pptx.ShapeType.roundRect, { x: 0.5, y: bgY, w: 12.3, h: 1.55, fill: { color: opt.bg }, rectRadius: 0.1 });
    s.addText(opt.icon, { x: 0.7, y: bgY + 0.15, w: 0.6, h: 0.5, fontSize: 24, bold: true, color: opt.clr });
    s.addText(opt.title, { x: 1.3, y: bgY + 0.12, w: 5, h: 0.35, fontSize: 15, bold: true, color: C.ink });
    bullets(s, opt.items, 1.5, bgY + 0.5, 11, 0.9, 11);
  });
})();

// Slide 5 — Batch 核心数据
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  header(s, "Batch Publish — Pink 验证数据", "100万次 malloc/free pair, Stage 6 full notify");

  const numY = 1.7;
  bigNum(s, "21.2%", "1T 提升", 0.3, numY);
  bigNum(s, "68.4%", "4T 提升", 3.4, numY);
  bigNum(s, "59.1%", "8T 提升", 6.5, numY);
  bigNum(s, "60.7%", "16T 提升", 9.6, numY);

  table(s, [
    ["Threads", "No Batch", "Batch=64", "提升"],
    ["1", "1.545s", "1.217s", "↓ 21.2%"],
    ["4", "2.718s", "0.859s", "↓ 68.4%"],
    ["8", "3.121s", "1.278s", "↓ 59.1%"],
    ["16", "3.408s", "1.340s", "↓ 60.7%"],
  ], 0.4, 2.8, 12.4);

  bullets(s, [
    "Batch-size sweep (8T): 4→1.83s  8→1.58s  16→1.44s  32→1.36s  64→1.26s",
    "Default-off 确认无误（env unset: 8T=3.39s, 16T=3.36s）",
  ], 0.6, 5.0, 12, 1.5, 13);
})();

// Slide 6 — StackWriter 模块级
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  header(s, "StackWriter 模块级瓶颈定位 (sub-stage 34/35)", "分离 ring write 内锁 vs eventfd 开销");

  table(s, [
    ["Sub", "测量内容", "1T", "4T", "8T", "16T"],
    ["34 write_only", "ring write + inner mutex", "8.60s", "20.94s", "27.40s", "32.25s"],
    ["35 flush_only", "write + eventfd", "9.15s", "19.60s", "23.94s", "31.97s"],
    ["33+batch64", "批处理后完整链", "1.24s", "—", "1.26s", "1.37s"],
  ], 0.4, 1.55, 12.4);

  bullets(s, [
    "ring write 内锁占 ~97% (8.60s vs 9.15s)——eventfd 仅 ~3%，内锁是明确主瓶颈",
    "per-record 锁竞争随线程恶化（8T write 比 1T 慢 3.2x）",
    "batch64 把 1T 从 9.15s 压到 1.24s (7.4x), 8T 从 23.94s 压到 1.26s (19x)",
    "数据对标: ShareMemoryBlock::PutWithPayloadTimeout / EventNotifier::Post",
  ], 0.6, 4.8, 12, 2, 13);
})();

// Slide 7 — 架构对齐
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  header(s, "Prototype ↔ OpenHarmony 热路径对齐", "重构后原型结构与真实代码在模块级别可比");

  table(s, [
    ["热路径操作", "Prototype (Plan B)", "OpenHarmony (hook_client.cpp)"],
    ["malloc/filter/sample", "hook_writer", "hook_malloc"],
    ["re-entry guard", "HookReentryGuard", "__set_hook_flag"],
    ["StackRawData fill", "simplified", "rawdata.{pid,tid,size,addr,ts}"],
    ["AddressHandler tracking", "address_handler.h ✅新增", "AddAllocAddr()"],
    ["StackWriter write", "stack_writer.cpp ✅新增", "WriteWithPayloadTimeout"],
    ["StackWriter flush", "Flush / FlushEventFd ✅新增", "Flush → EventNotifier::Post"],
  ], 0.4, 1.55, 12.4, 0.48);
})();

// Slide 8 — Fork 移植
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  header(s, "OpenHarmony Fork — 移植状态", "五个核心函数已全部覆盖");

  table(s, [
    ["函数", "调用场景", "record-fill-before-lock", "batch publish"],
    ["hook_malloc", "主分配路径", "✓", "✓"],
    ["hook_calloc", "C++ new 底层", "✓", "✓"],
    ["hook_realloc", "vector 扩容 (free+alloc双记录)", "✓", "✓"],
    ["hook_aligned_alloc", "对齐分配", "✓", "—"],
    ["hook_free", "释放路径", "✓", "✓"],
  ], 0.4, 1.55, 12.4);

  bullets(s, [
    "仓库: gitlab.youtune.tech/cychi/cyc_nativehook  (master 分支)",
    "待 OpenHarmony 编译环境到位后可 benchmark 验证",
    "未移植: PID/TID cache (已有), tracking fallback (架构不同)",
  ], 0.6, 4.5, 12, 1.5, 13);
})();

// Slide 9 — 教训
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  header(s, "教训 & 三步法", "两个无效优化如何避免");

  // Left
  s.addShape(pptx.ShapeType.roundRect, { x: 0.4, y: 1.6, w: 5.8, h: 4.2, fill: { color: C.redBg }, rectRadius: 0.12 });
  s.addText("两个无效优化", { x: 0.7, y: 1.8, w: 5, h: 0.4, fontSize: 16, bold: true, color: C.red });
  bullets(s, [
    "PID/TID cache: 真实代码已有 pthread_getspecific + atomic load，原型重复开发",
    "tracking fallback: 真实代码 free 不查 producer 端表，优化了一个不存在的瓶颈",
  ], 0.8, 2.3, 5, 3, 12);

  // Right
  s.addShape(pptx.ShapeType.roundRect, { x: 6.6, y: 1.6, w: 6.2, h: 4.2, fill: { color: C.greenBg }, rectRadius: 0.12 });
  s.addText("新的三步法 (AGENTS.md)", { x: 6.9, y: 1.8, w: 5.5, h: 0.4, fontSize: 16, bold: true, color: C.green });
  bullets(s, [
    "① 画映射表：原型每个模块 ↔ 真实代码 函数+行号",
    "② 结构对齐：原型补缺失模块，实验数据精确对标",
    "③ 三问预检：改哪个模块？对哪个函数？走这条路径？",
    "   答不上来 → 不动手",
  ], 7.1, 2.3, 5.3, 3, 12);
})();

// Slide 10 — Next
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  header(s, "Next Steps");

  ["Producer 继续拆: consumer 侧 profiling，完善 sub-stage 36",
   "真实代码验证: 等编译环境 → GitLab fork benchmark → 合 master",
   "eBPF 线 (如需): 上次遗留的高线程数重复实验",
  ].forEach((t, i) => {
    const y = 1.7 + i * 1.8;
    s.addShape(pptx.ShapeType.roundRect, { x: 2.5, y, w: 8.5, h: 1.45, fill: { color: C.white }, rectRadius: 0.1, shadow: { type: "outer", blur: 8, offset: 1, color: "000000", opacity: 0.06 } });
    s.addText(String(i + 1), { x: 2.7, y: y + 0.2, w: 0.8, h: 1.0, fontSize: 38, bold: true, color: C.inkFaint, align: "center", valign: "middle" });
    s.addText(t, { x: 3.6, y: y + 0.3, w: 7, h: 0.9, fontSize: 15, color: C.inkDim, valign: "middle" });
  });
})();

// ==================== OUTPUT ====================
pptx.writeFile({ fileName: "native_hook_progress_2026-06-05.pptx" }).then(() => {
  console.log("OK: native_hook_progress_2026-06-05.pptx");
});
