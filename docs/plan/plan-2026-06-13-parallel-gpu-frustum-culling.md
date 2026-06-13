# Parallel GPU Frustum Culling 开发计划

## 当前状态

当前 `GPUDrivenPipeline` 已经完成了从固定测试 frustum 到真实相机 frustum 的 correctness MVP。

已经具备的能力：

- CPU 可以上传规则测试实例数据。
- GPU compute shader 可以进行 frustum culling。
- GPU 可以写出 compact visible instance buffer。
- GPU 可以写出 indirect draw args。
- GPU visible count 可以通过 readback 回到 CPU 侧验证。
- camera frustum 路径可以从 `PlayerCameraManager` 获取运行时相机参数。
- plane-bound debug 路径可以把测试实例绑定到承载 `RT_GPUComputeOutput` 的平面 Actor bounds。

当前最大的实现限制是：

```text
FrustumCullInstances.usf 仍然使用单线程循环处理全部 instances
```

这适合证明链路正确，但不适合作为 GPU-driven renderer 的长期基础。

## 本阶段目标

本阶段目标是把当前单线程 culling MVP 推进为并行 GPU culling MVP。

核心目标：

```text
每个 GPU thread 处理一个或一小组 instance，
通过 atomic counter 写出 visible instance list，
并由 GPU 端 visible count 驱动 indirect draw args。
```

完成后，项目应该具备：

- 并行 frustum culling shader。
- GPU 端 atomic visible counter。
- 并行写入 compact visible instance buffer。
- GPU 端写入 indirect draw args 的 instance count。
- 继续支持 `Get Last Frustum Cull Result` readback 验证。
- 保留现有单线程路径作为 baseline 或回退参考。

## 范围约束

本阶段只解决“并行化 culling”。

暂时不做：

- Hi-Z occlusion culling。
- LOD selection。
- Nanite / StaticMesh 真实渲染管线接入。
- 多 draw command batching。
- GPU timestamp 精确性能统计。
- 完整场景 actor 自动扫描。
- 严格解决 RT 平面预览尺寸与玩家视野的像素级一致性。

保留这些约束，是为了避免把并行 culling、真实实例来源、最终画面投影一致性混在一个阶段里。

## 设计方向

### 1. 保留当前单线程路径

当前 `FrustumCullInstances.usf` 的单线程版本已经验证通过，建议保留为 baseline。

可选实现方式：

- 新增一个并行 shader 文件，例如 `ParallelFrustumCullInstances.usf`。
- 或者在现有 shader 中增加新的 entry point，例如 `CullInstancesParallelCS`。

优先建议新增新的 entry point，这样可以复用现有结构体和参数，同时减少文件数量。

### 2. 使用 atomic counter 写 visible list

并行 shader 的核心逻辑：

```hlsl
uint InstanceIndex = DispatchThreadID.x;
if (InstanceIndex >= InstanceCount)
{
    return;
}

if (IsInstanceVisible(Instance))
{
    uint VisibleIndex = 0;
    InterlockedAdd(OutVisibleCount[0], 1, VisibleIndex);
    OutVisibleInstanceData[VisibleIndex] = Instance;
}
```

其中：

- `OutVisibleCount[0]` 是 GPU 端 visible counter。
- `VisibleIndex` 是当前 visible instance 写入 compact buffer 的位置。
- `OutVisibleInstanceData` 仍然作为 draw 阶段读取的 visible instance buffer。

### 3. 写 indirect args

当前 indirect args 格式为：

```text
OutIndirectArgs[0] = 6
OutIndirectArgs[1] = VisibleCount
OutIndirectArgs[2] = 0
OutIndirectArgs[3] = 0
```

并行版本需要确保 culling 完成后再写 indirect args。

可选方案：

1. 两个 compute pass：
   - pass A：并行 culling，写 visible count 和 visible list。
   - pass B：单线程读取 visible count，写 indirect args 和 summary。

2. 一个 compute pass 内处理：
   - 每个线程做 culling。
   - 额外由某个线程写 args。
   - 但需要严格同步，不适合跨 thread group 的全局计数结果。

本阶段优先采用方案 1，因为逻辑更清晰，也更容易验证。

## 预期新增或调整的源文件

可能涉及：

- `Plugins/GPUDrivenPipeline/Shaders/FrustumCullInstances.usf`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/FrustumCullShader.h`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/FrustumCullShader.cpp`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/GPUDrivenIndirectDrawInterface.h`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/GPUDrivenIndirectDrawInterface.cpp`

如果为了让 baseline 和 parallel 路径更清晰，也可以新增：

- `ParallelFrustumCullShader.h`
- `ParallelFrustumCullShader.cpp`

但优先避免过早拆文件，除非现有 shader 参数结构开始明显膨胀。

## 蓝图接口建议

保留现有接口：

```text
Execute Test Frustum Culled Indirect Draw
Execute Camera Frustum Culled Indirect Draw
Execute Plane Camera Frustum Culled Indirect Draw
Get Last Frustum Cull Result
```

新增一个 parallel 测试入口：

```text
Execute Parallel Plane Camera Frustum Culled Indirect Draw
```

推荐参数：

```cpp
UTextureRenderTarget2D* OutputRenderTarget
AActor* CameraActor
AActor* PlaneActor
int32 InstanceCount = 4096
float FieldOfViewDegrees = 90.0f
float NearPlane = 10.0f
float FarPlane = 10000.0f
```

