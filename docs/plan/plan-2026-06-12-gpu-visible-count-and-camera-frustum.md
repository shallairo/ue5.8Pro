# GPU Visible Count 与 Camera Frustum 开发计划

## 当前状态

当前 `GPU Frustum Culling MVP` 已经完成第一轮验证。

已证明：

- CPU 可以生成规则测试实例。
- GPU compute shader 可以做最小视锥裁剪。
- GPU 可以写出 compact visible instance buffer。
- GPU 可以写出 `DrawPrimitiveIndirect` 所需的 instance count。
- graphics pass 可以只绘制 visible instance buffer。

当前仍未证明：

- GPU 端真实 visible count 的 readback 结果。
- 裁剪结果是否能跟随 UE 真实相机变化。
- 当前路径在交互视角下是否稳定。

因此下一阶段先补验证闭环，再接真实相机输入。

## 目标

本阶段目标是把当前“固定测试裁剪”推进到“可严格验证、可由相机驱动的 GPU culling”。

核心目标：

```text
GPU 写 visible count -> CPU readback 验证 -> CPU 提供真实相机 frustum plane -> GPU culling 结果随相机变化
```

本阶段完成后，项目应具备：

- 严格的 GPU 可见数量验证结果。
- 蓝图或 C++ 入口可以使用真实相机参数。
- Output Log 能同时输出 CPU 估算数量和 GPU readback 数量。
- RenderTarget 上的可见实例会随相机 frustum 设置变化。

## 范围约束

本阶段不做：

- 并行 compact list 性能优化
- Hi-Z occlusion culling
- LOD selection
- StaticMesh / Nanite 渲染路径接入
- 自动扫描关卡 Actor 生成真实实例
- 真正 GPU timestamp 性能统计

原因：

当前最重要的问题不是性能，而是验证链路是否严谨，以及裁剪输入是否从固定测试参数升级到真实视图参数。

## 里程碑 1：GPU Visible Count Readback

### 目标

让 GPU 在 culling shader 中额外写出 visible count summary，并通过 readback 回 CPU。

当前 `estimated-visible` 是 CPU 估算值，不是 GPU 回读值。本里程碑要补上真正的 GPU 端结果。

### 建议改动

在 `FrustumCullInstances.usf` 中新增输出：

```hlsl
RWBuffer<uint> OutVisibleCount;
```

或复用一个 summary buffer：

```text
Summary[0] = VisibleCount
Summary[1] = SourceInstanceCount
```

C++ 侧新增：

- `VisibleCountBuffer`
- `FRHIGPUBufferReadback`
- 蓝图可查询结构体，例如 `FGPUDrivenFrustumCullResult`

建议蓝图节点：

```cpp
Get Last Frustum Cull Result(FGPUDrivenFrustumCullResult& OutResult)
```

结构体字段建议：

```cpp
InstanceCount
EstimatedVisibleCount
GpuVisibleCount
BufferSizeBytes
CpuCullDispatchTimeMs
CpuDrawDispatchTimeMs
bReadbackReady
```

### 验证标准

使用当前固定测试 frustum plane，测试：

- `256 -> GpuVisibleCount = 64`
- `1024 -> GpuVisibleCount = 256`
- `4096 -> GpuVisibleCount = 1024`

通过条件：

- GPU readback 数值与 `estimated-visible` 一致。
- 多次 Play / Stop 不出现 readback 崩溃或旧数据误读。
- 未 ready 时蓝图查询应返回清晰状态，而不是读未初始化数据。

## 里程碑 2：真实相机 Frustum Plane

### 目标

把当前 `BuildTestFrustumPlanes()` 的固定测试平面替换为真实相机视锥平面。

第一版不需要自动接入 UE 主渲染视图，可以先让蓝图显式传入相机 actor 或相机参数。

### 推荐接口

优先新增一个测试入口，避免破坏当前固定测试节点：

```cpp
Execute Camera Frustum Culled Indirect Draw(
    UTextureRenderTarget2D* OutputRenderTarget,
    AActor* CameraActor,
    int32 InstanceCount = 1024,
    float FieldOfViewDegrees = 90.0f,
    float NearPlane = 10.0f,
    float FarPlane = 10000.0f)
```

如果直接依赖 camera component 更清晰，也可以改为：

```cpp
UCameraComponent* CameraComponent
```

推荐优先选择 `AActor* CameraActor`，因为蓝图连线成本低，后续再收紧类型。

### Frustum Plane 构造建议

C++ 侧从相机 transform 和 FOV 构造 6 个平面：

- Near
- Far
- Left
- Right
- Top
- Bottom

第一版可以只支持透视相机。

坐标约定必须在代码和文档中写清楚：

