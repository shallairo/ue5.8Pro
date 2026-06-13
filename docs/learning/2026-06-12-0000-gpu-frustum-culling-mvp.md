# GPU Frustum Culling MVP

## 本篇定位

在 `Indirect Draw MVP` 中，GPU 已经可以写 args buffer 并驱动绘制。但那时所有实例都参与绘制——无论它是否在视野内。

本次的目标是证明：**GPU 可以在绘制之前，先决定哪些实例应该被绘制**。这就是 GPU frustum culling 的最小实现。

通过本篇的学习，你应该能回答以下问题：
- 什么是视锥体裁剪（Frustum Culling）？它的数学原理是什么？
- 包围球（Bounding Sphere）和平面方程如何做可见性测试？
- 为什么第一版用 `[numthreads(1,1,1)]` 单线程？性能最优方案是什么？
- "Compact visible list" 是什么？为什么 indirect draw 需要一个紧凑的可见实例数组？
- GPU 侧同时写多个 Buffer（VisibleInstance, IndirectArgs, Summary）时状态管理如何做？

---

## 一、涉及源码总览

| 文件 | 行范围 | 职责 |
|------|--------|------|
| `GPUDrivenIndirectDrawInterface.h` | L36-L56 | **新增蓝图接口**：Frustum cull + readback |
| `GPUDrivenIndirectDrawInterface.cpp` | L444-L697 | **联合执行函数**：FrustumCulledIndirectDrawInternal |
| `GPUDrivenIndirectDrawInterface.cpp` | L869-L878 | **固定测试 plane 入口**：ExecuteTestFrustumCulledIndirectDraw |
| `FrustumCullShader.h` | 全部 | **culling shader 声明**：参数结构体（6 planes + 4 个 Buffer） |
| `FrustumCullShader.cpp` | 全部 | **culling shader 注册** |
| `FrustumCullInstances.usf` | 全部 | **culling 核心逻辑**：sphere-plane test + compact |
| `IndirectDrawShaders.h` | L24-L40 | **VS/PS**：读取 visible instance 数据（复用） |

---

## 二、Frustum Culling 的数学基础

### 2.1 视锥体（View Frustum）由 6 个平面构成

```
              Top
               │
               │  ┌──────────┐
    Left       │  │          │
   ────────────┼──┤  Near    ├──── Right
               │  │  Plane   │
               │  └──────────┘
               │
              Bottom
               │
               └─────→ Far Plane (不可见)
```

| 平面 | 法线方向 |
|------|---------|
| Near (近裁面) | 朝内（向 far 方向） |
| Far (远裁面) | 朝内（向 near 方向） |
| Left (左裁面) | 朝右 |
| Right (右裁面) | 朝左 |
| Top (上裁面) | 朝下 |
| Bottom (下裁面) | 朝上 |

### 2.2 平面方程

每个平面用四维向量 `(A, B, C, W)` 表示，其中 `(A, B, C)` 是平面法线，`W` 是原点到平面的有符号距离。

一个点 `P = (x, y, z)` 在平面**内侧**的条件是：

```text
dot((A, B, C), P) + W >= 0
```

### 2.3 包围球测试

```hlsl
// FrustumCullInstances.usf:29-30
bool IsSphereInsidePlane(float3 Center, float Radius, float4 Plane)
{
    return dot(Plane.xyz, Center) + Plane.w >= -Radius;
}
```

**几何含义：**
- `dot(Plane.xyz, Center) + Plane.w` 是球体中心到平面的有符号距离（负值表示中心在平面外侧）
- 如果这个距离的绝对值小于等于 `Radius`，球体**仍然与平面相交**（可见）
- 如果 `距离 + Radius < 0`（即 `dot(...) + w < -Radius`），球体**完全在平面外侧**（不可见）

```
          平面
     ──────┼──────────
           │
      ╭────┤────╮      ← 球体半径
      │ ●  │    │      ← 中心在平面内侧 → dot + w = 正 → 可见
      ╰────┤────╯
           │
           │
     ╭─────┼────╮
     │     │●   │      ← 中心在平面外侧但 Radius 足够大 → dot + w 为负但 ≥ -Radius → 仍可见
     ╰─────┼────╯
           │
           │
         ╭─┼──╮
         │ │● │       ← 中心在平面外侧且 Radius 不够 → dot + w < -Radius → 不可见
         ╰─┼──╯
```

