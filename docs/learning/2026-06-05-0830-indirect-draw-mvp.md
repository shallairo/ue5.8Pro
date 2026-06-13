# Indirect Draw MVP

## 本篇定位

这是项目的一个关键拐点。之前的所有工作（compute shader 写 RT、instance validation）证明了 GPU "可以处理数据"。而 Indirect Draw MVP 证明的是：**GPU 可以写 draw 参数并驱动实际的绘制提交**。

从这一篇开始，我们的渲染路径中出现了**两条 GPU 流水线的交叉**：
1. **Compute 流水线**：写 indirect args buffer
2. **Graphics 流水线**：消费 indirect args buffer 并绘制

理解这两条流水线的交互是掌握 GPU-driven rendering 的核心。

通过本篇的学习，你应该能回答以下问题：
- `DrawPrimitiveIndirect` 和普通的 `DrawPrimitive` 有什么区别？
- `FRHIDrawIndirectParameters` 的 4 个字段分别控制什么？
- Vertex Shader 为什么可以不依赖 Vertex Buffer？`SV_VertexID` 和 `SV_InstanceID` 如何配合使用？
- Compute Shader 写入的 indirect args 和 Graphics Pipeline 之间如何交接资源状态？
- 为什么间接绘制不需要 CPU 等待 GPU 完成 args 写入？

---

## 一、涉及源码总览

| 文件 | 行范围 | 职责 |
|------|--------|------|
| `GPUDrivenIndirectDrawInterface.h` | 全部 | **蓝图接口声明**：触发 indirect draw |
| `GPUDrivenIndirectDrawInterface.cpp` | L700-L867 | **主逻辑**：ExecuteTestIndirectInstanceDraw |
| `IndirectDrawShaders.h` | 全部 | **3 个 shader 类声明**：compute + VS + PS |
| `IndirectDrawShaders.cpp` | 全部 | **Shader 注册**：间接绘制相关的 shader 编译 |
| `IndirectDrawInstances.usf` | 全部 | **联合 shader 文件**：BuildIndirectArgsCS + MainVS + MainPS |

---

## 二、Indirect Draw 的核心概念

### 2.1 直接绘制 vs 间接绘制

**直接绘制（Direct Draw）：**
```cpp
// CPU 决定一切绘制参数
RHICmdList.SetVertexBuffer(...);
RHICmdList.DrawPrimitive(VertexCount, InstanceCount, ...);
// CPU 在调用时就已经知道 VertexCount 和 InstanceCount
```

**间接绘制（Indirect Draw）：**
```cpp
// GPU 从 Buffer 中读取绘制参数
// indirect args buffer 包含：VertexCount, InstanceCount, StartVertex, StartInstance
RHICmdList.DrawPrimitiveIndirect(IndirectArgsBuffer, 0);  // 0 = args 在 Buffer 中的 offset
// CPU 在调用时不知道 GPU 实际会画多少个实例！
```

**时机对比：**

```
直接绘制:
  CPU: "画 1024 个实例" ──────────────→ GPU: 执行   [同步填参数]
  
间接绘制:
  GPU compute: "我算出 VisibleCount=256" → 写入 ArgsBuffer
                                          ↓
  CPU: "画 ArgsBuffer 指定的数量" ──────→ GPU: 读取 ArgsBuffer → 按照 256 画
                                          [参数由 GPU 自己决定]
```

### 2.2 FRHIDrawIndirectParameters 的布局

```
Indirect Draw 的所有参数存储在 Buffer 中，格式固定为 4 个 uint32：

Offset (bytes) | 字段               | 本项目的值
---------------+--------------------+-----------
0              | VertexCountPerInstance | 6 (quad = 2 三角形 = 6 顶点)
4              | InstanceCount      | InstanceCount (GPU 决定)
8              | StartVertexLocation   | 0
12             | StartInstanceLocation | 0

总大小 = 16 bytes
```

