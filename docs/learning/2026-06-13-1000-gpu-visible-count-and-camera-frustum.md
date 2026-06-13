# GPU Visible Count 与 Camera Frustum

## 本篇定位

之前的 `GPU Frustum Culling MVP` 证明了 GPU 可以裁剪实例并驱动间接绘制。但有两个明显差距：
1. 裁剪结果只有画面观察，没有严格的 GPU 端定量验证
2. 使用的 frustum 是固定测试平面，不是真正的摄像机视锥

本阶段解决的问题：
- **GPU visible count readback**：从 GPU Summary Buffer 读取真实的可见实例数量，供蓝图查询
- **真实摄像机视锥**：从 `CameraActor` → `CameraComponent` → `PlayerCameraManager` 构造 6 个世界空间的 frustum plane
- **空间语义对齐**：让测试实例分布在 RT 显示平面（`PlaneActor`）的 world bounds 内，使画面和场景对齐

通过本篇的学习，你应该能回答以下问题：
- 如何从摄像机的位置/朝向/FOV 构造 6 个 frustum plane？
- 为什么 `dot(Plane.xyz, Center) + Plane.w` 可能为负？如何校正平面法线方向？
- 为什么 readback 的结果（`GpuVisibleCount`）和 CPU 估算（`EstimatedVisibleCount`）需要完全一致？
- 为什么测试实例不能固定在原点附近，而要锚定在相机前方？
- 为什么 AspectRatio 要根据游戏视口取，而不是根据 RenderTarget 取？

---

## 一、涉及源码总览

| 文件 | 行范围 | 职责 |
|------|--------|------|
| `GPUDrivenIndirectDrawInterface.h` | L48-L55 | **新增蓝图入口声明** |
| `GPUDrivenIndirectDrawInterface.cpp` | L880-L999 | **摄像机 frustum + plane 接口实现** |
| `GPUDrivenIndirectDrawInterface.cpp` | L249-L372 | **BuildCameraFrustumPlanes**：核心摄像机→视锥平面 |
| `GPUDrivenIndirectDrawInterface.cpp` | L231-L247 | **MakePlaneFromPointAndNormal**：平面构造+法线校正 |
| `GPUDrivenIndirectDrawInterface.cpp` | L68-L148 | **GenerateCameraAnchoredTestInstances / GeneratePlaneBoundedTestInstances** |
| `GPUDrivenIndirectDrawInterface.cpp` | L190-L214 | **GetInstanceBounds2D**：用实例 XY 范围归一化 VS 渲染 |
| `FrustumCullInstances.usf` | L72-L73 | **Summary 写入**：OutSummary[0] = VisibleCount |

---

## 二、完整执行链路追踪

### 2.1 Get Last Frustum Cull Result —— Readback 流程

这个流程与 InstanceDataValidation 中的 readback 模式几乎相同，但数据量更小（2 uint32 = 8 bytes）。

**GPU 侧写入 Summary（`FrustumCullInstances.usf:72-73`）：**

```hlsl
OutSummary[0] = VisibleCount;   // GPU 可见实例计数
OutSummary[1] = InstanceCount;  // 原始实例总数
```

**C++ 侧发起 Readback（`GPUDrivenIndirectDrawInterface.cpp:631-633`）：**

```cpp
TSharedPtr<FRHIGPUBufferReadback, ...> Readback = MakeShared<...>(TEXT("GPUDrivenPipeline.FrustumCullReadback"));
Readback->EnqueueCopy(RHICmdList, SummaryBuffer, FrustumCullSummarySizeBytes);  // 8 bytes
```

**蓝图查询结果（`GPUDrivenIndirectDrawInterface.cpp:992-999`）：**

```cpp
bool UGPUDrivenIndirectDrawInterface::GetLastFrustumCullResult(FGPUDrivenFrustumCullResult& OutResult)
{
    TryResolveFrustumCullReadback_GameThread();  // L994
    FScopeLock Lock(&GFrustumCullResultMutex);   // L996
    OutResult = GFrustumCullResult;              // L997
    return bFrustumCullReadbackReady;            // L998
}
```