**裁剪判定：** 实例需要**通过全部 6 个平面的测试**才被视为可见。任何一个平面测试不通过就 invisible。

---

## 三、完整执行链路追踪

### Phase 1：GPU Frustum Culling 联合执行

```
GPUDrivenIndirectDrawInterface.cpp:444-697
ExecuteFrustumCulledIndirectDrawInternal(RT, Instances, FrustumPlanes[6], Context)
```

#### 1.1 前置校验（L450-L498）

```
[L452-456] 检查 GRHIGlobals.SupportsDrawIndirect
[L458-468] 检查 OutputRenderTarget 和 InstanceCount 有效性
[L470-478] 超过 MaxIndirectDrawInstanceCount 时 clamp
[L480-495] 获取 RenderTargetResource + TextureSize
[L497]     初始化 VisibleInstanceInitialValues（全零，预留 InstanceCount 大小）
[L500-502] 计算 InstanceBounds + EstimatedVisibleCount
```

#### 1.2 CPU 可见性估计（L379-L402）

```cpp
static int32 EstimateVisibleInstanceCount(const TArray<FGPUDrivenInstanceData>& Instances,
    const FVector4f FrustumPlanes[6])
{
    int32 VisibleCount = 0;
    for (const auto& Instance : Instances)
    {
        bool bVisible = true;
        for (int32 PlaneIndex = 0; PlaneIndex < 6; ++PlaneIndex)
        {
            if (!IsSphereInsidePlane(Instance.Position, Instance.Radius, FrustumPlanes[PlaneIndex]))
            { bVisible = false; break; }
        }
        if (bVisible) ++VisibleCount;
    }
    return VisibleCount;
}
```

这个估计用于日志对照，实际控制绘制的是 GPU 端的结果。

#### 1.3 渲染线程内执行（L514-L696）

**创建 4 个 GPU Buffer：**

```
[L540-L545]  InstanceBuffer (StructuredBuffer + SRV)        → 输入：所有实例数据
[L547-L552]  VisibleInstanceBuffer (StructuredBuffer + UAV)  → 输出：裁剪后的可见实例
[L554-L560]  IndirectArgsBuffer (DrawIndirect + UAV)         → 输出：GPU 写完的绘制参数
[L562-L568]  SummaryBuffer (UAV + SourceCopy)                → 输出：GPU visible count
```

**Buffer 对比表：**

| Buffer | Size | UsageFlags | 初始状态 | 用途 |
|--------|------|-----------|---------|------|
| InstanceBuffer | InstanceCount × 32 | StructuredBuffer + ShaderResource | SRVCompute | Shader 只读输入 |
| VisibleInstanceBuffer | InstanceCount × 32 | SBuffer + ShaderResource + UAV | UAVCompute | Shader 写入可见实例 |
| IndirectArgsBuffer | 4 × uint32 | DrawIndirect + UAV | UAVCompute | Shader 写入 draw args |
| SummaryBuffer | 2 × uint32 | UAV + SourceCopy | UAVCompute | Shader 写入 visible count |

**创建 SRV/UAV 视图（L576-L604）：**

```cpp
// Instance → SRV (只读)
FShaderResourceViewRHIRef InstanceDataSRV = RHICmdList.CreateShaderResourceView(
    InstanceBuffer, FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(InstanceBuffer));

// VisibleInstance → SRV (vertex shader 读) + UAV (compute shader 写)
FShaderResourceViewRHIRef VisibleInstanceDataSRV = RHICmdList.CreateShaderResourceView(...);
FUnorderedAccessViewRHIRef VisibleInstanceDataUAV = RHICmdList.CreateUnorderedAccessView(...);
// ★ 同一个 Buffer 同时拥有 SRV 和 UAV —— 不同阶段使用不同视图

// IndirectArgs → UAV (compute shader 写)
FUnorderedAccessViewRHIRef IndirectArgsUAV = RHICmdList.CreateUnorderedAccessView(...);

// Summary → UAV (compute shader 写)
FUnorderedAccessViewRHIRef SummaryUAV = RHICmdList.CreateUnorderedAccessView(...);
```

**注意** `VisibleInstanceBuffer` 同时需要 `SRV` 和 `UAV`：
- compute phase：用 UAV 写入裁剪后的实例
- graphics phase：用 SRV 让 vertex shader 读取