这个布局是图形 API 级的约定（D3D12/Vulkan 都遵循）：

| D3D12 | Vulkan | UE 包装 |
|-------|--------|---------|
| `D3D12_DRAW_ARGUMENTS` | `VkDrawIndirectCommand` | `FRHIDrawIndirectParameters` |
| VertexCountPerInstance | vertexCount |  |
| InstanceCount | instanceCount |  |
| StartVertexLocation | firstVertex |  |
| StartInstanceLocation | firstInstance |  |

---

## 三、完整执行链路追踪

### Phase 1：CPU 侧准备工作（游戏线程）

```
GPUDrivenIndirectDrawInterface.cpp:700-867
ExecuteTestIndirectInstanceDraw(UTextureRenderTarget2D*, int32 InstanceCount)
```

```
[L702-706]  检查 GRHIGlobals.SupportsDrawIndirect —— 不是所有 RHI 都支持间接绘制！
[L708-717]  检查 RenderTarget、InstanceCount 有效性
[L728-743]  获取 RenderTargetResource + TextureSize
[L745]      生成 CPU 测试实例数据 GenerateTestInstances(InstanceCount)
[L747]      计算 GridWorldExtent（实例网格的世界空间范围）
```

### Phase 2：ENQUEUE_RENDER_COMMAND → 渲染线程

```
GPUDrivenIndirectDrawInterface.cpp:749-867
```

**Phase 2a：创建 Instance Buffer + SRV（L760-L775）**

```cpp
FBufferRHIRef InstanceBuffer = UE::RHIResourceUtils::CreateBufferFromArray<FGPUDrivenInstanceData>(
    RHICmdList,
    TEXT("GPUDrivenPipeline.IndirectDraw.InstanceData"),
    EBufferUsageFlags::StructuredBuffer | EBufferUsageFlags::ShaderResource,
    ERHIAccess::SRVGraphics,          // ★ 注意这里是 SRVGraphics（vertex shader 读取）
    TConstArrayView<FGPUDrivenInstanceData>(Instances));

FShaderResourceViewRHIRef InstanceDataSRV = RHICmdList.CreateShaderResourceView(
    InstanceBuffer,
    FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(InstanceBuffer));
```

注意这里的状态是 `SRVGraphics`（Pixel Shader 或 Vertex Shader 读取），不是 `SRVCompute`。因为 InstanceData 将在 **Graphics Pipeline 的 Vertex Shader** 中被读取。

**Phase 2b：创建 IndirectArgs Buffer + UAV（L777-L795）**

```cpp
FBufferRHIRef IndirectArgsBuffer = UE::RHIResourceUtils::CreateBufferFromArray<uint32>(
    RHICmdList,
    TEXT("GPUDrivenPipeline.IndirectDraw.Args"),
    EBufferUsageFlags::DrawIndirect | EBufferUsageFlags::UnorderedAccess,
    ERHIAccess::UAVCompute,             // 初始状态：compute shader 写入
    TConstArrayView<uint32>(InitialIndirectArgs, IndirectArgsUintCount));

FUnorderedAccessViewRHIRef IndirectArgsUAV = RHICmdList.CreateUnorderedAccessView(
    IndirectArgsBuffer,
    FRHIViewDesc::CreateBufferUAV()
        .SetType(FRHIViewDesc::EBufferType::Typed)
        .SetFormat(PF_R32_UINT));       // ★ Typed UAV，每个元素是 uint32
```

`EBufferUsageFlags::DrawIndirect` 标记了这个 Buffer 将用于 `DrawPrimitiveIndirect`。没有这个标记，DrawIndirect 调用会失败。

`FRHIViewDesc::CreateBufferUAV().SetType(Typed).SetFormat(PF_R32_UINT)` 表示这是一个"类型化"的 UAV——GPU 知道 Buffer 里存的是 `uint32` 数组，不是结构化数据。

