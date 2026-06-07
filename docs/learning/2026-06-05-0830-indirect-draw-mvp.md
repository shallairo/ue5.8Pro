# Indirect Draw MVP 学习记录

## 这次做了什么

这次实现的是项目里的第一条最小 indirect draw 路径。

核心目标不是接入 UE 的完整 StaticMesh 渲染，而是先在插件内部证明两件事：

1. GPU compute shader 可以写出 `DrawPrimitiveIndirect` 参数。
2. graphics pipeline 可以真正消费这份参数并把实例画到 `RT_GPUComputeOutput`。

相关源码：

- [GPUDrivenIndirectDrawInterface.cpp](/E:/unrealProject/pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/GPUDrivenIndirectDrawInterface.cpp:1)
- [IndirectDrawShaders.h](/E:/unrealProject/pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/IndirectDrawShaders.h:1)
- [IndirectDrawShaders.cpp](/E:/unrealProject/pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/IndirectDrawShaders.cpp:1)
- [IndirectDrawInstances.usf](/E:/unrealProject/pro/Plugins/GPUDrivenPipeline/Shaders/IndirectDrawInstances.usf:1)

## 核心链路

这一版的链路是：

```text
CPU 生成测试实例
-> 上传 StructuredBuffer<FGPUDrivenInstanceData>
-> compute shader 写 indirect args buffer
-> graphics shader 用 DrawPrimitiveIndirect 绘制
-> 结果输出到 RT_GPUComputeOutput
```

这里和之前的 `instance validation path` 最大不同在于：

- 之前是 GPU 读数据后写 summary，再 readback 回 CPU
- 这次是 GPU 读数据后直接驱动 draw

也就是说，验证目标从“GPU 能处理实例数据”推进到了“GPU 处理完数据后，能继续推动真正的渲染提交”。

## 最重要的几个概念

### 1. indirect args buffer 是什么

`DrawPrimitiveIndirect` 需要一段符合固定布局的参数缓冲。

在 UE/RHI 里，对应的是：

```cpp
struct FRHIDrawIndirectParameters
{
    uint32 VertexCountPerInstance;
    uint32 InstanceCount;
    uint32 StartVertexLocation;
    uint32 StartInstanceLocation;
};
```

这次 compute shader 写出的就是这 4 个 `uint`：

- `VertexCountPerInstance = 6`
- `InstanceCount = 输入实例数`
- `StartVertexLocation = 0`
- `StartInstanceLocation = 0`

因为每个实例画的是一个 quad，而 quad 由两个三角形组成，所以一共需要 `6` 个顶点。

### 2. 为什么 vertex shader 不需要顶点缓冲

这次 graphics shader 使用了：

- `SV_VertexID`
- `SV_InstanceID`

因此不需要传统的 vertex buffer 来存几何体顶点。

做法是：

- 用 `SV_VertexID % 6` 在 shader 里直接生成一个 quad 的 6 个顶点偏移
- 用 `SV_InstanceID` 读取 `StructuredBuffer<FGPUDrivenInstanceData>` 中对应实例的数据

这也是为什么代码里使用了 `GEmptyVertexDeclaration`。当前 MVP 的几何不是从 CPU 顶点流中取，而是由 shader 程序化生成。

### 3. 为什么先画到 RenderTarget

这一步没有直接接 UE 的 StaticMesh 渲染路径，是因为那样会一下子引入更多复杂问题：

- 场景代理
- mesh/material 绑定
- 可见性系统
- draw list 集成

当前先把结果画到 `RT_GPUComputeOutput`，可以让我们只聚焦于：

- indirect args 是否正确
- shader 读取实例数据是否正确
- draw 是否真的被 GPU 提交执行

这是一种很适合原型阶段的“缩小战场”方法。

## 代码里最值得记住的点

### `UGPUDrivenIndirectDrawInterface::ExecuteTestIndirectInstanceDraw`

这个函数负责：

- 检查 RHI 是否支持 draw indirect
- 生成测试实例数据
- 创建 instance structured buffer
- 创建 indirect args buffer
- dispatch compute shader 写 indirect 参数
- 开启 render pass 并调用 `RHICmdList.DrawPrimitiveIndirect`

它是当前 indirect draw MVP 的主入口。

### `FIndirectDrawArgsShader`

这是最小 compute shader。

它的任务非常单纯：向 args buffer 写 4 个整数。

虽然逻辑简单，但意义很大，因为这代表：

```text
draw 参数已经由 GPU 生产，而不是 CPU 直接填结构体后立刻调用 draw
```

### `FIndirectDrawInstanceVS`

这个 vertex shader 做了两件事：

1. 根据 `SV_InstanceID` 读取实例数据
2. 根据 `SV_VertexID` 生成 quad 的顶点偏移

再把实例的位置映射到 RenderTarget 的 NDC 坐标范围里。

## 这一步最容易踩的坑

### 1. 画面被旧的 compute pass 覆盖

如果蓝图里同时调用旧的渐变 pass 和新的 indirect draw pass，后执行的那个会覆盖前一个对 `RT_GPUComputeOutput` 的写入结果。

所以测试时最好只保留一个最终写入节点。

### 2. 把 CPU dispatch time 当成 GPU 性能

日志里的 `cpu-compute` 和 `cpu-draw` 只是 CPU 提交命令的耗时，不是 GPU 实际执行时间。

如果后续要做真正的性能分析，还是要看：

- `stat gpu`
- RenderDoc
- PIX
- Unreal Insights GPU trace

### 3. 资源状态转换出错

这一类路径最敏感的地方是 resource transition。

当前代码里至少涉及：

- indirect args buffer：从 `UAVCompute` 切到 `IndirectArgs`
- render target：从 `SRVMask` 切到 `RTV`，绘制后再切回 `SRVMask`

如果这里状态错了，很容易出现黑屏、RHI warning，甚至驱动报错。

## 当前阶段的结论

这一步如果验证通过，项目状态就从：

```text
GPU 可以处理实例数据
```

推进到：

```text
GPU 可以写 draw 参数并驱动最小 draw
```

这已经是进入真正 GPU-driven renderer 之前非常关键的拐点。

下一步就可以继续往这两个方向之一推进：

1. 让 indirect draw 读取更真实的可见实例列表
2. 在 indirect draw 之前加入 frustum culling compute pass
