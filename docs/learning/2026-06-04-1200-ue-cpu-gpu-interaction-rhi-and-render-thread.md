# UE CPU-GPU 交互：三层架构与渲染管线

## 本篇定位

本文全面讲解 UE 中 CPU 和 GPU 之间如何协同工作。理解这三层架构（游戏线程 → 渲染线程 → RHI 层 → GPU）是开发 GPU-driven 渲染功能的**绝对前提**。

通过本篇的学习，你应该能回答以下问题：
- UE 为什么要用三个线程处理渲染？它们各自做什么？
- `ENQUEUE_RENDER_COMMAND` 投递到渲染线程后，命令是怎么到达 GPU 的？
- 什么是 RHI 抽象层？DX12/Vulkan/Metal 切换到不同的后端时，我们的代码需要改吗？
- 状态转换（Transition）在 GPU 流水线中的语义是什么？
- `FlushRenderingCommands` 为什么是"重型操作"？

---

## 一、三层架构概览

```
┌─────────────────────────────────────────────────────────────┐
│ 游戏线程 (Game Thread)                                        │
│ 用途：游戏逻辑、物理、AI、蓝图执行、UObject 生命周期管理        │
│ 对 GPU 的操作：只能"排队"，不能直接访问                        │
│ 关键函数：ExecuteSimpleComputeShader() 等蓝图入口             │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        │ ENQUEUE_RENDER_COMMAND / FRenderCommandDispatcher::Enqueue
                        │ 投递到线程安全的命令管道
                        ▼
┌─────────────────────────────────────────────────────────────┐
│ 渲染线程 (Render Thread)                                      │
│ 用途：场景剔除、绘制列表排序、执行渲染命令                      │
│ 对 GPU 的操作：通过 FRHICommandListImmediate 记录命令           │
│ 关键对象：FSceneRenderer、FRHICommandListImmediate             │
│ 本项目的渲染：lambda 内部调用 RHI 命令                        │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        │ FRHICommandListImmediate.Submit() / ImmediateFlush()
                        │ 翻译为具体图形 API 调用（DX12/Vulkan）
                        ▼
┌─────────────────────────────────────────────────────────────┐
│ RHI 层 (Render Hardware Interface)                           │
│ 用途：统一抽象层，屏蔽 DX12/Vulkan/Metal 的差异                │
│ 关键对象：FRHIBuffer、FRHITexture、FRHIShaderResourceView      │
│ 本项目的使用：RHICmdList.Transition()、CreateBufferFromArray() │
└───────────────────────┬─────────────────────────────────────┘
                        │
                        │ DX12 API / Vulkan API / Metal API
                        ▼
┌─────────────────────────────────────────────────────────────┐
│ GPU                                                         │
│ 用途：执行实际的图形/计算流水线                                │
│ 数据来源：显存中的 Vertex Buffer、Index Buffer、Texture       │
│ 执行方式：完全异步（Dispatch/Draw 提交后不等待完成）           │
└─────────────────────────────────────────────────────────────┘
```

---

## 二、分层详解（基于项目代码）

### 2.1 游戏线程（Game Thread）

**定义：** UE 的主线程，负责所有游戏逻辑。蓝图的 `Event Tick`、`BeginPlay`、`Event Graph` 都在此执行。

**本项目中的表现：**
- `ComputeShaderInterface.cpp:16` — `ExecuteSimpleComputeShader()` 在游戏线程被蓝图调用
- `GPUDrivenInstanceBufferInterface.cpp:111` — `UploadTestInstanceData()` 也在游戏线程
- **关键约束：** 游戏线程**不能**直接操作 GPU 资源。如果尝试在游戏线程获取 UAV 指针或调用 Dispatch，会导致崩溃或未定义行为。

**解决方案：** 使用 `ENQUEUE_RENDER_COMMAND` 把 GPU 操作"包装"成命令投递到渲染线程。

### 2.2 渲染线程（Render Thread）

**定义：** 引擎启动时创建的一个独立线程（也叫 `RenderingThread`），专门负责处理所有与 GPU 资源相关的操作。

**本项目中的表现：**
- 所有 `ENQUEUE_RENDER_COMMAND` 中的 lambda 都在渲染线程执行
- lambda 中可以使用 `FRHICommandListImmediate& RHICmdList` 来：
  - 创建/销毁 GPU Buffer 和 Texture（`CreateBufferFromArray`）
  - 设置资源状态（`Transition`）
  - 调度 compute shader（`FComputeShaderUtils::Dispatch`）
  - 提交 draw call（`DrawPrimitiveIndirect`）