**绑定参数并 Dispatch Culling Shader（L606-L624）：**

```cpp
FFrustumCullInstancesShader::FParameters CullingParameters;
CullingParameters.InstanceData = InstanceDataSRV;               // SRV 输入
CullingParameters.OutVisibleInstanceData = VisibleInstanceDataUAV;  // UAV 输出
CullingParameters.OutIndirectArgs = IndirectArgsUAV;            // UAV 输出
CullingParameters.OutSummary = SummaryUAV;                      // UAV 输出
CullingParameters.InstanceCount = InstanceCount;
CullingParameters.FrustumPlane0..5 = FrustumPlanes[0..5];       // 6 个平面参数

FComputeShaderUtils::Dispatch(RHICmdList, CullingShader, CullingParameters, FIntVector(1, 1, 1));
```

`GroupCount = (1,1,1)` + `numthreads(1,1,1)` = 单线程串行遍历所有实例。

**资源状态转换（L626-L629）：**

```cpp
RHICmdList.Transition(FRHITransitionInfo(SummaryBuffer,       UAVCompute → CopySrc));
RHICmdList.Transition(FRHITransitionInfo(VisibleInstanceBuffer, UAVCompute → SRVGraphics));
RHICmdList.Transition(FRHITransitionInfo(IndirectArgsBuffer,   UAVCompute → IndirectArgs));
RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture,  SRVMask → RTV));
```

一次 Dispatch 触发了 4 个状态转换，说明同一个 compute shader 的产出要供应给不同的消费端。

**发起 Readback（L631-L633）：**

```cpp
TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback =
    MakeShared<FRHIGPUBufferReadback, ...>(TEXT("GPUDrivenPipeline.FrustumCullReadback"));
Readback->EnqueueCopy(RHICmdList, SummaryBuffer, FrustumCullSummarySizeBytes);
```

**Graphics Pipeline 绘制（L635-L695）：**

与 Indirect Draw MVP 基本一致，唯一的区别是 vertex shader 读取的是 `VisibleInstanceDataSRV`（裁剪后的可见实例数组），而非原始 InstanceData。

---

## 四、Shader 源码分析

### 4.1 Culling Shader：CullInstancesCS

```
FrustumCullInstances.usf:46-74
```

```hlsl
[numthreads(1, 1, 1)]
void CullInstancesCS(uint3 ThreadID : SV_DispatchThreadID)
{
    // [L49-52] 只让第一个线程工作（单线程模式）
    if (ThreadID.x != 0 || ThreadID.y != 0 || ThreadID.z != 0) return;

    uint VisibleCount = 0;

    // [L56-65] [loop] 串行遍历所有实例
    [loop]
    for (uint InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
    {
        const FGPUDrivenInstanceData Instance = InstanceData[InstanceIndex];
        if (IsInstanceVisible(Instance))
        {
            // ★ compact write：按 VisibleCount 递增写入
            // VisibleCount 既是计数器也是写入索引
            OutVisibleInstanceData[VisibleCount] = Instance;
            VisibleCount++;
        }
    }

    // [L67-70] 写入 indirect args
    OutIndirectArgs[0] = 6;             // VertexCountPerInstance
    OutIndirectArgs[1] = VisibleCount;  // ★ InstanceCount = GPU 计算出的可见数量
    OutIndirectArgs[2] = 0;             // StartVertexLocation
    OutIndirectArgs[3] = 0;             // StartInstanceLocation

    // [L72-73] 写入 summary，用于 CPU readback
    OutSummary[0] = VisibleCount;    // GPU 可见实例计数
    OutSummary[1] = InstanceCount;   // 原始实例总数
}
```

**IsInstanceVisible 函数（L33-44）：**

```hlsl
bool IsInstanceVisible(FGPUDrivenInstanceData Instance)
{
    return IsSphereInsidePlane(Center, Radius, FrustumPlane0)
        && IsSphereInsidePlane(Center, Radius, FrustumPlane1)
        && ...
        && IsSphereInsidePlane(Center, Radius, FrustumPlane5);
    // 全部 6 个平面都通过才返回 true
}
```

**Compact Write 是什么：**

假设输入实例 0~9，可见的是第 2, 5, 7 个：

```
Input InstanceData[0..9]:
[0][1][2][3][4][5][6][7][8][9]
         ↑     ↑     ↑        (可见)

Compact 后 OutVisibleInstanceData:
[2][5][7] ... ... ... ... ... ... ...
 0  1  2 (VisibleCount = 3)
```

