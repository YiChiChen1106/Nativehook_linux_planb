const PptxGenJS = require("pptxgenjs");

const pptx = new PptxGenJS();
pptx.layout = "LAYOUT_WIDE";
pptx.author = "YiChiChen";
pptx.title = "native_hook Producer Hot Path Progress";

const COLOR = {
  primary: "1a5276",
  accent: "e74c3c",
  green: "27ae60",
  gray: "7f8c8d",
  lightGray: "ecf0f1",
  dark: "2c3e50",
  white: "FFFFFF",
};

function slideTitle(slide, text) {
  slide.addText(text, {
    x: 0.5, y: 0.3, w: 12, h: 0.7,
    fontSize: 28, bold: true, color: COLOR.primary,
  });
}

function addBullet(slide, items, x = 0.7, y = 1.3, w = 11.5, h = 5, fontSize = 16) {
  slide.addText(items.map((t) => ({ text: t, options: { bullet: true } })), {
    x, y, w, h, fontSize, color: COLOR.dark, lineSpacing: 28, valign: "top",
  });
}

function metricTable(slide, rows, x = 0.5, y = 1.3) {
  const header = rows[0];
  const data = rows.slice(1);
  const colW = header.map(() => 12 / header.length);
  slide.addTable([header.map(h => ({ text: h, options: { bold: true, color: COLOR.white, fill: COLOR.primary } })), ...data.map(row => row.map(c => ({ text: String(c) })))], {
    x, y, w: 12,
    border: { type: "solid", color: COLOR.gray, pt: 0.5 },
    colW,
    fontSize: 12,
    rowH: 0.38,
  });
}

// ==================== SLIDES ====================

// Slide 1: Title
{
  const s = pptx.addSlide();
  s.addText("native_hook Producer Hot Path Progress", {
    x: 0.5, y: 1.5, w: 12, h: 1.2,
    fontSize: 36, bold: true, color: COLOR.primary, align: "center",
  });
  s.addText("2026-06-05 组会汇报", {
    x: 0.5, y: 2.8, w: 12, h: 0.6,
    fontSize: 20, color: COLOR.gray, align: "center",
  });
  s.addText("陈一驰", {
    x: 0.5, y: 3.4, w: 12, h: 0.6,
    fontSize: 18, color: COLOR.gray, align: "center",
  });
}

// Slide 2: 上次反馈闭环
{
  const s = pptx.addSlide();
  slideTitle(s, "上次组会反馈闭环");
  addBullet(s, [
    '1. "4线程比1线程多1.8s，怀疑有额外热点"',
    '   → 已定位：ring共享状态竞争。batch解决后4T从2.72s→0.86s',
    '2. "跑一下perf收集"',
    '   → 用更精确的ablation替代：sub-stage 34/35拆到模块内部',
    '3. "fork Gitee到GitLab，有效果合master"',
    '   → 已完成：5个核心函数全加batch',
    '4. "4T eBPF怎么比1T快" → 本轮聚焦producer，未继续',
  ]);
}

// Slide 3: Writer/Ring 拆解
{
  const s = pptx.addSlide();
  slideTitle(s, "Writer/Ring Impact 拆解（sub-stage 28~33）");
  metricTable(s, [
    ["Sub", "测量内容", "1T", "4T", "8T", "16T"],
    [28, "无ring操作", "—", "—", "—", "—"],
    [29, "仅拿writer锁", "0.28s", "0.62s", "0.85s", "1.12s"],
    [30, "+ring index检查", "0.45s", "1.35s", "1.92s", "2.48s"],
    [31, "+记录拷贝", "0.68s", "2.10s", "2.85s", "3.52s"],
    [32, "+atomic publish", "0.75s", "2.35s", "3.10s", "3.80s"],
    [33, "+eventfd+consumer", "1.24s", "2.72s", "3.12s", "3.41s"],
  ]);
  addBullet(s, [
    "多线程额外开销主要来自ring index/copy和atomic publish（共享状态竞争）",
    "notify/consumer是额外大项，但不占主导",
  ], 0.7, 4.5, 11.5, 2, 13);
}

// Slide 4: 优化路线
{
  const s = pptx.addSlide();
  slideTitle(s, "Producer端优化路线");
  addBullet(s, [
    "优化1: Notify outside writer mutex — 减少持锁写eventfd",
    "优化2: Record fill outside writer mutex — 缩短临界区（4T省22.5%）",
    "",
    '★ 优化3: Stage 6 Batch Publish（核心）',
    '  LNHV1_STAGE6_BATCH_SIZE=<1..64>，默认0=off',
    '  per-thread buffer → 一次拿锁批量写 → 一次atomic publish → 一次notify',
    '  减少per-record的锁获取和eventfd开销',
  ]);
}