**渲染线程如何获取 lambda：**

```
游戏线程                                          渲染线程
───────                                          ───────
FRenderCommandDispatcher::Enqueue(lambda)  ───→  FRenderThreadCommandPipe
                                                  (线程安全队列)
                                                    │
                                                 渲染线程主循环:
                                                    │
                                                 取出队列中的下一个命令
                                                    │
                                                 执行 lambda(RHICmdList)
                                                    │
                                                 记录 RHI 命令到 RHICmdList
                                                    │
                                                 处理下一个命令
```

### 2.3 ENQUEUE_RENDER_COMMAND 宏展开

以 `ComputeShaderInterface.cpp:59-61` 为例：

```cpp
ENQUEUE_RENDER_COMMAND(GPUDrivenPipeline_ExecuteSimpleComputeShader)(
    [RenderTargetResource, TextureSize, RenderTargetName](FRHICommandListImmediate& RHICmdList)
    {
        // ... 渲染线程执行体
    });
```

宏展开后（简化版）相当于：

```cpp
// 1. 定义一个独特的命令标签类型
struct TSTR_GPUDrivenPipeline_ExecuteSimpleComputeShader_SOME_LINE_NUMBER { ... };

// 2. 通过 FRenderCommandDispatcher::Enqueue 压入队列
FRenderCommandDispatcher::Enqueue<TRenderCommandTag<...>>(
    TUniqueFunction<void(FRHICommandListImmediate&)>(
        [RenderTargetResource, ...](FRHICommandListImmediate& RHICmdList) { ... }
    )
);
```

**`FRenderCommandDispatcher::Enqueue` 的实现（`RenderingThread.h:992`）：**

```cpp
template <typename RenderCommandTag>
static void Enqueue(TUniqueFunction<void(FRHICommandListImmediate&)>&& Function)
{
    if (FRenderCommandList* CommandList = FRenderCommandList::GetInstanceTLS())
    {
        CommandList->Enqueue<RenderCommandTag>(MoveTemp(Function));
        return;
    }
    FRenderThreadCommandPipe::Enqueue<RenderCommandTag>(MoveTemp(Function));
}
```

如果当前有嵌套的命令列表上下文（比如在渲染 pass 内部再派发命令），就会加入嵌套列表，否则加入全局渲染线程命令管道。

---

## 三、RHI 层详解

### 3.1 RHI 是什么

RHI = **Render Hardware Interface**。它是 UE 对底层图形 API 的统一封装层。

```
你的 C++ 渲染代码
       ↓   (只使用 RHI 接口)
┌──────────────────┐
│  RHI Layer       │  ← UE 内部
│  FRHICmdList     │
│  FRHIBuffer      │
│  FRHITexture     │
│  FRHIShader      │
└──────┬───────────┘
       │
       ├──→ D3D12RHI.dll  (Windows)
       ├──→ VulkanRHI.dll  (Android / Linux)
       └──→ MetalRHI.dll   (macOS / iOS)
```

**这意味着：** 只要使用 RHI 接口编写渲染代码，就可以在 DX12、Vulkan、Metal 等不同图形 API 上运行，不需要改动 C++ 端代码。

### 3.2 本项目中常见的 RHI 类型映射

| UE RHI 类型 | 对应 DX12 对象 | 用途 |
|-------------|---------------|------|
| `FRHIBuffer` | `ID3D12Resource` (Buffer) | GPU 显存缓冲 |
| `FRHITexture` | `ID3D12Resource` (Texture2D) | GPU 纹理 |
| `FShaderResourceViewRHIRef` | `D3D12_SHADER_RESOURCE_VIEW_DESC` | Shader 只读视图 |
| `FUnorderedAccessViewRHIRef` | `D3D12_UNORDERED_ACCESS_VIEW_DESC` | Shader 读写视图 |
| `FRHICommandListImmediate` | `ID3D12GraphicsCommandList` | 命令列表 |
| `FRHIGPUBufferReadback` | Staging Readback Buffer | GPU → CPU 回读 |

### 3.3 RHI 核心操作示例

#### 创建 Buffer

