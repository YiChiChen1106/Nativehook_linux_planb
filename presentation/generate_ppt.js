const PptxGenJS = require("pptxgenjs");

const pptx = new PptxGenJS();
pptx.layout = "LAYOUT_WIDE";
pptx.author = "陈一驰";
pptx.title = "native_hook Producer Hot Path Progress";

const C = {
  bg:       "0F1923",
  card:     "162230",
  cardAlt:  "1B2B3D",
  white:    "F0F6FF",
  dim:      "8899B4",
  faint:    "506080",
  cyan:     "2DE1FC",
  cyanBg:   "173545",
  green:    "3DFCB4",
  greenBg:  "143525",
  orange:   "FF8C52",
  orangeBg: "302015",
  purple:   "C4A0FF",
  purpleBg: "201830",
  pink:     "FF6B9D",
  pinkBg:   "2D1828",
  gold:     "FFD166",
  line:     "1E3045",
};

function hdr(slide, num, title, sub) {
  slide.addText(String(num).padStart(2,"0"), {
    x: 0.35, y: 0.25, w: 0.9, h: 0.65,
    fontSize: 28, color: C.cyan, fontFace: "Consolas", align: "right", bold: true,
  });
  slide.addShape(pptx.ShapeType.rect, { x: 1.4, y: 0.3, w: 2/72, h: 0.55, fill: { color: C.cyan } });
  slide.addText(title, {
    x: 1.6, y: 0.22, w: 10, h: 0.45,
    fontSize: 19, bold: true, color: C.white,
  });
  if (sub) slide.addText(sub, {
    x: 1.6, y: 0.62, w: 10, h: 0.25,
    fontSize: 9, color: C.faint,
  });
  slide.addShape(pptx.ShapeType.rect, { x: 0, y: 1.05, w: 13.33, h: 1/72, fill: { color: C.line } });
}

function tbl(slide, rows, x, y, w) {
  const n = rows[0].length;
  const cw = (w||12.3)/n;
  const hdr = rows[0].map((h,i) => ({
    text: h, options: { bold: true, fontSize: 9, color: C.cyan, fill:{color:C.card}, align:i===0?"left":"center" }
  }));
  const dat = rows.slice(1).map((r,ri) =>
    r.map((c,ci) => ({
      text: String(c), options: {
        fontSize: 9, color: ci===n-1?C.green:C.dim, fill:{color:ri%2?C.cardAlt:C.card},
        align: ci===0?"left":"center",
      }
    }))
  );
  slide.addTable([hdr,...dat], {
    x:x||0.4, y:y||1.3, w:w||12.4,
    border:{type:"solid",color:C.line,pt:0.3},
    colW:Array(n).fill(cw), rowH:0.35, margin:[2,6,2,6],
  });
}

function blt(slide, lines, x, y, w, h, fs) {
  slide.addText(lines.map(t => ({
    text: t, options: { bullet:{code:"25B8"}, fontSize:fs||11, color:C.dim, breakType:"none", paraSpaceAfter:5 }
  })), {
    x:x||0.6, y:y||1.3, w:w||12, h:h||5, lineSpacing:22, valign:"top",
  });
}

function big(slide, val, sub, x, y) {
  slide.addText(val, { x, y, w:2.5, h:0.75, fontSize:38, bold:true, color:C.cyan, align:"center", fontFace:"Consolas" });
  slide.addText(sub, { x, y:y+0.7, w:2.5, h:0.3, fontSize:9, color:C.faint, align:"center" });
}

// === SLIDES ===

// 1 — Title
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  for (let i = 0; i < 6; i++) {
    s.addShape(pptx.ShapeType.rect, { x: 0, y: 1.15*i, w: 13.33, h: 0.2/72, fill: { color: C.line, transparency: 40 } });
  }
  s.addShape(pptx.ShapeType.rect, { x: 1.0, y: 1.8, w: 1.0, h: 3.2, fill: { color: C.cyanBg } });
  s.addShape(pptx.ShapeType.roundRect, { x: 1.5, y: 1.8, w: 0.08, h: 3.2, fill: { color: C.cyan }, rectRadius: 0 });
  s.addText("native_hook", {
    x: 2.2, y: 2.0, w: 9, h: 1.2,
    fontSize: 58, bold: true, color: C.white, fontFace: "Consolas",
  });
  s.addText("PRODUCER  HOT  PATH  ANALYSIS", {
    x: 2.2, y: 3.15, w: 9, h: 0.5,
    fontSize: 15, color: C.cyan, fontFace: "Consolas",
  });
  s.addShape(pptx.ShapeType.rect, { x: 2.2, y: 3.8, w: 5.5, h: 1.2/72, fill: { color: C.gold } });
  s.addText("2026.06.05  组会汇报  ·  陈一驰", {
    x: 2.2, y: 4.1, w: 9, h: 0.4,
    fontSize: 12, color: C.faint,
  });
})();