**TryResolveFrustumCullReadback_GameThread（`GPUDrivenIndirectDrawInterface.cpp:404-442`）：**

```
[L408-413] 检查是否已 ready 或 readback 无效
[L418-439] ENQUEUE_RENDER_COMMAND → 渲染线程执行:
              Readback->Lock(8 bytes)
              FMemory::Memcpy(Summary, ReadbackData, 8)
              Readback->Unlock()
              GFrustumCullResult.GpuVisibleCount = Summary[0]
              GFrustumCullResult.InstanceCount = Summary[1]
              bFrustumCullReadbackReady = true
[L441]     FlushRenderingCommands()  ← 同步等待
```

---

### 2.2 Camera Frustum 平面构造

核心函数 `BuildCameraFrustumPlanes` 位于 `GPUDrivenIndirectDrawInterface.cpp:249-372`。

**Step 1：获取摄像机参数（L268-L327）**

函数通过三级 fallback 获取摄像机姿态：

```
优先等级 1：CameraComponent → 最优先（如果 Actor 上挂载了 CameraComponent）
  使用 CameraComponent 的：
    - GetComponentLocation()    → Origin
    - GetForwardVector()        → Forward
    - GetRightVector()          → Right
    - GetUpVector()             → Up
    - FieldOfView               → EffectiveFovDegrees
    - AspectRatio               → EffectiveAspectRatio (source="CameraComponent")

优先等级 2：PlayerCameraManager → 第二优先（如果 Actor 是Pawn且有PlayerController）
  使用 PlayerCameraManager 的：
    - GetCameraLocation()       → Origin
    - GetCameraRotation()       → Forward/Right/Up (从旋转矩阵提取)
    - GetFOVAngle()             → EffectiveFovDegrees
    - GetViewportSize() 计算     → EffectiveAspectRatio (source="Viewport")

回退等级 3：Actor 自身 Transform → 最后手段
  使用 Actor 的：
    - GetActorLocation()        → Origin
    - GetActorForwardVector()   → Forward/Right/Up
    - 输入参数                  → FieldOfViewDegrees / FallbackAspectRatio
```

**为什么需要三级 fallback：**
- 蓝图传入的可能是 `BP_CameraActor`（有 CameraComponent）、`BP_PlayerPawn`（通过 PlayerCameraManager 取视角）、或者一个普通的 Actor
- 如果直接取 `ActorLocation` 和 `ActorForwardVector`，对于挂在 SpringArm 上的摄像机，位置和朝向都不同

**Step 2：计算 Near 平面四个角（L337-L347）**

```cpp
// [L338] 半水平 FOV = FOV/2 弧度
const float HalfHorizontalFovRadians = FMath::DegreesToRadians(EffectiveFovDegrees * 0.5f);

// [L339-L340] Near 平面的半宽和半高
const float NearHalfWidth = FMath::Tan(HalfHorizontalFovRadians) * NearPlane;
const float NearHalfHeight = NearHalfWidth / EffectiveAspectRatio;

// [L342-L348] Near 平面四个角的世界坐标
const FVector NearCenter = Origin + Forward * NearPlane;
const FVector NearTopLeft     = NearCenter + Up * NearHalfHeight - Right * NearHalfWidth;
const FVector NearTopRight    = NearCenter + Up * NearHalfHeight + Right * NearHalfWidth;
const FVector NearBottomLeft  = NearCenter - Up * NearHalfHeight - Right * NearHalfWidth;
const FVector NearBottomRight = NearCenter - Up * NearHalfHeight + Right * NearHalfWidth;
```

**几何关系：**

```
                     Up
                     ↑
                     │
            NearTopLeft ──── NearTopRight
                 │              │
       Right ←───┼──── Origin ──┼───→ Forward
                 │              │
          NearBottomLeft ── NearBottomRight
                     │
                     ↓
                  (Near plane = Forward × NearPlane)
```

**Step 3：构造 6 个平面（L350-L355）**