这个 `OutVisibleInstanceData` 配合 `IndirectArgs.InstanceCount = 3`，graphics pipeline 只会读取前 3 个元素，后面未使用的元素被跳过。

---

## 五、数据流/状态转换图

整个阶段的资源流转：

```
┌────────────────────────────────────────────────────────────────────────┐
│ Compute Phase (FrustumCullInstancesCS)                                 │
│                                                                        │
│  InstanceData[0..N-1] (SRV)                                            │
│       │                                                                │
│       │  for i in 0..N-1:                                              │
│       │    if IsInstanceVisible(i):                                    │
│       │      OutVisibleInstanceData[VisibleCount++] = InstanceData[i]  │
│       │                                                                │
│       ├──→ OutVisibleInstanceData[][] (UAVCompute)                     │
│       │     状态 → Transition → SRVGraphics                            │
│       │                                                                │
│       ├──→ OutIndirectArgs[] = {6, VisibleCount, 0, 0} (UAVCompute)   │
│       │     状态 → Transition → IndirectArgs                          │
│       │                                                                │
│       └──→ OutSummary[] = {VisibleCount, InstanceCount} (UAVCompute)  │
│             状态 → Transition → CopySrc                                │
│              → EnqueueCopy → Readback → CPU 读取                      │
└────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│ Graphics Phase (MainVS + MainPS)                                      │
│                                                                        │
│  OutVisibleInstanceData (SRVGraphics) ←─ compact 数组                  │
│  OutIndirectArgs (IndirectArgs) ←─ InstanceCount = VisibleCount       │
│                                                                        │
│  MainVS: for each InstanceID in 0..VisibleCount-1:                    │
│      Instance = OutVisibleInstanceData[InstanceID]                     │
│      Output = WorldToNDC(Instance.Position) + QuadOffset(VertexID)    │
│                                                                        │
│  MainPS: Output = InstanceColor                                        │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 六、补充知识点

### 6.1 为什么第一版用单线程

本版 culling shader 使用 `[numthreads(1,1,1)]` + 单线程循环，**是有意为之**：

| 方案 | 复杂度 | 优点 | 缺点 |
|------|--------|------|------|
| 单线程串行 | 最低 | 无同步、无竞争、逻辑清晰 | 性能差（O(n) 串行） |
| 多线程并行 + AppendByteAddressBuffer | 高 | 性能好（O(n/m) 并行） | 需要 D3D12 的 Append/Consume buffer |
| 多线程并行 + Counter UAV | 高 | 性能好 | 需要全局 atomic 计数器 |

**当前 MVP 选择单线程的原因：**
1. 正确性优先——避免引入并行 culling 的同步 Bug
2. 数据量小（1024~4096 实例），单线程在 compute shader 上仍然很快（几十微秒）
3. 后续在 parallel culling 阶段再升级为并行方案

### 6.2 Compact Visible List vs Non-compact

**Non-compact（不紧凑）：**
```
Visible[InstanceCount] → 用 uint8/uint32 标记可见性
  [1][0][0][1][1][0][1]... (N 个标记)
```
问题：vertex shader 仍然需要遍历所有实例，跳过不可见的浪费。

**Compact（紧凑）：**
```
Visible[InstanceCount] → 只存可见实例
  [已裁剪的实例 A][已裁剪的实例 B]... (VisibleCount 个)