// 2 — 反馈闭环
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 1, "上次组会反馈闭环", "RESPONSE TO LAST MEETING");

  const cards = [
    { tag:"RESOLVED", c:C.green, bg:C.greenBg, q:"▸ \"4T比1T多1.8s\"", a:"ring共享状态竞争\n→ batch解决后 4T: 2.72s → 0.86s" },
    { tag:"UPGRADED", c:C.cyan, bg:C.cyanBg, q:"▸ \"跑一下perf\"", a:"ablation替代perf\n→ sub-stage 34/35 拆到模块内部操作级" },
    { tag:"COMPLETED", c:C.gold, bg:C.orangeBg, q:"▸ \"fork Gitee → GitLab\"", a:"5个核心函数全加batch\n→ gitlab.youtune.tech/cychi/cyc_nativehook" },
    { tag:"PAUSED", c:C.purple, bg:C.purpleBg, q:"▸ \"4T eBPF异常快\"", a:"本轮聚焦producer端\neBPF线暂停，待后续" },
  ];
  cards.forEach((c, i) => {
    const col = i%2, row = Math.floor(i/2);
    const x = 0.35+col*6.35, y = 1.25+row*2.8;
    s.addShape(pptx.ShapeType.roundRect, { x, y, w:6.1, h:2.5, fill:{color:c.bg}, rectRadius:0.08 });
    s.addShape(pptx.ShapeType.rect, { x, y, w:0.1, h:2.5, fill:{color:c.c} });
    s.addText(c.tag, { x:x+0.3, y:y+0.15, w:1.5, h:0.25, fontSize:7, bold:true, color:c.c, fontFace:"Consolas" });
    s.addText(c.q, { x:x+0.3, y:y+0.5, w:5.5, h:0.4, fontSize:13, bold:true, color:C.white });
    s.addText(c.a, { x:x+0.3, y:y+1.1, w:5.5, h:1.2, fontSize:11, color:C.dim, lineSpacing:20 });
  });
})();

// 3 — Writer/Ring
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 2, "Writer/Ring Impact 拆解实验", "SUB-STAGE 28~33  ·  100万次 malloc/free pair");

  tbl(s, [
    ["Sub-stage", "测量内容", "1T", "4T", "8T", "16T"],
    ["28 no_ring", "基线（无ring操作）", "—", "—", "—", "—"],
    ["29 mutex_only", "仅拿锁 + tracking", "0.28s", "0.62s", "0.85s", "1.12s"],
    ["30 ring_index", "+ ring index检查", "0.45s", "1.35s", "1.92s", "2.48s"],
    ["31 record_copy", "+ 记录拷贝到共享内存", "0.68s", "2.10s", "2.85s", "3.52s"],
    ["32 atomic_pub", "+ atomic write_index发布", "0.75s", "2.35s", "3.10s", "3.80s"],
    ["33 full_notify", "+ eventfd通知 + consumer交互", "1.24s", "2.72s", "3.12s", "3.41s"],
  ], 0.35, 1.25, 12.5);
  blt(s, [
    "多线程额外开销主要来自 ring index/copy 和 atomic publish——共享状态竞争逐步放大",
    "notify/consumer 交互在 8T/16T 下显著抬头，是第二大开销源",
  ], 0.5, 4.8, 12, 1.5, 11);
})();