```cpp
const FVector InsidePoint = Origin + Forward * ((NearPlane + FarPlane) * 0.5f);  // 视锥内部任意点

OutPlanes[0] = MakePlaneFromPointAndNormal(NearCenter,           Forward,                InsidePoint);  // Near
OutPlanes[1] = MakePlaneFromPointAndNormal(FarCenter,           -Forward,               InsidePoint);  // Far
OutPlanes[2] = MakePlaneFromPointAndNormal(Origin,  Cross(TopLeft - Origin, BottomLeft - Origin),  InsidePoint);  // Left
OutPlanes[3] = MakePlaneFromPointAndNormal(Origin,  Cross(BottomRight - Origin, TopRight - Origin), InsidePoint);  // Right
OutPlanes[4] = MakePlaneFromPointAndNormal(Origin,  Cross(TopRight - Origin, TopLeft - Origin),   InsidePoint);  // Top
OutPlanes[5] = MakePlaneFromPointAndNormal(Origin,  Cross(BottomLeft - Origin, BottomRight - Origin), InsidePoint);  // Bottom
```

每个平面由三个元素定义：
1. **平面上一点**（PointNearArea 或 Origin）
2. **平面法线方向**（从两个边向量的叉积得出）
3. **内部测试点**（用于校正法线方向）

**构造左右平面的几何原理：**

```
Left Plane:
  叉积 (NearTopLeft - Origin) × (NearBottomLeft - Origin)
  = 从原点指向左上角 × 从原点指向左下角
  → 结果垂直于"从原点发出的左边扇形" → 即 left plane 法线
```

**Step 4：法线方向校正（MakePlaneFromPointAndNormal，L231-L247）**

```cpp
static FVector4f MakePlaneFromPointAndNormal(const FVector& Point, FVector Normal,
    const FVector& InsidePoint)
{
    Normal = Normal.GetSafeNormal();
    float W = -FVector::DotProduct(Normal, Point);

    // 检查 InsidePoint 是否在平面正侧
    // 如果不在（dot < 0），翻转法线
    if (FVector::DotProduct(Normal, InsidePoint) + W < 0.0f)
    {
        Normal *= -1.0f;
        W *= -1.0f;
    }

    return FVector4f(Normal.X, Normal.Y, Normal.Z, W);
}
```

**为什么要校正：** 叉积的方向取决于操作数顺序。如果叉积顺序写反了，法线可能指向外侧而不是内侧。通过检查已知在视锥内部的点（`InsidePoint`）是否满足 `dot + w >= 0`，可以在运行时自动纠正。这个技巧大大降低了"视野内全被裁掉"这类 bug 的概率。

---

### 2.3 实例空间对齐

#### 旧方案（固定测试平面）

```
FrustumCullInstances.usf 之前：
  实例生成在世界原点附近的 XY 网格上
  → 如果相机在原点上方向下看 → 完美对齐
  → 如果相机从侧面看向 RT 平面 → 实例位置和相机视角无关

Bug 表现：
  玩家视口能看到 RT 平面（一块地板上显示彩色格子）
  但 culling 认为那批实例不在视锥内
  → RT 画面变黑或只剩很少的实例
  → 开发者以为"相机裁剪路过了"，其实是"实例根本不在相机视野内"
```

#### 新方案 1：Camera Anchored (ExecuteCameraFrustumCulledIndirectDraw)

`GPUDrivenIndirectDrawInterface.cpp:95-148` — `GenerateCameraAnchoredTestInstances`

流程：
```
CameraView.Forward 投影到 XY 平面 → GroundForward
CameraView.Right 投影到 XY 平面 → GroundRight
计算相机射线与 Z=0 平面的交点 → DistanceToGround
网格中心 = Origin + Forward * DistanceToGround
网格原点 = 网格中心 - GroundRight * ExtentX/2 - GroundForward * ExtentY/2
实例位置 = 网格原点 + X * GroundRight * Spacing + Y * GroundForward * Spacing
```

这样实例网格会出现在相机**正前方的地面上**，无论相机在哪里、朝向哪里。

#### 新方案 2：Plane Bounded (ExecutePlaneCameraFrustumCulledIndirectDraw)

