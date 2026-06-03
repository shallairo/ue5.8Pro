# Compute Pass 基线采集

## 我们验证了什么

这次为第一条 GPU pass demo 完成了基线采集。`SimpleComputeShader.usf` 会把红绿 UV 渐变写入 `RT_GPUComputeOutput`，Output Log 也确认该 pass 已经针对 `1024 x 1024` 的 Render Target 发起 dispatch，线程组数量为 `128 x 128 x 1`。

这说明当前 demo 已经不只是代码层面的实验，而是具备了可见输出和可重复观测的验证入口。

## 关键概念

一次 compute shader dispatch 至少有两类不同的时间概念：

1. CPU dispatch timing：CPU 提交 GPU 命令所花的时间。
2. GPU elapsed time：GPU 真正执行该 shader 工作所花的时间。

当前 `GetLastExecutionTime()` 返回的是 CPU 侧 dispatch timing。我们这次看到的 `0.004 ms` 可以说明命令提交本身很轻，但不能说明 `SimpleComputeShader` 在 GPU 上实际只执行了 `0.004 ms`。

如果之后要精确衡量 shader 自身的 GPU 耗时，需要使用 RenderDoc、PIX、Unreal Insights GPU trace，或者在插件里加入 GPU timestamp query。

## 为什么要做基线

这个项目的目标是 GPU-driven rendering，所以后续每一步优化都需要有对照数据。基线的意义不是证明当前 demo 很快，而是记录一个“已知可运行、已知可观测”的初始状态。

这次记录下来的维度包括：

- Frame time
- Game thread time
- Render thread / Draw time
- GPU time
- Draw count
- Primitive count
- Render Target 尺寸
- Dispatch group count
- CPU dispatch timing

等后续加入 structured instance buffer、indirect draw arguments、GPU culling 之后，就可以和这个状态做对比。

## 重要坑点

`stat gpu` 里的 compute queue 并不等于 `SimpleComputeShader` 的单独耗时。当前项目开启了 Lumen、TSR 等现代渲染特性，它们也会在 GPU compute queue 中产生大量工作。

因此，这次 `stat gpu` 数据适合作为场景级 baseline，但不能当成 shader 级别 profiling 结果。

另一个容易混淆的点是：Output Log 里的 `cpu-dispatch=0.004 ms` 只是 CPU 提交命令的耗时。它是验证日志，不是 GPU 性能结论。

## 可复用结论

第一条 compute pass 已经可以作为后续开发的验证锚点。下一步应该构建 GPU-visible instance data path，也就是把一批实例元数据上传到 GPU buffer。

这个阶段要和当前的渐变 RT pass 保持解耦：渐变 pass 负责验证 compute dispatch 和 UAV 写入能力，instance data path 负责为后面的 GPU culling 和 indirect draw 打地基。