```
优势：DrawIndirect 的 InstanceCount = VisibleCount，vertex shader 只处理可见实例。

Compact 是 GPU-driven rendering 的标准模式。

### 6.3 为什么需要同时写 3 个 Buffer

同一个 compute shader 写了三个不同的 Buffer：

| Buffer | 读取方 | 用途 |
|--------|--------|------|
| `OutVisibleInstanceData` | Vertex Shader (MainVS) | 绘制实例的数据 |
| `OutIndirectArgs` | Graphics Pipeline (DrawIndirect) | 控制绘制数量 |
| `OutSummary` | CPU (Readback) | 验证 GPU 结果 |

这样一次 dispatch 就完成了"裁剪、参数生成、结果记录"三项工作。这也是 GPU-driven pipeline 的核心优势——减少 CPU/GPU 之间往返。

### 6.4 固定测试平面与真实摄像机的区别

```
GPUDrivenIndirectDrawInterface.cpp:217-229
BuildTestFrustumPlanes(GridWorldExtent, OutPlanes)
```

```cpp
OutPlanes[0] = FVector4f(1, 0, 0, -MinX);    // Left:   x >= MinX
OutPlanes[1] = FVector4f(-1, 0, 0, MaxX);     // Right:  x <= MaxX
OutPlanes[2] = FVector4f(0, 1, 0, -MinY);     // Bottom: y >= MinY
OutPlanes[3] = FVector4f(0, -1, 0, MaxY);     // Top:    y <= MaxY
OutPlanes[4] = FVector4f(0, 0, 1, 100000);    // Near:   z >= -100000
OutPlanes[5] = FVector4f(0, 0, -1, 100000);   // Far:    z <= 100000
```

这里 `MinX = 0.25 * GridWidth`, `MaxX = 0.75 * GridWidth`（XY 同理），所以固定平面会保留网格**中间四分之一**区域的实例，裁掉外圈。

---

## 七、关键源码索引

| 步骤 | 文件 | 行号 | 作用 |
|------|------|------|------|
| 固定平面生成 | `GPUDrivenIndirectDrawInterface.cpp` | L217-L229 | BuildTestFrustumPlanes |
| CPU 可见性估计 | 同上 | L379-L402 | EstimateVisibleInstanceCount |
| 联合执行函数 | 同上 | L444-L697 | ExecuteFrustumCulledIndirectDrawInternal |
| Instance Buffer 创建 | 同上 | L540-L545 | 输入实例 |
| VisibleInstance Buffer 创建 | 同上 | L547-L552 | 输出紧凑可见实例 |
| IndirectArgs Buffer 创建 | 同上 | L554-L560 | 输出绘制参数 |
| Summary Buffer 创建 | 同上 | L562-L568 | 输出统计结果 |
| Culling 参数绑定 + Dispatch | 同上 | L606-L624 | 调度 culling shader |
| 4 个 Transition | 同上 | L626-L629 | 资源状态转换 |
| Readback 发起 | 同上 | L631-L633 | EnqueueCopy |
| Graphics PSO 设置 | 同上 | L635-L653 | 管线状态对象 |
| DrawIndirect | 同上 | L669 | DrawPrimitiveIndirect |
| Culling Shader 入口 | `FrustumCullInstances.usf` | L46-L74 | CullInstancesCS |
| 球-平面测试 | 同上 | L29-L31 | IsSphereInsidePlane |
| 六平面测试 | 同上 | L33-L44 | IsInstanceVisible |
| 公共入口 | `GPUDrivenIndirectDrawInterface.cpp` | L869-L878 | ExecuteTestFrustumCulledIndirectDraw |

---

## 八、验证方法

1. 在蓝图调用 `Execute Test Frustum Culled Indirect Draw (RT_GPUComputeOutput, 256)`
2. 预期：256 实例中只有约 64 个出现在画面中央（固定平面保留中间 1/4）
3. 对比调用 `Execute Test Indirect Instance Draw (RT_GPUComputeOutput, 256)`
4. 前者显示完整网格，后者只显示中间区域的实例

**日志验证：**

```
GPUDrivenPipeline: Frustum culled indirect draw submitted 1024 source instances to RT_GPUComputeOutput
  (1024x1024, estimated-visible=256, cpu-cull=0.018 ms, cpu-draw=0.000 ms).
```

`estimated-visible=256` 表示 CPU 估计约 1/4 的实例可见（1024/4 = 256），数量一致。

---

## 九、常见问题排查

| 现象 | 可能原因 | 排查方法 |
|------|---------|---------|
| 画面全部可见（无裁剪） | Transition 错误导致 visible buffer 未正确写入 | 检查 IndrectArgs → UAVCompute → IndrectArgs |
| 画面全部为空 | 平面法线方向反了 | 检查 plane test 符号 |
| | VisibleInstanceDataSRV 绑定错误 | 检查 vertex shader 读取的是哪个 SRV |
| Readback 永远不 ready | SummaryBuffer 缺少 `SourceCopy` 标记 | 检查 Buffer 创建标志 |
| 画面闪烁 | 多轮 dispatch 覆盖同一 RT | 检查蓝图是否只调用一次 |

| CPU dispatch 异常 | 首次 shader 编译 | 重置编辑器后再测 |
