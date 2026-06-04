# UE CPU-GPU 交互：RHI 层与渲染线程

## 概述

本篇结合项目源码，讲解 UE 中 CPU 与 GPU 交互的三层架构：游戏线程、渲染线程和 RHI 层。

## 对应源码文件

- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/ComputeShaderInterface.cpp`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/GPUDrivenInstanceBufferInterface.cpp`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/SimpleComputeShader.h`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/SimpleComputeShader.cpp`
- `Plugins/GPUDrivenPipeline/Shaders/SimpleComputeShader.usf`
- `Plugins/GPUDrivenPipeline/Shaders/InstanceDataValidation.usf`

## 三层架构

```
游戏线程 (Game Thread)
  │
  │ ENQUEUE_RENDER_COMMAND
  ▼
渲染线程 (Render Thread)
  │
  │ FRHICommandList
  ▼
RHI 层 (Render Hardware Interface)
  │
  │ DX12 / Vulkan / Metal
  ▼
GPU
```

## 第一层：游戏线程 → 渲染线程

### ENQUEUE_RENDER_COMMAND

`ComputeShaderInterface.cpp:59`：

```cpp
ENQUEUE_RENDER_COMMAND(GPUDrivenPipeline_ExecuteSimpleComputeShader)(
    [RenderTargetResource, TextureSize, RenderTargetName](FRHICommandListImmediate& RHICmdList)
    {
        // 这里的代码在渲染线程执行
    });
```

作用：把 lambda 放进渲染线程的命令队列。游戏线程不会等它执行完，只是"投递"一个任务。

原因：GPU 资源不是线程安全的。只有渲染线程才能操作 GPU 资源。

## 第二层：渲染线程与 RHI 命令列表

### FRHICommandListImmediate

渲染线程的"画笔"，封装所有发给 GPU 的命令。通过它来：

- 创建资源（Buffer、Texture）
- 设置状态（资源转换）
- 提交工作（Dispatch、Draw）

`ComputeShaderInterface.cpp:89-92`：

```cpp
// 资源状态转换
RHICmdList.Transition(FRHITransitionInfo(
    RenderTargetTexture,
    ERHIAccess::SRVMask,      // 之前：被采样（读）
    ERHIAccess::UAVCompute    // 现在：被 compute shader 写
));

// 执行 compute shader
FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, PassParameters, GroupCount);

// 再次转换
RHICmdList.Transition(FRHITransitionInfo(
    RenderTargetTexture,
    ERHIAccess::UAVCompute,
    ERHIAccess::SRVMask
));
```

### 资源状态转换

GPU 是流水线式的，需要知道资源当前状态：

- `ERHIAccess::SRVMask`：只能读
- `ERHIAccess::UAVCompute`：compute shader 可写

状态不对会导致崩溃或未定义行为。

## 第三层：RHI 抽象层

### 统一接口

RHI 层统一了不同图形 API：

```
你的代码 → RHI 接口 → DX12 实现（Windows）
                     → Vulkan 实现（Android/Linux）
                     → Metal 实现（Mac/iOS）
```

### 关键 RHI 类型

`SimpleComputeShader.h` 中的参数声明：

```cpp
BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
    SHADER_PARAMETER_UAV(RWTexture2D<float4>, OutputTexture)  // 可读写纹理
    SHADER_PARAMETER(FVector2f, TextureSize)                   // 普通常量
END_SHADER_PARAMETER_STRUCT()
```

| 参数类型 | GPU 含义 |
|---------|---------|
| `SHADER_PARAMETER` | 普通常量，CPU → GPU |
| `SHADER_PARAMETER_UAV` | 可读写 GPU 资源视图 |
| `SHADER_PARAMETER_SRV` | 只读 GPU 资源视图 |

### Shader 注册

`SimpleComputeShader.cpp:7-10`：

```cpp
IMPLEMENT_GLOBAL_SHADER(
    FSimpleComputeShader,
    "/Plugin/GPUDrivenPipeline/SimpleComputeShader.usf",
    "MainCS",
    SF_Compute
);
```

告诉 shader 编译系统：编译 `FSimpleComputeShader` 时去找 `.usf` 文件的 `MainCS` 函数。

## 完整流程：实例数据上传

以 `GPUDrivenInstanceBufferInterface.cpp` 为例：

1. 游戏线程生成测试数据（CPU 数组）
2. 投递渲染命令 `ENQUEUE_RENDER_COMMAND`
3. 渲染线程创建 GPU Buffer（CPU 数组 → 显存）
4. 创建 SRV（shader 可读）
5. 创建 Summary Buffer + UAV（shader 可写）
6. 绑定参数
7. Dispatch compute shader
8. 资源状态转换 + 发起 readback
9. GPU 执行 shader，写入结果
10. Readback 读回 CPU

## 关键概念总结

| 概念 | 源码位置 |
|------|---------|
| ENQUEUE_RENDER_COMMAND | `ComputeShaderInterface.cpp:59` |
| 资源状态转换 | `RHICmdList.Transition()` |
| Structured Buffer | `CreateBufferFromArray` |
| SRV | `CreateShaderResourceView` |
| UAV | `CreateUnorderedAccessView` |
| Readback | `FRHIGPUBufferReadback` |
| FlushRenderingCommands | `GPUDrivenInstanceBufferInterface.cpp:94` |

## 最容易踩的坑

1. 在游戏线程直接访问 GPU 资源 — 必须通过 `ENQUEUE_RENDER_COMMAND`
2. 忘记资源状态转换 — 不告诉 GPU 状态变化会崩溃
3. UAV 没开 — RenderTarget 创建时没勾 `bSupportsUAV`
4. Readback 时机不对 — GPU 还没写完就去读
5. 线程安全 — 共享变量需要用 `FCriticalSection` 或 `TAtomic`