原因：

- 先复用当前最接近真实测试场景的 plane-bound 路径。
- 避免让 parallel 阶段重新纠结“固定 debug grid 是否和场景平面对齐”。
- 方便与当前已验证的 serial plane camera path 对比。

## 里程碑

### 里程碑 1：并行 culling shader 编译通过

目标：

- 新增并行 culling entry point。
- 使用 `numthreads(64, 1, 1)` 或 `numthreads(128, 1, 1)`。
- dispatch group 数量由 `InstanceCount` 推导。

验收标准：

- UE 编译通过。
- shader 能被正确注册。
- Output Log 不出现 shader compile error。

### 里程碑 2：visible counter 与 visible list 正确

目标：

- GPU 并行写入 visible counter。
- GPU 并行写入 compact visible instance buffer。

验收标准：

- `GpuVisibleCount` 可以 readback。
- 固定 frustum 测试下，parallel 结果与 serial baseline 一致。
- 多次 Play / Stop 不出现随机数量或旧数据。

### 里程碑 3：indirect draw 使用并行结果

目标：

- GPU 根据 parallel visible count 写 indirect args。
- draw 只绘制 parallel visible list。

验收标准：

- RT 画面随 frustum 变化。
- `estimated-visible`、`gpu-visible` 和画面趋势一致。
- `DrawPrimitiveIndirect` 没有读取未初始化 args。

### 里程碑 4：规模测试

目标：

测试不同 instance count 下的稳定性。

建议测试规模：

- `1024`
- `4096`
- `16384`
- `65536`

记录数据：

- `InstanceCount`
- `EstimatedVisibleCount`
- `GpuVisibleCount`
- `BufferSizeBytes`
- `cpu-cull`
- `cpu-draw`
- 是否出现黑屏、闪烁、数量不稳定

### 里程碑 5：与 serial baseline 对比

目标：

保留 serial 路径作为 correctness baseline，对比 parallel 路径是否一致。

对比内容：

- visible count 是否一致。
- RT 画面是否一致或趋势一致。
- 大规模 instance 下 CPU submit 时间是否仍稳定。

注意：

当前 `cpu-cull` 仍然只是 CPU 侧提交和 dispatch 记录，不是 GPU elapsed time。
因此本阶段不把它作为真实性能结论，只作为稳定性参考。

## 测试计划

### 测试 1：固定 frustum correctness

使用固定测试 frustum，对比 serial 与 parallel：

```text
1024 source instances
4096 source instances
16384 source instances
```

预期：

- `GpuVisibleCount` 与 serial baseline 一致。
- RT 画面结构一致。

### 测试 2：Plane camera frustum correctness

使用当前场景里的 `BP_GPUComputePassDemo` 作为 `PlaneActor`。

预期：

- 相机正对 plane 时 visible count 较高。
- 相机偏离 plane 时 visible count 下降。
- 相机完全背向 plane 时 visible count 接近 0。

### 测试 3：压力与稳定性

使用 `65536` instances 连续 Play / Stop 多轮。

观察：

- 是否崩溃。
- 是否有 RHI validation warning。
- `GpuVisibleCount` 是否出现明显随机跳变。
- readback 是否稳定 ready。

## 风险点

### 1. Atomic counter 初始化

visible counter 必须在每次 dispatch 前清零。

如果没有正确清零，会出现：

- visible count 持续累加。
- indirect draw 读取越界。
- RT 随机闪烁或黑屏。

### 2. UAV 写入顺序

parallel culling pass 写 visible list 和 count 后，args pass 必须等 culling pass 完成。

需要明确资源 transition 或 pass 顺序，避免 args pass 读到旧 count。

### 3. Buffer 容量

`OutVisibleInstanceData` 容量至少要等于 `InstanceCount`。

如果 visible count 超过 buffer 容量，会直接造成越界写。
本阶段因为每个 instance 最多写一次，所以容量等于 source instance count 即可。

### 4. Readback 时机

readback 仍然是异步的。

蓝图侧查询时需要允许：

```text
pending -> ready
```

不能假设 dispatch 当帧立即可读。

## 完成标准

本阶段完成时，必须满足：

- [ ] 新增 parallel culling shader 或 entry point。
- [ ] 新增 parallel plane camera frustum 蓝图入口。
- [ ] parallel 路径能写 visible list。
- [ ] parallel 路径能写 indirect args。
- [ ] `GpuVisibleCount` 能 readback。
- [ ] fixed frustum 下 parallel 与 serial baseline 一致。
- [ ] plane camera frustum 下 visible count 会随相机变化。
- [ ] `1024 / 4096 / 16384 / 65536` 四档测试至少完成一轮。
- [ ] 对应测试文档放入 `docs/test/`。
- [ ] 对应学习记录放入 `docs/learning/`。

## 后续衔接

并行 culling 完成后，下一阶段再考虑：

- 真实场景实例来源接入。
- 简单 GPU LOD selection。
- 多 draw command 或 mesh batch 结构。
- Hi-Z occlusion culling。
- GPU timestamp 性能统计。

优先级建议：

```text
parallel culling -> 真实实例来源 -> GPU LOD -> Hi-Z occlusion
```