`GPUDrivenIndirectDrawInterface.cpp:151-188` — `GeneratePlaneBoundedTestInstances`

流程：
```
PlaneActor->GetActorBounds(false, BoundsOrigin, BoundsExtent)
  → 获得 RT 显示平面的世界范围

实例填满这个 Bounds：
  MinX = BoundsOrigin.X - BoundsExtent.X
  MaxX = BoundsOrigin.X + BoundsExtent.X
  同理 MinY / MaxY
  StepX = (MaxX - MinX) / (GridWidth - 1)
  同理 StepY
  每个实例 = (MinX + X * StepX, MinY + Y * StepY, BoundsOrigin.Z)
  Radius = max(StepX, StepY) / 2
```

**这样三个对象的空间语义一致：**
1. `PlayerCameraManager` → 摄像机视锥（决定什么可见）
2. `PlaneActor` → RT 显示平面的世界范围（决定实例分布在哪里）
3. `FGPUDrivenInstanceData.Position` → 铺满 PlaneActor 的范围

---

### 2.4 世界坐标到 NDC 的映射更新

**之前的 Vertex Shader（固定原点）：**

```hlsl
const float2 Center = float2(Normalized.x * 2.0 - 1.0, 1.0 - Normalized.y * 2.0);
// Normalized = (Instance.Position.xy - 0) / GridWorldExtent
// 假设实例从 0 开始线性排列
```

**更新后的 Vertex Shader（支持任意偏移 `GridWorldMin`）：**

```hlsl
// IndirectDrawInstances.usf:75-77
const float2 SafeExtent = max(GridWorldExtent, float2(1.0, 1.0));
const float2 Normalized = (Instance.Position.xy - GridWorldMin) / SafeExtent;
const float2 Center = float2(Normalized.x * 2.0 - 1.0, 1.0 - Normalized.y * 2.0);
```

`GridWorldMin` 和 `GridWorldExtent` 通过 C++ 的 `GetInstanceBounds2D`（`GPUDrivenIndirectDrawInterface.cpp:190-214`）计算，并传递到 vertex shader 参数中。这样不论实例分布在世界空间的哪个位置，都能正确投射到 RT 的 NDC 空间。

---

## 三、完整数据流图（Camera Frustum + Readback）