- 实例当前位于测试网格的世界坐标。
- 相机 frustum plane 也必须在同一世界坐标系下。
- shader 中继续使用 `dot(Normal, Center) + W >= -Radius`。

### 验证标准

测试时移动或旋转相机：

- 相机看向网格中心：可见数量较高。
- 相机偏离网格：可见数量下降。
- 相机背向网格：可见数量接近 0。

通过条件：

- GPU visible count readback 会随相机变化。
- RenderTarget 上绘制的实例数量和位置变化符合相机方向。
- Output Log 中能同时看到 `estimated-visible` 和 `gpu-visible`。

## 数据流

本阶段完成后的目标数据流：

```text
Blueprint
-> Execute Camera Frustum Culled Indirect Draw
-> CPU 生成测试实例
-> CPU 从 CameraActor 构造 frustum plane
-> Upload Instance Buffer
-> Dispatch Frustum Culling Compute Shader
-> GPU 写 VisibleInstanceBuffer
-> GPU 写 IndirectArgsBuffer.InstanceCount
-> GPU 写 VisibleCountSummary
-> DrawPrimitiveIndirect
-> GPU readback VisibleCountSummary
-> Blueprint 查询 Last Frustum Cull Result
```

## 测试计划

### 固定 frustum readback 测试

1. 使用 `Execute Test Frustum Culled Indirect Draw`。
2. 分别测试 `256`、`1024`、`4096`。
3. 调用 `Get Last Frustum Cull Result`。
4. 记录 `EstimatedVisibleCount` 与 `GpuVisibleCount`。

预期：

```text
EstimatedVisibleCount == GpuVisibleCount
```

### 相机 frustum 测试

1. 在场景中放置测试相机或使用 player camera。
2. 使用新的 camera frustum 节点。
3. 让相机看向网格中心。
4. 让相机偏离或背向网格。
5. 观察 RenderTarget 和 readback 数值变化。

预期：

- 相机看向网格时，可见实例较多。
- 相机偏离网格时，可见实例减少。
- 相机背向网格时，可见实例接近 0。

## 成功标准

- [x] GPU visible count 能 readback 到 CPU。
- [x] 新增结果查询蓝图节点或等价调试接口。
- [x] 能从相机 transform 构造 frustum plane。
- [x] 有对应测试文档。
- [x] 有对应学习日志。
- [ ] `EstimatedVisibleCount` 与 `GpuVisibleCount` 在固定测试 frustum 下匹配。
- [ ] GPU culling 结果会随相机变化。
- [ ] RenderTarget 画面与 readback 数量一致。

## 当前实现说明

当前代码已经完成两部分实现：

1. `Execute Test Frustum Culled Indirect Draw` 会把 GPU visible count 写入 summary buffer，并通过 `Get Last Frustum Cull Result` 回读到 CPU。
2. `Execute Camera Frustum Culled Indirect Draw` 会从 `CameraActor` 的世界变换构造 6 个世界空间 frustum plane，并复用现有 culling + indirect draw 路径。

当前仍缺最后一步 UE 侧验证：需要用户编译后在蓝图里实际调用新节点，确认 `GpuVisibleCount`、画面和相机变化三者一致。

## 风险与排查重点

### 1. Readback 生命周期

`FRHIGPUBufferReadback` 是异步路径。必须明确：

- 什么时候创建 readback。
- 什么时候查询 `IsReady()`。
- 未 ready 时返回什么。
- 新一轮 dispatch 是否会覆盖上一轮未读结果。

建议复用 `GPUDrivenInstanceBufferInterface.cpp` 中已有 readback 模式，但避免不必要的同步 stall。

### 2. 坐标系和平面方向

真实相机 frustum 最大风险是平面法线方向错误。

如果方向反了，常见表现是：

- 视野内全部被裁掉。
- 视野外反而被保留。
- 相机背向网格时仍然显示大量实例。

需要先在 CPU 侧用同一套 plane 做 `EstimatedVisibleCount`，再和 GPU readback 比较。

### 3. RenderTarget 显示不等于严格计数

画面只能证明大方向正确，不能证明数量完全正确。

本阶段必须依赖 GPU visible count readback 形成严格验证。

### 4. 当前单线程 shader 不是性能目标

本阶段即使 `GpuVisibleCount` 正确，也不能得出性能结论。

并行 culling 应作为下一阶段单独处理。

## 完成后需要更新

实现完成后新增或更新：

- `docs/test/test-gpu-visible-count-and-camera-frustum.md`
- `docs/learning/YYYY-MM-DD-HHMM-gpu-visible-count-and-camera-frustum.md`
- `docs/plan/index.md`
- `docs/plan/plan-2026-06-03-gpu-driven-execution.md`
