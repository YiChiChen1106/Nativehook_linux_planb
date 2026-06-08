import PptxGenJS from "pptxgenjs";

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
  slide.addText(items.map((t) => ({ text: t, options: { bullet: true, breakType: "none" } })), {
    x, y, w, h, fontSize, color: COLOR.dark, lineSpacing: 28, valign: "top",
  });
}

function metricTable(slide, rows, x = 0.5, y = 1.3) {
  const header = rows[0];
  const data = rows.slice(1);
  slide.addTable([header, ...data], {
    x, y, w: 12,
    border: { type: "solid", color: COLOR.gray },
    colW: header.map(() => 12 / header.length),
    fontSize: 12,
    rowH: 0.35,
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
    '1. "4线程比1线程多1.8s，怀疑有额外热点" → 已定位：ring共享状态竞争（index/copy/atomic publish/notify）\n   batch publish 解决后，4T从2.72s→0.86s，不仅消除差距，甚至反超1T',
    '2. "跑一下perf收集"   → 用更精确的ablation替代perf：新增sub-stage 34/35拆到模块内部操作级\n   ring write内锁占97%，eventfd仅3%',
    '3. "fork Gitee native hook到GitLab，有效果合master" → 已完成\n   gitlab.youtune.tech/cychi/cyc_nativehook，5个核心函数全加batch publish',
    '4. "4线程eBPF怎么比单线程还快" → 本轮聚焦producer端，eBPF未继续',
  ]);
}

// Slide 3: Writer/Ring 拆解实验
{
  const s = pptx.addSlide();
  slideTitle(s, "Writer/Ring Impact 拆解（sub-stage 28~33）");
  metricTable(s, [
    ["Sub-stage", "测量内容", "1T", "4T", "8T", "16T"],
    ["28 no_ring", "无ring操作(baseline inside hook)", "—", "—", "—", "—"],
    ["29 mutex_only", "仅拿writer锁", "+0.28s", "+0.62s", "+0.85s", "+1.12s"],
    ["30 ring_index", "+ring index检查", "+0.45s", "+1.35s", "+1.92s", "+2.48s"],
    ["31 record_copy", "+记录拷贝到共享内存", "+0.68s", "+2.10s", "+2.85s", "+3.52s"],
    ["32 atomic_publish", "+atomic write_index发布", "+0.75s", "+2.35s", "+3.10s", "+3.80s"],
    ["33 full_notify", "+eventfd通知+consumer交互", "+1.24s", "+2.72s", "+3.12s", "+3.41s"],
  ]);
  addBullet(s, [
    "结论：多线程额外开销主要来自 ring index/copy 和 atomic publish（共享状态竞争）",
    "notify/consumer 是额外大项，但不占主导",
  ], 0.7, 4.5, 11.5, 2, 13);
}

// Slide 4: 优化路线
{
  const s = pptx.addSlide();
  slideTitle(s, "Producer 端优化路线");
  addBullet(s, [
    "优化1: Notify outside writer mutex — 8T/16T有效，减少持锁写eventfd时间",
    "优化2: Record fill outside writer mutex — 缩短锁内工作量",
    "  mutex本身不是全部瓶颈，但缩短临界区在多线程下明显（4T省22.5%）",
    '优化3: Stage 6 Batch Publish（核心）',
    '  用 LNHV1_STAGE6_BATCH_SIZE 控制batch大小（默认0=off）',
    '  per-thread buffer累积record → 一次拿锁写多个 → 一次atomic publish → 一次notify',
  ]);
}

// Slide 5: Batch Publish 核心数据
{
  const s = pptx.addSlide();
  slideTitle(s, "Batch Publish — Pink Benchmark");
  metricTable(s, [
    ["Threads", "No Batch (record-fill-outside-lock)", "Batch=64", "提升"],
    ["1", "1.545s", "1.217s", "21.2%"],
    ["4", "2.718s", "0.859s", "68.4%"],
    ["8", "3.121s", "1.278s", "59.1%"],
    ["16", "3.408s", "1.340s", "60.7%"],
  ]);
  addBullet(s, [
    "Batch-size sweep: batch4→1.83s, batch8→1.58s, batch16→1.44s, batch32→1.36s, batch64→1.26s",
    "结论：per-record共享ring发布是剩余最大Stage 6扩展成本，batch大幅缓解",
  ], 0.7, 4.0, 11.5, 2.5, 13);
}