```
┌─────────────────────────────────────────────────────────────────────┐
│ 游戏线程 (Game Thread)                                               │
│                                                                     │
│  ExecutePlaneCameraFrustumCulledIndirectDraw(RT, Camera, Plane,...) │
│    │                                                                │
│    ├─ BuildCameraFrustumPlanes()                                    │
│    │   ├─ CameraComponent? → 优先取摄像机组件参数                   │
│    │   ├─ PlayerCameraManager? → 取当前游戏视角                     │
│    │   └─ Actor Transform → 回退                                    │
│    │   ├─ 计算 Near 平面四角                                        │
│    │   ├─ 叉积构造 6 个 frustum plane                               │
│    │   └─ InsidePoint 校正法线方向                                  │
│    │                                                                │
│    ├─ PlaneActor->GetActorBounds()                                   │
│    │   → 获得 RT 显示平面的世界范围                                  │
│    │                                                                │
│    ├─ GeneratePlaneBoundedTestInstances()                           │
│    │   → 实例铺满 PlaneActor 的 XY 范围                              │
│    │                                                                │
│    └─ ENQUEUE_RENDER_COMMAND ─────────────────────────────┐         │
└─────────────────────────────────────────────────────────────────────┘
                                                             │
                                                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│ 渲染线程 (Render Thread)                                             │
│                                                                     │
│  [Compute Phase] FrustumCullInstancesCS dispatch                    │
│    ├─ InstanceData[0..N-1] (SRV)                                    │
│    ├─ 6 个 FrustumPlanes (常量)                                    │
│    ├─ 串行遍历所有实例                                               │
│    ├─ 通过测试的 → 写入 VisibleInstanceData (UAV)                    │
│    ├─ VisibleCount → OutIndirectArgs[1] (UAV)                       │
│    └─ Summary = {VisibleCount, InstanceCount} (UAV)                 │
│                                                                     │
│  [Transition] UAVCompute → SRVGraphics / IndirectArgs / CopySrc     │
│  [Readback]    EnqueueCopy(Summary → Staging)                       │
│                                                                     │
│  [Graphics Phase] DrawPrimitiveIndirect                             │
│    ├─ VisibleInstanceData (SRV) → Vertex Shader                     │
│    ├─ IndirectArgs (IndirectArgs) → GPU 读取绘制参数                │
│    └─ RT_GPUComputeOutput ← 显示裁剪后的实例                         │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
                                                             │
                                                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│ GPU → CPU (异步 Readback)                                           │
│                                                                     │
│  SummaryBuffer (UAVCompute) → CopySrc → EnqueueCopy                 │
│    → GPU 完成 → StagingBuffer 就绪                                   │
│                                                                     │
│  蓝图 GetLastFrustumCullResult()                                    │
│    → TryResolveFrustumCullReadback_GameThread()                     │
│      → Check IsReady()                                              │
│      → Lock(8 bytes) → Memcpy → Unlock                              │
│      → Store to GValidationResult                                   │
│      → FlushRenderingCommands()                                     │
│      → 返回 GpuVisibleCount                                         │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 四、补充知识点

### 4.1 视锥平面推导的详细过程

从 FOV 和 AspectRatio 推导 frustum 的核心是理解 Near 平面的几何结构。

已知：
```
HalfFovRad = tan(FOV / 2 × π / 180)     // 半角正切 = 对边/邻边
NearHalfWidth = tan(HalfFovRad) × NearPlane    // Near 平面半宽
NearHalfHeight = NearHalfWidth / AspectRatio   // Near 平面半高
```

```
         NearPlane
   ◄───────────►
   ───────────────
   \  NearHalf  /
    \   Width  /
     \        /
      \      /
       \    /  HalfFovRad
        \  /
         \/
       Camera Origin
```

AspectRatio 影响的是 Near 平面的**高度**——宽高比越大，水平 FOV 不变的情况下垂直 FOV 越窄（视野上下更紧）。

### 4.2 PlayerCameraManager vs CameraComponent

在 UE 编辑器中有多种"摄像机"：

| 对象 | 含义 | 何时使用 |
|------|------|---------|
| `UCameraComponent` | Actor 上挂载的摄像机组件 | 场景中有明确摄像机 Actor |
| `APlayerCameraManager` | 引擎每帧管理的游戏摄像机 | 玩家控制的 Pawn 视角 |
| `ULocalPlayer::PlayerController` | 玩家控制器 | 获取当前活跃玩家 |

**本项目摄像机读取策略（`GPUDrivenIndirectDrawInterface.cpp:277-327`）：**

```
if (Actor 有 CameraComponent)
  → 使用 CameraComponent（最精确）
else if (Actor 是 APawn 且有 PlayerController)
  → 使用 PlayerCameraManager（玩家视角）
else
  → 使用 Actor 自身 Transform（回退）
```

### 4.3 AspectRatio 的不同来源

| 来源 | 值 | 含义 |
|------|-----|------|
| `RenderTarget SizeX/SizeY` | `1024/1024 = 1.0` | 正方形成像 → 错误！ |
| `CameraComponent.AspectRatio` | 通常 `16/9 = 1.778` | 摄像机组件设置，常为 0 |
| `PlayerController.GetViewportSize()` | 如 `1920/1080 = 1.778` | 真实游戏视口 → 最准确 |

**本项目中的 AspectRatio 获取策略（`GPUDrivenIndirectDrawInterface.cpp:278-322`）：**

```
默认 fallback = RenderTarget 宽高比

如果有 CameraComponent:
  > AspectRatio > 0 → 使用 CameraComponent 的 AspectRatio（source="CameraComponent"）

如果是 PlayerController + PlayerCameraManager:
  > GetViewportSize > 0 → 使用真实视口宽高比（source="Viewport"）
