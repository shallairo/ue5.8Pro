# 2026-06-03 20:07 Compute Pass 基线采集

## 本次做了什么

本次完成第一个 GPU pass demo 的基线采集。Demo 使用 `SimpleComputeShader.usf` 写入 `RT_GPUComputeOutput`，并通过材质显示在关卡平面上。

## 相关源码

- [ComputeShaderInterface.cpp](../../Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/ComputeShaderInterface.cpp)
- [SimpleComputeShader.h](../../Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/SimpleComputeShader.h)
- [SimpleComputeShader.cpp](../../Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/SimpleComputeShader.cpp)
- [SimpleComputeShader.usf](../../Plugins/GPUDrivenPipeline/Shaders/SimpleComputeShader.usf)

## 核心代码链路

蓝图调用：

```cpp
UComputeShaderInterface::ExecuteSimpleComputeShader(...)
```

C++ 侧会检查 RenderTarget 是否有效、是否支持 UAV，然后把真正的工作通过：

```cpp
ENQUEUE_RENDER_COMMAND(...)
```

交给渲染线程。

渲染线程中使用：

```cpp
FComputeShaderUtils::Dispatch(...)
```

调度 shader。

## Shader 做了什么

`SimpleComputeShader.usf` 中每个线程处理一个像素，根据像素坐标计算 UV：

```hlsl
float2 UV = float2(ThreadID.xy) / float2(Width, Height);
float4 Color = float4(UV.x, UV.y, 0.5, 1.0);
OutputTexture[ThreadID.xy] = Color;
```

因此画面表现为红绿渐变。

## 基线的意义

基线不是为了证明性能已经很好，而是为了给后续阶段提供对照。之后加入 structured buffer、instance validation、indirect draw 或 GPU culling 时，都可以和这次最小 pass 做比较。

## 重要区别

`cpu-dispatch` 只表示 CPU 提交 dispatch 的耗时，不表示 GPU 真实执行时间。真实 GPU 时间需要使用 `stat gpu`、RenderDoc、PIX 或 Unreal Insights 进一步测量。