**Phase 2c：Dispatch Compute Shader 写 Indirect Args（L803-L812）**

```cpp
FIndirectDrawArgsShader::FParameters ComputeParameters;
ComputeParameters.OutIndirectArgs = IndirectArgsUAV;
ComputeParameters.InstanceCount = static_cast<uint32>(InstanceCount);

TShaderMapRef<FIndirectDrawArgsShader> IndirectArgsShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

const double ComputeStartTime = FPlatformTime::Seconds();
FComputeShaderUtils::Dispatch(RHICmdList, IndirectArgsShader, ComputeParameters, FIntVector(1, 1, 1));
const double ComputeEndTime = FPlatformTime::Seconds();
```

`GroupCount = (1,1,1)` × `numthreads(1,1,1)` = 总共 1 个线程。因为只需要写 4 个 uint32，单线程就够了。

**Phase 2d：资源状态转换（L814-L815）**

```cpp
RHICmdList.Transition(FRHITransitionInfo(IndirectArgsBuffer,
    ERHIAccess::UAVCompute,             // 当前：compute 可写
    ERHIAccess::IndirectArgs));         // 目标：indirect draw 参数

RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture,
    ERHIAccess::SRVMask,                // 当前：材质只读
    ERHIAccess::RTV));                  // 目标：render target 写入
```

**Phase 2e：Graphics Pipeline 设置与 Indirect Draw（L817-L853）**

```cpp
// 1. 开始 RenderPass
FRHIRenderPassInfo RenderPassInfo(1, ColorRTs, ERenderTargetActions::Clear_Store);
RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("GPUDrivenPipeline_IndirectDraw"));
RHICmdList.SetViewport(...);

// 2. 设置 PSO（Pipeline State Object）
FGraphicsPipelineStateInitializer GraphicsPSOInit;
RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
GraphicsPSOInit.PrimitiveType = PT_TriangleList;
GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
// ★ 使用空顶点声明：没有 CPU 顶点缓冲！
GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

// 3. 绑定参数
FIndirectDrawInstanceVS::FParameters VertexParameters;
VertexParameters.InstanceData = VisibleInstanceDataSRV;  // SRV 绑定到 vertex shader
// ... 其他参数 ...

// 4. ★ 间接绘制！GPU 从 Buffer 读取 InstanceCount
RHICmdList.DrawPrimitiveIndirect(IndirectArgsBuffer, 0);

// 5. 结束 RenderPass
RHICmdList.EndRenderPass();
RHICmdList.Transition(RenderTargetTexture, ERHIAccess::RTV, ERHIAccess::SRVMask);
```

---

## 四、Shader 源码分析

### 4.1 Compute Shader：BuildIndirectArgsCS

```
IndirectDrawInstances.usf:19-31
```

```hlsl
[numthreads(1, 1, 1)]
void BuildIndirectArgsCS(uint3 ThreadID : SV_DispatchThreadID)
{
    if (ThreadID.x != 0 || ThreadID.y != 0 || ThreadID.z != 0) return;

    // [L27-L30] 写入间接绘制参数
    OutIndirectArgs[0] = 6;            // VertexCountPerInstance (quad = 6 verts)
    OutIndirectArgs[1] = InstanceCount; // InstanceCount (= 输入实例总数)
    OutIndirectArgs[2] = 0;            // StartVertexLocation
    OutIndirectArgs[3] = 0;            // StartInstanceLocation
}
```

**输出解释：** GPU 告诉 graphics pipeline："每个实例 6 个顶点，一共画 InstanceCount 个实例，从顶点 0、实例 0 开始。"

此时 `OutIndirectArgs` 是 `RWBuffer<uint>`（Typed UAV），所以 `OutIndirectArgs[0]` 就是第 0 个 uint32。

### 4.2 Vertex Shader：MainVS

```
IndirectDrawInstances.usf:69-88
```