```

**为什么拿真实视口宽高比：** 玩家肉眼通过游戏视口观察场景，如果构建 frustum 时用了正方形的 1:1 aspect，但实际游戏窗口是 16:9，那么水平视野在 GPU culling 中会被错误地压缩，边缘的实例会被提前裁掉。

---

## 五、本阶段关键 Debug 经验

### 5.1 PlayerPawn ≠ 游戏相机

传入 `Get Player Pawn` 作为 CameraActor，但：
- Pawn 的 `ActorTransform` 可能和实际摄像机不同（尤其是用了 SpringArm）
- PlayerCameraManager 持有的是**真实渲染视角**

### 5.2 场景中的 RT 平面 ≠ 实例世界位置

- 执行 `Execute Camera Frustum Culled Indirect Draw` 时，实例仍然生成在世界原点附近的 XY 网格上
- 如果相机看向的是别处，culling 结果可能全不可见
- 解决方案：`Execute Plane Camera Frustum Culled Indirect Draw` 需要显式传入 PlaneActor

### 5.3 调试时机

测试时应该遵循顺序：
1. 先用 `Execute Test Frustum Culled Indirect Draw`（固定平面）确认 culling 逻辑正确
2. 再用 `Execute Camera Frustum Culled Indirect Draw` 确认摄像机平面提取正确
3. 最后用 `Execute Plane Camera Frustum Culled Indirect Draw` 确认空间对齐正确

不要跳过中间步骤——在一次改动中同时变更多个变量，出了问题很难定位。

---

## 六、关键源码索引

| 步骤 | 文件 | 行号 | 作用 |
|------|------|------|------|
| 摄像机 frustum 构造 | `GPUDrivenIndirectDrawInterface.cpp` | L249-L372 | BuildCameraFrustumPlanes |
| 平面法线校正 | 同上 | L231-L247 | MakePlaneFromPointAndNormal |
| 相机锚定实例 | 同上 | L95-L148 | GenerateCameraAnchoredTestInstances |
| 平面锚定实例 | 同上 | L151-L188 | GeneratePlaneBoundedTestInstances |
| 实例 Bounds 计算 | 同上 | L190-L214 | GetInstanceBounds2D |
| Readback 解析 | 同上 | L404-L442 | TryResolveFrustumCullReadback_GameThread |
| 获取结果接口 | 同上 | L992-L999 | GetLastFrustumCullResult |
| 摄像机版入口 | 同上 | L880-L918 | ExecuteCameraFrustumCulledIndirectDraw |
| 平面版入口 | 同上 | L920-L990 | ExecutePlaneCameraFrustumCulledIndirectDraw |
| HDLS 写入 Summary | `FrustumCullInstances.usf` | L72-L73 | OutSummary[0] = VisibleCount |

---

## 七、验证步骤

在蓝图中使用 `Execute Plane Camera Frustum Culled Indirect Draw`：
1. 传入正确的 `CameraActor`（或者 `Get Player Pawn`）
2. 传入场景中显示 RT 的平面 Actor
3. FOV=90, Near=10, Far=10000, InstanceCount=256 (默认)
4. 运行 PIE，移动相机观察画面变化

**应观察到的结果：**
- 相机正对平面 → 应该看到所有实例
- 相机旋转离开平面 → 实例逐渐被裁掉（画面边缘实例先消失）
- 相机完全背向平面 → 画面接近全黑，只有背景清除色
- `Get Last Frustum Cull Result` 的 `GpuVisibleCount` 应随相机移动变化

---

## 八、当前已知限制

1. **剔除精度不完全匹配肉眼**：当前 camera-driven frustum culling 已经大体正确，但和玩家肉眼看到的可见范围还没有完全一一对应。更像是 "correctness MVP"，尚未达到像素级一致。

2. **测试实例仍然是程序化生成的**：没有接入真实场景的 StaticMesh/Nanite，culling 结果仅通过 RT 显示平面上的彩色 quad 验证。

3. **AspectRatio 仍有回退场景**：某些情况下（无 PlayerController，无 CameraComponent），仍然使用 RenderTarget 的宽高比，这可能导致水平/垂直视野不准确。