// 4 — 优化路线
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 3, "Producer 端三项优化", "PER-RECORD OVERHEAD  ·  OPTIMIZATION PIPELINE");

  [
    { n:"01", t:"NOTIFY OUTSIDE MUTEX", c:C.cyan, lines:[
      "eventfd 写入移出 writer 锁临界区", "8T: 3.12s→2.85s    16T: 3.41s→2.98s", "减少持锁时间，多线程有效"
    ]},
    { n:"02", t:"RECORD FILL OUTSIDE MUTEX", c:C.green, lines:[
      "record 元数据填充移出锁", "4T: 3.51s→2.72s (22.5%)    8T: 3.64s→3.12s (14.3%)", "缩短临界区 = 减少竞争窗口"
    ]},
    { n:"03", t:"★  STAGE 6  BATCH  PUBLISH", c:C.gold, lines:[
      "per-thread buffer → 一次拿锁批量写 → 一次 atomic publish → 一次 notify",
      "1T: 1.55s→1.22s    8T: 3.12s→1.28s    16T: 3.41s→1.34s",
      "env gate:  LNHV1_STAGE6_BATCH_SIZE = <1..64>",
    ]},
  ].forEach((o,i) => {
    const y = 1.25 + i*1.95;
    s.addShape(pptx.ShapeType.roundRect, { x:0.35, y, w:12.6, h:1.7, fill:{color:C.card}, rectRadius:0.08 });
    s.addShape(pptx.ShapeType.rect, { x:0.35, y, w:0.1, h:1.7, fill:{color:o.c} });
    s.addText(o.n, { x:0.7, y:y+0.2, w:0.7, h:0.7, fontSize:26, bold:true, color:o.c, fontFace:"Consolas" });
    s.addText(o.t, { x:1.6, y:y+0.15, w:6, h:0.35, fontSize:12, bold:true, color:o.c, fontFace:"Consolas" });
    blt(s, o.lines, 1.8, y+0.55, 10.8, 1.0, 11);
  });
})();

// 5 — Batch 数据
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 4, "Batch Publish — Pink 验证数据", "100万次固定负载  ·  Stage 6 full notify");

  big(s, "21.2%", "1 Thread", 0.35, 1.35);
  big(s, "68.4%", "4 Threads", 3.5, 1.35);
  big(s, "59.1%", "8 Threads", 6.7, 1.35);
  big(s, "60.7%", "16 Threads", 9.9, 1.35);

  tbl(s, [
    ["Threads", "No Batch", "Batch = 64", "提升"],
    ["1", "1.545s", "1.217s", "↓ 21.2%"],
    ["4", "2.718s", "0.859s", "↓ 68.4%"],
    ["8", "3.121s", "1.278s", "↓ 59.1%"],
    ["16", "3.408s", "1.340s", "↓ 60.7%"],
  ], 0.35, 2.5, 12.5);
  blt(s, [
    "Batch-size sweep (8T): 4→1.83s  8→1.58s  16→1.44s  32→1.36s  64→1.26s",
    "Default-off 确认: env未设时 8T=3.39s, 16T=3.36s（行为不变）",
  ], 0.5, 4.8, 12, 1.5, 11);
})();

// 6 — 模块级
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 5, "StackWriter 模块级瓶颈定位", "SUB-STAGE 34/35  ·  RING WRITE vs EVENTFD 开销分离");

  tbl(s, [
    ["Sub-stage", "测量内容", "1T", "4T", "8T", "16T"],
    ["34 write_only", "ring write + inner mutex", "8.60s", "20.94s", "27.40s", "32.25s"],
    ["35 flush_only", "write + eventfd 通知", "9.15s", "19.60s", "23.94s", "31.97s"],
    ["33 + batch64", "批处理后完整链路", "1.24s", "—", "1.26s", "1.37s"],
  ], 0.35, 1.25, 12.5);
  blt(s, [
    "RING WRITE 内锁占比 ~97%（8.60s vs 9.15s）—— eventfd 仅 ~3%，内锁是明确主瓶颈",
    "per-record 锁竞争随线程恶化：8T write 比 1T 慢 3.2 倍",
    "BATCH64 消除 per-record 锁:  1T  9.15s→1.24s (7.4x)    8T  23.94s→1.26s (19x)",
    "对标真实代码:  ShareMemoryBlock::PutWithPayloadTimeout  /  EventNotifier::Post",
  ], 0.5, 4.8, 12, 2, 11);
})();

// 7 — 架构
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 6, "Prototype  ↔  OpenHarmony  架构对齐", "HOT PATH MODULE MAPPING");

  tbl(s, [
    ["热路径操作", "Prototype (Plan B)", "OpenHarmony (hook_client.cpp)"],
    ["malloc / filter / sample", "hook_writer", "hook_malloc"],
    ["re-entry guard", "HookReentryGuard", "__set_hook_flag"],
    ["StackRawData fill", "simplified", "rawdata.{pid, tid, size, addr, ts}"],
    ["AddressHandler", "address_handler.h  [NEW]", "AddAllocAddr()"],
    ["StackWriter write", "stack_writer.cpp  [NEW]", "WriteWithPayloadTimeout"],
    ["StackWriter flush", "Flush / FlushEventFd  [NEW]", "Flush → EventNotifier::Post"],
  ], 0.35, 1.25, 12.5, 0.65);
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
  ], 0.35, 1.25, 12.5);
  blt(s, [
    "待 OpenHarmony 编译环境到位后可 benchmark 验证",
    "未移植:  PID/TID cache（真实代码已有）  /  tracking fallback（架构不同，不适用）",
  ], 0.5, 4.2, 12, 1.5, 11);
})();

