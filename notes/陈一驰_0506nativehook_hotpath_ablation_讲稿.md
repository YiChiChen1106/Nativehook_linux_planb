# 陈一驰_0506 native_hook hot-path ablation 口语版讲稿

## 第 1 页：标题页

今天我讲一下这周做的新实验，主要是 producer hot-path ablation。

这个名字听起来有点长，其实意思很简单：我不再继续扫 sample_interval、filter_size 这些参数了，而是把 producer 里面的路径一段一段拆开，看看到底是哪一步最拖性能。

前面我们已经看到，record 数量可以降下来。但问题是，record 降了以后，吞吐没有一直跟着涨。所以这周我想先搞清楚：剩下的 overhead 到底卡在哪儿。

这一页图可以这么带：后面所有实验都围绕下面这 6 段，从 hook entry 一直拆到 notify。

## 第 2 页：为什么这周不继续 sweep

先接一下上周的结果。

上周 sweep 其实已经说明一件事：sample_interval 和 filter_size 是有用的。它们确实能把 record 数量压下去，这个方向没问题。

但另一个现象也挺明显：record 继续变少以后，throughput 很快就不怎么涨了。也就是说，后面剩下的开销，可能不是 consumer 那边单独造成的，也不只是 shared memory 传输的问题。

所以我这周没有继续加参数组合。再扫下去可能会有更多数字，但不一定能回答“瓶颈在哪”。我就换了一个做法，直接拆 producer 这条热路径。

这一页讲图的时候不用细读每个点，只说趋势就够了：参数有效，但吞吐恢复到一段后就平台化了。

## 第 3 页：这周想问的问题

这周我真正想问的问题是：producer 里面哪一步最贵？

有可能一进 malloc/free hook 就已经有成本；也有可能是 guard 比较重；也可能是 mutex、tracking、record write，或者最后 notify consumer 这一步。

所以我用了一个比较笨但直观的方法：一层一层打开。Stage 1 只保留 hook entry，Stage 2 加 guard，Stage 3 加 mutex，后面再加 tracking、record write、notify。

这样每多加一层，我就看一下吞吐掉了多少。掉得多的地方，就说明这一层比较可疑。

这一页可以按箭头从左往右扫一遍，不用展开太多实现细节。

## 第 4 页：6 个 stage 是怎么拆的

这张表就是具体拆法。

Stage 1 最轻，只是让 malloc/free 先进我们的 wrapper，然后直接返回。Stage 2 加了 reentry guard，主要是防止 hook 自己里面又触发 hook。Stage 3 开始进 HookWriter，并且加 mutex。

Stage 4 加 allocation tracking。这里的意思是，alloc 被我们保留下来了，free 的时候才跟着记录。Stage 5 再加 record write，也就是填 metadata，然后写到 shared memory 里面。Stage 6 就是完整路径，再加 eventfd notify 和 consumer drain。

这里我想特别解释一下 Stage 5。Stage 5 不 notify consumer，不然就会变成完整路径了。但如果完全没人消费，ring buffer 很快会满。所以我在 producer 侧做了一个实验用的 self-drain，只是为了让它能一直测写 record 的成本。

这页可以稍微慢一点讲，因为后面的结果都要基于这个 stage 定义。

## 第 5 页：单线程结果

先看单线程。

baseline 大概是 17.26M ops/s。只进 hook entry 以后，降到 14.91M。这个说明 hook wrapper 本身就有一点成本，不是完全免费的。

加 guard 后到 10.14M，再加 mutex 到 6.90M。这里已经有明显下降了。

但后面掉得更狠。加 tracking 后，只剩 1.81M；再加 record write 后，掉到 0.34M。最后从 Stage 5 到 Stage 6，也就是加 notify，0.34M 变成 0.31M。这个差距反而没那么夸张。

所以单线程这里，我现在的理解是：notify 不是最大头。真正比较重的是 tracking 和 record write。

讲图的时候，可以直接指两个地方：Stage 3 到 Stage 4，Stage 4 到 Stage 5。这两个断点最明显。

## 第 6 页：四线程结果

再看四线程，这里 mutex 的问题会更明显。

baseline 是 12.89M。Stage 1 基本差不多，这里有一点波动，我觉得不用过度解释。Stage 2 是 10.59M。然后一加 mutex，也就是 Stage 3，直接掉到 2.75M。

这个变化挺大的。它说明在多线程情况下，全局 mutex 很容易变成瓶颈。单线程里 mutex 只是 lock 和 unlock 的成本，但四线程里它会有竞争。

后面也是一样，加 tracking 后到 0.47M，加 record write 后到 0.08M，完整 notify 是 0.07M。

所以四线程下，我会把重点放在两个地方：一个是 mutex，另一个还是 tracking 和 record write。

这页讲图的时候，重点圈 Stage 2 到 Stage 3。这里就是四线程和单线程最大的不同。

## 第 7 页：看相邻 stage 掉了多少

刚才两页看的是绝对吞吐。绝对值有时候不太好判断，所以这一页换个角度，看“每新增一层，吞吐又掉了多少”。

单线程里，比较明显的是 Stage 3 到 Stage 4，还有 Stage 4 到 Stage 5。也就是 tracking 和 record write。

四线程里，Stage 2 到 Stage 3 也很明显。这就是 mutex 的影响被放大了。

这页给我的提示是，下一步别急着补 stack unwind。因为现在还没到 stack 那一步，producer 本地路径已经掉得很厉害了。更应该先继续拆 tracking、metadata、ring write、锁粒度这些东西。

讲图的时候不用读每个数字。就说柱子最长的几段，对应下一步最该看的地方。

## 第 8 页：notify 不是这轮最大增量

这里我想单独讲一下 notify。

一开始我也会担心，eventfd notify 加 consumer drain 会不会特别贵。因为完整 native_hook 里面，这一步看起来挺像一个同步点。

但这组结果里，Stage 5 到 Stage 6 的差距没有那么大。单线程是 0.34M 到 0.31M，四线程是 0.08M 到 0.07M。

再看 consumer 侧的指标，avg_batch 基本是 20，dropped 是 0。这说明 consumer batching 是正常的，ring 也没有大量丢数据。

所以我不会说 notify 没成本。更稳一点的说法是：在这组 record 很密集的实验里，最大的新增成本已经在 notify 之前出现了。

这一页可以先看左边 Stage 5 和 Stage 6 的差距，再看右边 metrics。重点就是 batch 正常、没有 dropped。

## 第 9 页：结论和下一步

最后简单收一下。

这组实验不是想证明 native_hook 最终一定是多少 overhead。它更像是一个定位实验，先看看矛头指向哪里。

目前看到的信号是：剩下的大头更像在 producer 本地热路径里。尤其是 mutex、allocation tracking、record write 这几块。

所以下一步我觉得可以继续往里拆。比如 tracking 里面，把 insert 和 erase 分开；metadata 里面，把 clock_gettime、getpid、gettid 分开；ring write 里面，只测 shared memory 写和 index 更新。锁这边也可以试一下 per-thread buffer 或者 sharded lock，看四线程会不会好一点。

最后我也想听一下老师意见：现在这个 tracking 是 `unordered_set + global mutex`，可能比真实 native_hook 粗一些。如果老师觉得这个差距会影响判断，我下一轮可以先把 tracking 结构改得更贴近 upstream，然后再重新跑一遍。

这一页不用把每个框都讲完。最后落到一句话就行：这周的结果把问题从 consumer 那边，往 producer 本地热路径又推进了一步。