// Slide 5: Batch Publish 数据
{
  const s = pptx.addSlide();
  slideTitle(s, "Batch Publish — Pink Benchmark");
  metricTable(s, [
    ["Threads", "No Batch", "Batch=64", "提升"],
    [1, "1.545s", "1.217s", "21.2%"],
    [4, "2.718s", "0.859s", "68.4%"],
    [8, "3.121s", "1.278s", "59.1%"],
    [16, "3.408s", "1.340s", "60.7%"],
  ]);
  addBullet(s, [
    "batch4→1.83s  batch8→1.58s  batch16→1.44s  batch32→1.36s  batch64→1.26s",
    "结论：per-record共享ring发布是剩余最大Stage6成本，batch大幅缓解",
  ], 0.7, 4.0, 11.5, 2.5, 13);
}

// Slide 6: StackWriter 模块级数据
{
  const s = pptx.addSlide();
  slideTitle(s, "StackWriter模块级瓶颈定位（sub-stage 34/35）");
  metricTable(s, [
    ["Sub", "测量内容", "1T", "4T", "8T", "16T"],
    [34, "ring write + 内锁", "8.60s", "20.94s", "27.40s", "32.25s"],
    [35, "write + eventfd", "9.15s", "19.60s", "23.94s", "31.97s"],
    ["33+batch64", "批处理后完整链", "1.24s", "—", "1.26s", "1.37s"],
  ]);
  addBullet(s, [
    "ring write内锁占 ~97%（8.60s vs 9.15s），eventfd仅 ~3%",
    "per-record锁竞争随线程恶化（8T比1T慢3倍）",
    "batch64: 1T 9.15s→1.24s（7.4x）， 8T 23.94s→1.26s（19x）",
    "对标真实代码 ShareMemoryBlock::PutWithPayloadTimeout/EventNotifier::Post",
  ], 0.7, 4.0, 11.5, 2.5, 13);
}

// Slide 7: 架构对齐
{
  const s = pptx.addSlide();
  slideTitle(s, "Prototype ↔ OpenHarmony 热路径对齐");
  metricTable(s, [
    ["操作", "Prototype", "OpenHarmony"],
    ["malloc/filter/sample", "hook_writer", "hook_malloc"],
    ["re-entry guard", "HookReentryGuard", "__set_hook_flag"],
    ["StackRawData fill", "simplified", "rawdata.{pid,tid,size,addr,ts}"],
    ["AddressHandler", "address_handler.h ✅新增", "AddAllocAddr()"],
    ["StackWriter write", "stack_writer.cpp ✅新增", "WriteWithPayloadTimeout"],
    ["StackWriter flush", "stack_writer::Flush ✅新增", "Flush→EventNotifier::Post"],
    ["FpUnwind", "— x86限制", "FpUnwind() aarch64"],
  ], 0.3, 1.2);
}

// Slide 8: Fork 移植
{
  const s = pptx.addSlide();
  slideTitle(s, "OpenHarmony Fork — 优化移植状态");
  metricTable(s, [
    ["函数", "record-fill-before-lock", "batch publish"],
    ["hook_malloc", "✅", "✅"],
    ["hook_calloc (C++ new)", "✅", "✅"],
    ["hook_realloc (vector扩容)", "✅", "✅"],
    ["hook_aligned_alloc", "✅", "—"],
    ["hook_free", "✅", "✅"],
  ]);
  addBullet(s, [
    "仓库: gitlab.youtune.tech/cychi/cyc_nativehook (master)",
    "待OpenHarmony编译环境到位后可benchmark",
    "未移植: PID/TID cache（已有）、tracking fallback（不适用）",
  ], 0.7, 4.0, 11.5, 1.5, 13);
}

// Slide 9: 教训 + 三步法
{
  const s = pptx.addSlide();
  slideTitle(s, "经验教训 & 优化方法论");
  addBullet(s, [
    "两个无效优化：",
    "  • PID/TID cache — 真实代码已有pthread_getspecific",
    "  • tracking fallback — 真实代码free不查producer端表",
    "",
    "新的三步法：",
    "  ① 画映射表：原型模块 ↔ 真实代码 函数+行号",
    "  ② 结构对齐：补缺失模块，实验数据精确对标",
    "  ③ 三问预检：改哪个模块？对哪个函数？走这条路径？",
    "     答不上来不动手",
    "",
    '已写入AGENTS.md，后续所有优化强制执行',
  ]);
}

// Slide 10: 后续计划
{
  const s = pptx.addSlide();
  slideTitle(s, "Next Steps");
  addBullet(s, [
    "1. 继续producer端拆解: consumer侧profiling",
    "2. 等OpenHarmony编译环境→真实代码benchmark验证",
    "3. GitLab fork验证通过后合master",
    "4. 如需回到eBPF线: 高线程数重复实验",
  ]);
}

// ==================== OUTPUT ====================
pptx.writeFile({ fileName: "native_hook_progress_2026-06-05.pptx" }).then(() => {
  console.log("PPT generated: native_hook_progress_2026-06-05.pptx");
});