// 9 — 教训+三步法
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 8, "经验教训 & 三步预检方法", "FAILED OPTIMIZATIONS  +  PRE-FLIGHT CHECKLIST  [AGENTS.md]");

  // Left
  s.addShape(pptx.ShapeType.roundRect, { x:0.3, y:1.35, w:5.8, h:4.5, fill:{color:C.pinkBg}, rectRadius:0.1 });
  s.addShape(pptx.ShapeType.rect, { x:0.3, y:1.35, w:0.1, h:4.5, fill:{color:C.pink} });
  s.addText("LESSONS FROM FAILED OPTIMIZATIONS", {
    x:0.65, y:1.5, w:5, h:0.3, fontSize:9, bold:true, color:C.pink, fontFace:"Consolas",
  });
  blt(s, [
    "PID/TID cache → 真实代码已有 pthread_getspecific + atomic load",
    "          优化方向正确，但冗余——原型重复开发了已有实现",
    "",
    "tracking fallback → free不走producer端查表",
    "          真实代码 free 直接发给 daemon 做匹配",
    "          优化了一个原型里才存在的瓶颈",
  ], 0.65, 2.0, 5.2, 3.5, 10);

  // Right
  s.addShape(pptx.ShapeType.roundRect, { x:6.5, y:1.35, w:6.5, h:4.5, fill:{color:C.greenBg}, rectRadius:0.1 });
  s.addShape(pptx.ShapeType.rect, { x:6.5, y:1.35, w:0.1, h:4.5, fill:{color:C.green} });
  s.addText("3-STEP  PRE-FLIGHT  [AGENTS.md]", {
    x:6.85, y:1.5, w:5, h:0.3, fontSize:9, bold:true, color:C.green, fontFace:"Consolas",
  });
  blt(s, [
    "①  画映射表",
    "    原型每个模块 ↔ 真实代码 函数+行号",
    "",
    "②  结构对齐",
    "    原型补缺失模块，实验数据精确对标",
    "",
    "③  三问预检",
    "    Q1: 改哪个原型模块？",
    "    Q2: 对应哪个真实函数？",
    "    Q3: 真实代码走不走这条路径？",
    "    → 答不上来，不动手",
  ], 6.85, 2.0, 5.8, 3.5, 10);
})();

// 10 — Next
(() => {
  const s = pptx.addSlide();
  s.background = { fill: C.bg };
  hdr(s, 9, "Next Steps");

  [
    { n:"01", t:"Producer 端继续拆解", d:"consumer 侧 profiling · 完善 sub-stage 36 完整链测量数据", c:C.cyan },
    { n:"02", t:"真实代码验证", d:"等待 OpenHarmony 编译环境 → GitLab fork benchmark → 合 master", c:C.green },
    { n:"03", t:"eBPF 线（如需继续）", d:"上次遗留: 4T 异常快 → 更高线程数重复实验确认", c:C.purple },
  ].forEach((it,i) => {
    const y = 1.5 + i*1.9;
    s.addShape(pptx.ShapeType.roundRect, { x:1.0, y, w:11.3, h:1.55, fill:{color:C.card}, rectRadius:0.1 });
    s.addShape(pptx.ShapeType.rect, { x:1.0, y, w:0.1, h:1.55, fill:{color:it.c} });
    s.addText(it.n, { x:1.35, y:y+0.15, w:0.8, h:0.8, fontSize:32, bold:true, color:it.c, fontFace:"Consolas" });
    s.addText(it.t, { x:2.3, y:y+0.2, w:5, h:0.35, fontSize:14, bold:true, color:C.white });
    s.addText(it.d, { x:2.3, y:y+0.7, w:9, h:0.5, fontSize:11, color:C.dim });
  });
})();

pptx.writeFile({ fileName: "native_hook_progress_2026-06-05.pptx" }).then(() => console.log("OK"));