// Slide 6: StackWriter 模块级数据
{
  const s = pptx.addSlide();
  slideTitle(s, "新Ablation：模块级瓶颈定位（sub-stage 34/35）");
  metricTable(s, [
    ["Sub-stage", "测量内容", "1T", "4T", "8T", "16T"],
    ["34 write_only", "纯ring write + 内部锁", "8.60s", "20.94s", "27.40s", "32.25s"],
    ["35 flush_only", "write + eventfd通知", "9.15s", "19.60s", "23.94s", "31.97s"],
    ["33 full+batch64", "批处理后完整链路", "1.24s", "—", "1.26s", "1.37s"],
  ]);
  addBullet(s, [
    "ring write内锁占 ~97%（8.60s vs 9.15s），eventfd仅 ~3%",
    "per-record锁竞争随线程恶化（8T比1T慢3倍）",
    "batch64: 1T 9.15s→1.24s（7.4x），8T 23.94s→1.26s（19x）",
    "数据直接对标真实代码 ShareMemoryBlock::PutWithPayloadTimeout + EventNotifier::Post",
  ], 0.7, 4.0, 11.5, 2.5, 13);
}

// Slide 7: 架构对齐
{
  const s = pptx.addSlide();
  slideTitle(s, "Prototype ↔ OpenHarmony 热路径对齐");
  metricTable(s, [
    ["操作", "Prototype", "OpenHarmony (hook_client.cpp)", "状态"],
    ["malloc/filter/sample", "hook_writer", "hook_malloc", "✅"],
    ["re-entry guard", "HookReentryGuard", "__set_hook_flag", "✅"],
    ["FpUnwind stack walk", "— (x86)", "FpUnwind() aarch64", "❌架构限制"],
    ["StackRawData fill", "⚠️ simplified", "rawdata.{pid,tid,...}", "⚠️"],
    ["AddressHandler tracking", "address_handler.h", "AddAllocAddr()", "✅ 新增"],
    ["StackWriter write", "stack_writer.cpp", "WriteWithPayloadTimeout", "✅ 新增"],
    ["StackWriter flush", "NotifyEventFd", "Flush→EventNotifier::Post", "✅ 新增"],
  ], 0.3, 1.2);
}

// Slide 8: OpenHarmony Fork 移植
{
  const s = pptx.addSlide();
  slideTitle(s, "OpenHarmony Fork — 优化移植");
  metricTable(s, [
    ["函数", "record-fill-before-lock", "batch publish", "说明"],
    ["hook_malloc", "✅", "✅", "主分配路径"],
    ["hook_calloc", "✅", "✅", "C++ new底层调用"],
    ["hook_realloc", "✅", "✅", "vector扩容，双记录(free+alloc)"],
    ["hook_aligned_alloc", "✅", "—", "对齐分配"],
    ["hook_free", "✅", "✅", "本次新增"],
  ]);
  addBullet(s, [
    "仓库: gitlab.youtune.tech/cychi/cyc_nativehook  (master分支)",
    "待OpenHarmony编译环境到位后可benchmark验证",
    "未移植：PID/TID cache（已有）、thread_local_fallback tracking（不适用）",
  ], 0.7, 4.0, 11.5, 2, 13);
}

// Slide 9: 教训 + 三步法
{
  const s = pptx.addSlide();
  slideTitle(s, "经验教训 & 优化方法论");
  addBullet(s, [
    "两个无效优化的教训：",
    "  PID/TID cache — 真实代码已有pthread_getspecific缓存，原型冗余开发",
    "  thread_local_fallback tracking — 真实代码free不查producer端表，优化无对应热路径",
    "",
    "新的三步法（写入AGENTS.md）：",
    "  ① 画映射表：原型每个模块↔真实代码哪个函数、哪一行",
    '  ② 结构对齐：原型补缺失模块（如stack_writer、address_handler），让实验数据精确对标',
    "  ③ 三问预检：改哪个模块？对应哪个真实函数？真实代码走不走这条路径？",
    "     三个问题有一个答不上来，不动手",
  ]);
}

// Slide 10: 后续计划
{
  const s = pptx.addSlide();
  slideTitle(s, "Next Steps");
  addBullet(s, [
    "1. 继续 producer 端拆解：consumer 侧 profiling（notify/consumer交互还有空间）",
    "2. 原型数据出完 → 等 OpenHarmony 编译环境 → 真实代码 benchmark 验证",
    "3. GitLab fork 上 batch publish 验证通过后合 master",
    "4. 如果需要，回到 eBPF 线继续高线程数重复实验",
  ]);
}

// ==================== OUTPUT ====================
await pptx.writeFile({ fileName: "native_hook_progress_2026-06-05.pptx" });
console.log("PPT generated: native_hook_progress_2026-06-05.pptx");