```hlsl
FVertexOutput MainVS(uint VertexID : SV_VertexID, uint InstanceID : SV_InstanceID)
{
    // [L73] 从 SRV 中读取实例数据
    // InstanceData 是 StructuredBuffer<FGPUDrivenInstanceData>
    // GPU 内部用 InstanceID × stride 计算地址偏移
    const FGPUDrivenInstanceData Instance = InstanceData[InstanceID];

    // [L75-L77] 将世界坐标 XY 归一化到 NDC [-1, 1]
    const float2 SafeExtent = max(GridWorldExtent, float2(1.0, 1.0));
    const float2 Normalized = (Instance.Position.xy - GridWorldMin) / SafeExtent;
    const float2 Center = float2(Normalized.x * 2.0 - 1.0, 1.0 - Normalized.y * 2.0);

    // [L79-L81] quad 尺寸在 NDC 空间中的半边长
    const float2 QuadHalfSizeNDC = float2(
        (QuadPixelSize / max(RenderTargetSize.x, 1.0)) * 2.0,
        (QuadPixelSize / max(RenderTargetSize.y, 1.0)) * 2.0);

    // [L83] 根据 VertexID 生成 quad 四个角的顶点偏移
    const float2 Offset = GetQuadVertexOffset(VertexID) * QuadHalfSizeNDC;

    // [L85] 最终顶点位置 = 实例中心 + 偏移
    Output.Position = float4(Center + Offset, 0.0, 1.0);
    Output.Color = GetInstanceColor(Instance.Flags);
    return Output;
}
```

**为什么不需要 Vertex Buffer：**
- `SV_InstanceID` 由 GPU 自动生成，值从 0 递增到 `InstanceCount - 1`
- 通过 `InstanceData[InstanceID]` 可以直接在 SRV 中索引到对应实例
- `SV_VertexID`（0..5）在 GetQuadVertexOffset 中返回 quad 6 个顶点的局部偏移
- 所以不需要 CPU 上传任何顶点缓冲！

**Quad 生成（GetQuadVertexOffset）：**

```
VertexID 0 → (-1,-1)  1 → (-1,+1)  2 → (+1,+1)
              ┌──────┐
              │  \   │
              │   \  │
              └──────┘
VertexID 3 → (-1,-1)  4 → (+1,+1)  5 → (+1,-1)
```

这是两个三角形组成一个 quad 的标准模式，用 `SV_VertexID % 6` 取模复用。

### 4.3 Pixel Shader：MainPS

```
IndirectDrawInstances.usf:90-93
```

```hlsl
float4 MainPS(FVertexOutput Input) : SV_Target0
{
    return Input.Color;
}
```

像素 shader 的任务极其简单：直接输出 vertex shader 传入的颜色。

---

## 五、关键资源状态转换图

**本阶段同时操作了 Compute 和 Graphics 两条流水线：**

```
Compute Pipeline                    Graphics Pipeline
────────────────                    ─────────────────

Instance StructuredBuffer (SRV) ←──────────────── Instance StructuredBuffer (SRV)
                                              （同一个 Buffer，同一状态 SRVGraphics）
      
IndirectArgsBuffer
  ├─ [Compute] UAVCompute: shader 写入 4 uint32
  │
  └─ Transition: UAVCompute → IndirectArgs  ★ 关键转换
       │
       └─ [Graphics] IndirectArgs: DrawPrimitiveIndirect 读取

RenderTargetTexture
  ├─ [初始] SRVMask
  │
  ├─ Transition: SRVMask → RTV  ★ RenderTarget 转换
  │
  ├─ [Graphics] RTV: 像素 shader 写入
  │
  └─ Transition: RTV → SRVMask  ★ 恢复材质采样
```