```cpp
// GPUDrivenInstanceBufferInterface.cpp:164-170
FBufferRHIRef InstanceBuffer = UE::RHIResourceUtils::CreateBufferFromArray<FGPUDrivenInstanceData>(
    RHICmdList,
    TEXT("GPUDrivenPipeline.InstanceData"),     // Debug 名称（PIX/RenderDoc 可见）
    EBufferUsageFlags::StructuredBuffer | EBufferUsageFlags::ShaderResource,  // 用途
    ERHIAccess::SRVCompute,                     // 初始状态
    TConstArrayView<FGPUDrivenInstanceData>(Instances));  // CPU 数据
```

在 D3D12 后端，这个函数内部会：
1. 计算 Buffer 大小：`sizeof(FGPUDrivenInstanceData) * Num = 32 * 1024 = 32768 bytes`
2. 调用 `CreateCommittedResource` 创建 `ID3D12Resource`，设置初始状态
3. 调用 `WriteToSubresource` 或上传堆复制 CPU 数据到显存
4. 返回 `FRHIBuffer` 句柄

#### 资源状态转换

```cpp
// ComputeShaderInterface.cpp:92
RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture,
    ERHIAccess::SRVMask,     // 当前状态：只读（材质采样）
    ERHIAccess::UAVCompute   // 目标状态：compute 写入
));
```

在 D3D12 后端，这会产生：
```cpp
D3D12_RESOURCE_BARRIER barrier = {};
barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
// 加入命令列表
CommandList->ResourceBarrier(1, &barrier);
```

---

## 四、三层交互时序图（真正的全景）

以下用**本项目的 IndirectDraw MVP** 流程为例展示三层交互：

```
游戏线程                 渲染线程                    RHI 层 (DX12)            GPU
────────                ────────                  ────────────            ───────
ExecuteTestIndirect
InstanceDraw(RT, 1024)
   │
   ├─ 生成实例数据 (CPU 数组)
   │
   ├─ ENQUEUE_RENDER_COMMAND ─────────────────────┐
   │   lambda                                    │
   │   (此时代码立即返回)                          │
   ▼                                             ▼
  继续游戏逻辑                          lambda 被执行
                                           │
                                           ├─ CreateBufferFromArray
                                           │   → FRHIBuffer (InstanceData)
                                           │       ↓
                                           │   D3D12: CreateCommittedResource
                                           │   + WriteToSubresource
                                           │
                                           ├─ Create SRV/UAV
                                           │   → FRHIShaderResourceView
                                           │
                                           ├─ Dispatch(args_shader)
                                           │   → RHICmdList.DispatchComputeShader
                                           │       ↓
                                           │   D3D12: Dispatch(1,1,1)
                                           │
                                           ├─ Transition: UAV→IndirectArgs ─────→ GPU Barrier
                                           │
                                           ├─ BeginRenderPass ───────────────────→ OMSetRenderTargets
                                           │
                                           ├─ SetGraphicsPipelineState
                                           │   → PSO 绑定
                                           │
                                           ├─ DrawPrimitiveIndirect ────────────→ ExecuteIndirect
                                           │                                       ↓
                                           │                                     GPU 执行绘制
                                           │
                                           └─ EndRenderPass ────────────────────→ Resolve
                                                                                    ↓
                                                                            画面显示在 RT
```

**关键观察：** 游戏线程在 `ENQUEUE_RENDER_COMMAND` 后立即返回，不等待渲染线程完成。这就是"异步"的含义。

---

## 五、补充知识点

### 5.1 为什么不把所有工作放在游戏线程

假设用一个线程处理所有逻辑和渲染：

```text
帧 1: [逻辑 5ms] [渲染准备 2ms] [GPU 等待 3ms]  →  10ms
帧 2: [逻辑 5ms] [渲染准备 2ms] [GPU 等待 3ms]  →  10ms
```

```text
                    CPU 空闲           CPU 空闲
时间:  |逻辑|渲染|████|逻辑|渲染|████|
       ↑ 帧 1       ↑ 帧 2
```

使用双线程：

```text
游戏线程: |逻辑 1|████|逻辑 2|████|逻辑 3|   (每帧 5ms)
渲染线程: |采1|渲染1|██|采2|渲染2|██|采3|   (每帧 3ms)
GPU:      |█ 执行1 █|███|█ 执行2 █|███|█ 执行3 █
```

**CPU 空闲时间大幅减少 = 更高的帧率。**

这就是 UE 三层架构（游戏线程 + 渲染线程 + RHI 线程）背后的核心动机：**让 CPU 和 GPU 流水线并行工作**。

### 5.2 ERHIAccess 状态转换的完整语义

