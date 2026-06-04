# 2026-06-03 Compute Pass 基线报告

## 摘要

本报告记录第一个 GPU compute pass demo 的基线状态。当前 demo 已能在 PIE 中调度 `SimpleComputeShader.usf`，并把 `1024 x 1024` 的渐变结果写入 `RT_GPUComputeOutput`。

这份基线用于后续对比 GPU instance data path、indirect draw 和 GPU culling 阶段。

## Dispatch 日志

关键日志：

```text
GPUDrivenPipeline: Dispatched SimpleComputeShader to RT_GPUComputeOutput (1024x1024, groups=128,128,1, cpu-dispatch=0.004 ms).
```

含义：

- RenderTarget 尺寸为 `1024 x 1024`。
- shader 使用 `8 x 8` 线程组。
- dispatch group 为 `128 x 128 x 1`。
- `cpu-dispatch` 是 CPU 侧提交耗时，不是真实 GPU 执行耗时。

## 已验证内容

- 蓝图可以调用插件函数。
- 渲染线程可以拿到 RenderTarget UAV。
- compute shader 可以写入 RenderTarget。
- 材质可以采样 RenderTarget 并显示到平面。
- Demo 不再触发 D3D device removed。

## 未证明内容

本报告还没有证明：

- GPU 实际执行耗时。
- GPU structured buffer 上传。
- GPU 读取实例数据。
- indirect draw。
- GPU culling。

这些内容属于后续阶段。

## 后续动作

下一阶段进入 GPU 实例数据路径开发，目标是验证 CPU 生成的一批实例结构体可以上传到 GPU，并被 compute shader 正确读取。