**为什么需要 Transition：**
- `IndirectArgsBuffer` 刚刚被 compute shader 写入（UAVCompute），现在 graphics pipeline 要以间接参数方式读取。D3D12 要求显式转换状态，否则可能读到过期数据或 crash。
- `RenderTargetTexture` 从材质采样（SRVMask）切换到 render target 写入（RTV），转换是必须的。

---

## 六、完整执行流程图

```
游戏线程                                渲染线程                              GPU
───────                                ────────                             ───────
ExecuteTestIndirectInstanceDraw()
  │
  ├─ 生成测试实例数据
  │
  ├─ ENQUEUE_RENDER_COMMAND ─────────→ lambda 执行
  │                                      │
  │                                      ├─ CreateBufferFromArray(Instances)
  │                                      │   → Instance StructuredBuffer + SRV
  │                                      │
  │                                      ├─ CreateBufferFromArray(ArgsInit)
  │                                      │   → Args Buffer + UAV
  │                                      │
  │  (游戏线程继续执行)                     ├─ Dispatch(BuildIndirectArgsCS) ────→ GPU
  │                                      │   GroupCount=(1,1,1)                  │
  │                                      │   OutIndirectArgs = UAV               │
  │                                      │                                       │
  │                                      │   Transition: UAVCompute → IndirectArgs
  │                                      │   Transition: SRVMask → RTV
  │                                      │
  │                                      ├─ BeginRenderPass(RT)
  │                                      │
  │                                      ├─ SetGraphicsPipelineState
  │                                      │   ├─ VertexShader = MainVS
  │                                      │   ├─ PixelShader = MainPS
  │                                      │   ├─ EmptyVertexDecl → 无 VB
  │                                      │   └─ Primitive = TriangleList
  │                                      │
  │                                      ├─ SetShaderParameters(VS)
  │                                      │   InstanceData = SRV
  │                                      │
  │                                      ├─ DrawPrimitiveIndirect(args) ────────→ GPU
  │                                      │   ArgsBuffer: VertexCount=6           │
  │                                      │   InstanceCount=<GPU决定>              │
  │                                      │                                       │
  │                                      │                              MainVS 执行
  │                                      │                              InstanceID → 读取 SRV
  │                                      │                              VertexID → 生成 quad
  │                                      │                                       │
  │                                      │                              MainPS 执行
  │                                      │                              输出颜色
  │                                      │                                       │
  │                                      ├─ EndRenderPass
  │                                      └─ Transition: RTV → SRVMask
  │
  关卡显示 RT_GPUComputeOutput
```

---

## 七、补充知识点

### 7.1 GEmptyVertexDeclaration

`GEmptyVertexDeclaration` 是 UE 提供的一个特殊顶点声明，表示 "这个 draw call 不需要 CPU 顶点缓冲"。它在 `CommonRenderResources.h` 中定义。

**通常的绘制流程（有 VB）：**
```
CPU 上传顶点数据 → VertexBuffer → IA（Input Assembler）阶段绑定 VB
→ VS 通过 InputLayout 读取顶点属性 → 渲染
```

**本项目的绘制流程（无 VB）：**
```
VS 通过 SV_VertexID 程序化生成顶点位置 → 不需要 InputLayout
VS 通过 SV_InstanceID 从 StructuredBuffer 中读取实例数据
```

使用 `GEmptyVertexDeclaration` 的场景：
1. 程序化生成几何体（quad、fullscreen triangle）
2. GPU-driven 管线（实例数据来自 StructuredBuffer）
3. VS 只需要系统语义（SV_VertexID, SV_InstanceID）不需要自定义 attribute

### 7.2 DrawPrimitiveIndirect 的执行时机

当 CPU 调用 `RHICmdList.DrawPrimitiveIndirect(ArgsBuffer, 0)` 时：

1. CPU 侧的 RHI 代码记录一个 DrawIndirect 命令到命令列表
2. 这个命令不包含具体的绘制参数，只包含 ArgsBuffer 的 GPU 地址
3. GPU 执行到这个命令时，才从显存中读取 ArgsBuffer 的内容
4. **此时 compute shader 对 ArgsBuffer 的写入早已完成**（因为都在同一个 `ImmediateFlush` 批次中）