项目中用到的状态：

| ERHIAccess | 可读 | 可写 | 用于 |
|-----------|------|------|------|
| `SRVMask` | 顶点/像素/计算 shader 读 | 否 | 纹理被材质采样时 |
| `SRVCompute` | 计算 shader 读 | 否 | 实例数据被 compute shader 读取 |
| `SRVGraphics` | 顶点/像素 shader 读 | 否 | 可见实例被 vertex shader 读取 |
| `UAVCompute` | 计算 shader 读写 | 计算 shader 写 | Compute Shader 输出 |
| `RTV` | 否 | 像素 shader 写 | RenderTarget 写入 |
| `IndirectArgs` | GPU 读（indirect draw） | 否 | DrawIndirect 参数缓冲 |
| `CopySrc` | GPU copy 操作读 | 否 | Readback 前的源 |

**状态转换图（本项目的典型路径）：**

```
纹理 (RenderTarget):
  SRVMask ←→ UAVCompute ←→ RTV ←→ SRVMask
    (材质)    (compute 写)   (draw 写)  (回到材质)

Buffer (InstanceData):
  SRVCompute ←→ (保持只读)

Buffer (Summary/IndirectArgs):
  UAVCompute ←→ CopySrc    (readback)
  UAVCompute ←→ IndirectArgs  (draw indirect)
```

### 5.3 为什么会死锁

这是一个新手常见的死锁场景：
```cpp
// 游戏线程
ENQUEUE_RENDER_COMMAND(Work)(
    [](FRHICommandListImmediate& RHICmdList) {
        // 渲染线程执行
    });
FlushRenderingCommands();  // 游戏线程阻塞等待渲染线程
```

如果在渲染线程内部也调用了 `FlushRenderingCommands()`，就会死锁：
```
游戏线程等待渲染线程完成 → 渲染线程在 Flush 中等待自己完成 → 永远死锁
```

所以 **`FlushRenderingCommands` 只能在游戏线程调用**。这也意味着 readback 过程一定会阻塞游戏线程。

### 5.4 FGlobalShader 的生命周期和线程安全性

本项目中所有 shader（`FSimpleComputeShader`、`FFrustumCullInstancesShader` 等）都是 `FGlobalShader`：

```cpp
// 注册为全局 shader
IMPLEMENT_GLOBAL_SHADER(FMyShader, "...", "MainCS", SF_Compute);
```

**特点：**
- 在引擎启动时按需编译
- 缓存在 `FGlobalShaderMap` 中（引擎级单例）
- 通过 `GetGlobalShaderMap(GMaxRHIFeatureLevel)` 获取
- 是只读的，线程安全
- 跨关卡/世界持续存在，无需手动管理

**什么时候会重新编译？**
- 修改 `.usf` 文件并重启编辑器
- 使用 `r.ShaderDevelopmentMode=1` + `Ctrl+Shift+.(句号)` 热重载
- `Platfroms > Reload Shaders`

### 5.5 RHICmdList 中的命令缓存机制

```cpp
// 这些命令不会立即执行，只是被记录到 RHICmdList 内部的内存池中
RHICmdList.Transition(...);                // 记录 Transition 命令
RHICmdList.DispatchComputeShader(...);     // 记录 Dispatch 命令
RHICmdList.BeginRenderPass(...);           // 记录 RenderPass 命令

// 实际提交到 GPU 的方式：
// 1. RHICmdList 在渲染线程帧结束时自动 Submit
// 2. 手动调用 ImmediateFlush()
// 3. RHI 线程从命令列表拉取执行
```

这类似于**命令缓冲模式（Command Buffer Pattern）**：所有 API 调用先记录，然后一批批提交给驱动，减少 CPU/GPU 之间的调用开销。

---

## 六、常见问题排查

| 问题 | 根因 | 解决方案 |
|------|------|----------|
| 在游戏线程调用 RHI 函数崩溃 | 线程安全违规 | 通过 ENQUEUE_RENDER_COMMAND 投递 |
| 渲染结果不对或无变化 | 忘记 Flush 或资源状态错 | 检查 Transition 顺序和状态 |
| GPU crash / Device Removed | 资源状态转换错误 | 确认每个 Transition 的 From/To 正确 |
| Readback 数据为 0 | GPU 还没写完就读 | 检查 IsReady() 或增加 Delay |
| Readback 卡死 | 死锁或资源尚未释放 | 检查上一轮 Readback 是否已经 Unlock |