所以不需要 CPU 侧等待——GPU 按命令列表顺序执行，compute shader 在后，draw 在前。Transition 确保了之前的写入对后续读取可见。

### 7.3 一次 Dispatch + 一次 Draw 的两阶段模式

本项目目前的 Indirect Draw MVP 做了 **两次 GPU 工作**：

```
1. Dispatch (Compute) → 写 IndirectArgs
   ↑                  ↑
  CPU提交             GPU执行时间
   
2. DrawPrimitiveIndirect (Graphics) → 消费 IndirectArgs
```

这两次之间没有任何 CPU 介入。compute shader 执行完后，ArgsBuffer 中包含有效的绘制参数，随后 graphics pipeline 立即读取它。

**这就是 GPU-driven 的核心模式：GPU 自己决定后续 GPU 的工作。**

### 7.4 VS/PS 参数的 C++/HLSL 绑定机制

```cpp
// C++ 侧声明参数
BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
    SHADER_PARAMETER_SRV(StructuredBuffer<FGPUDrivenInstanceData>, InstanceData)
    SHADER_PARAMETER(uint32, InstanceCount)
    SHADER_PARAMETER(FVector2f, RenderTargetSize)
    SHADER_PARAMETER(FVector2f, GridWorldMin)
    SHADER_PARAMETER(FVector2f, GridWorldExtent)
    SHADER_PARAMETER(float, QuadPixelSize)
END_SHADER_PARAMETER_STRUCT()

// C++ 侧填充参数
VertexParameters.InstanceData = VisibleInstanceDataSRV;

// 使用 SetShaderParameters 一次性绑定所有参数到 GPU
SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VertexParameters);
```

SetShaderParameters 内部做了：
1. 遍历 FParameters 的每个成员（通过 UE 反射/宏生成的信息）
2. 根据其在结构体中的 offset 和 type，决定绑定到 GPU 的哪个 slot
3. 调用 `RHICmdList.SetShaderResourceViewParameter` / `SetShaderParameter`

HLSL 侧同名字段的绑定由 shader 编译系统自动匹配（通过 slot register）。

---

## 八、验证步骤

1. 在蓝图 `BeginPlay` 中调用 `Execute Test Indirect Instance Draw (RT_GPUComputeOutput, 1024)`
2. 确保关卡中显示 RT 的平面存在
3. Output Log 应输出：
   ```
   GPUDrivenPipeline: IndirectDraw MVP rendered 1024 instances to RT_GPUComputeOutput (1024x1024, ...)
   ```
4. 画面应显示彩色 quad 网格

**对比旧 compute pass：**
- 旧 pass：红绿渐变（每个像素由 compute shader 填充）
- 新 pass：彩色 quad 网格（每个 quad 代表一个实例，由 indirect draw 绘制）
- 如果看到渐变而不是 quad，说明仍然在显示旧 pass 的输出——交叉检查蓝图节点连接

---

## 九、常见问题排查

| 现象 | 可能原因 | 排查方法 |
|------|---------|---------|
| 画面黑色 | RT 在 draw 后未被材质采样 | 检查 Transition 是否正确恢复到 SRVMask |
| | Indirect args 无效 | 检查 args buffer 创建标志是否含 `DrawIndirect` |
| 实例画不全 | InstanceCount 太大 | 检查 MaxIndirectDrawInstanceCount 上限 |
| | ArgsBuffer 内容被覆盖 | 检查 Transition 时序 |
| RHI warning | 资源状态转换错误 | 检查每个 Transition 的两个状态是否正确 |
| GPU crash | 使用 Compute Shader 写入 `DrawIndirect` 标记的 Buffer | 确认 UAV 创建格式正确 |
