# GPU Frustum Culling MVP 学习记录

## 这次做了什么

这次在 `Indirect Draw MVP` 之后，新增了一条最小 GPU frustum culling 路径。

核心目标是证明：

```text
GPU 不只是执行 draw，还可以先决定哪些实例应该参与 draw
```

相关源码：

- [GPUDrivenIndirectDrawInterface.h](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/GPUDrivenIndirectDrawInterface.h)
- [GPUDrivenIndirectDrawInterface.cpp](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/GPUDrivenIndirectDrawInterface.cpp)
- [FrustumCullShader.h](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/FrustumCullShader.h)
- [FrustumCullShader.cpp](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/FrustumCullShader.cpp)
- [FrustumCullInstances.usf](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Shaders/FrustumCullInstances.usf)

## 核心链路

新增蓝图节点：

```cpp
Execute Test Frustum Culled Indirect Draw(UTextureRenderTarget2D* OutputRenderTarget, int32 InstanceCount = 1024)
```

当前链路是：

```text
CPU 生成测试实例
-> 上传原始 InstanceData structured buffer
-> 创建 VisibleInstanceData UAV/SRV
-> 创建 IndirectArgs UAV
-> 创建 Summary UAV / Readback
-> FrustumCullInstances.usf 做 GPU 裁剪
-> GPU 写 VisibleInstanceData
-> GPU 写 IndirectArgs.InstanceCount
-> GPU 写 Summary.VisibleCount
-> DrawPrimitiveIndirect 绘制 visible buffer
-> CPU 通过 Readback 获取 GpuVisibleCount
-> RT_GPUComputeOutput 显示裁剪后的结果
```

## 关键结构

### `FFrustumCullInstancesShader`

这是新的 compute shader C++ 包装类。

它绑定了：

- `InstanceData`：原始实例数据 SRV
- `OutVisibleInstanceData`：裁剪后实例 UAV
- `OutIndirectArgs`：indirect draw 参数 UAV
- `OutSummary`：GPU visible count summary UAV
- `InstanceCount`：输入实例数量
- `FrustumPlane0` 到 `FrustumPlane5`：6 个测试视锥平面

### `FrustumCullInstances.usf`

这个 shader 做三件事：

1. 遍历所有输入实例。
2. 使用包围球和平面方程判断实例是否可见。
3. 把可见实例写入 compact visible buffer，并把可见数量写入 indirect args 和 summary buffer。

平面测试的核心形式是：

```hlsl
dot(Plane.xyz, Center) + Plane.w >= -Radius
```

这表示实例包围球只要没有完全落在平面外侧，就保留为可见。

## 为什么第一版用单线程循环

当前 shader 使用：

```hlsl
[numthreads(1, 1, 1)]
```

这不是性能方案，而是为了让第一版语义足够明确：

- 不需要跨线程组同步
- 不需要 append counter
- 不需要 prefix sum
- 可以在同一个 shader 中得到最终 visible count 并写入 indirect args

如果第一版直接做并行 compact list，就必须解决可见实例写入索引分配和最终计数同步问题。那些是下一阶段的性能工作，不应该阻塞当前 MVP。

## 和上一阶段的区别

`Indirect Draw MVP` 证明的是：

```text
GPU 写出的 indirect args 可以驱动 draw
```

这次新增的是：

```text
GPU culling 结果可以决定 indirect args 里的 InstanceCount
```

也就是说，draw 的实例数量已经不再只是 CPU 输入的 `InstanceCount`，而是由 GPU 裁剪后的可见数量决定。

## 当前限制

当前测试 frustum plane 是 CPU 固定生成的，不跟随 UE 摄像机移动。

当前 Output Log 的 `estimated-visible` 是 CPU 使用同一组测试平面估算出来的值，用于第一版调试对照；真正控制绘制数量的是 GPU 写入 `IndirectArgsBuffer` 的 `InstanceCount`。

当前已验证日志：

```text
GPUDrivenPipeline: Frustum culled indirect draw submitted 256 source instances to RT_GPUComputeOutput (1024x1024, estimated-visible=64, instance-bytes=8192, cpu-cull=0.042 ms, cpu-draw=0.000 ms).
GPUDrivenPipeline: Frustum culled indirect draw submitted 1024 source instances to RT_GPUComputeOutput (1024x1024, estimated-visible=256, instance-bytes=32768, cpu-cull=0.018 ms, cpu-draw=0.000 ms).
GPUDrivenPipeline: Frustum culled indirect draw submitted 4096 source instances to RT_GPUComputeOutput (1024x1024, estimated-visible=1024, instance-bytes=131072, cpu-cull=0.009 ms, cpu-draw=0.000 ms).
```

这些结果说明固定测试平面稳定保留了规则网格的中间四分之一区域，且当前 MVP 已经验证“GPU culling 结果可以控制最终 indirect draw 的实例数量”。

当前代码已经进一步补上第一版严格验证接口：

```cpp
Get Last Frustum Cull Result(FGPUDrivenFrustumCullResult& OutResult)
```

这个接口会通过 `FRHIGPUBufferReadback` 回读 GPU summary，并返回：

- `InstanceCount`
- `EstimatedVisibleCount`
- `GpuVisibleCount`
- `BufferSizeBytes`
- `CpuCullDispatchTimeMs`
- `CpuDrawDispatchTimeMs`

也就是说，当前阶段已经不再只依赖画面和 CPU 估算值，蓝图可以直接读取 GPU 端的可见实例数量。

后续如果要做更严格验证，应增加：

- 并行 culling 写入 visible list
- 从真实 view/projection 矩阵提取 frustum plane
- Tick 或交互参数驱动 frustum 变化

## 调试重点

### 1. 资源状态转换

本阶段新增了两个关键状态转换：

- `VisibleInstanceBuffer`：`UAVCompute -> SRVGraphics`
- `IndirectArgsBuffer`：`UAVCompute -> IndirectArgs`

如果转换错误，常见结果是黑屏、随机颜色或 RHI warning。

### 2. visible buffer 和 args 数量一致

vertex shader 读取的是 compact 后的 visible buffer。

`DrawPrimitiveIndirect` 的实例数量来自 GPU 写入的 indirect args。如果这两个结果不一致，可能出现越界读取或画面异常。

### 3. RenderTarget 覆盖

如果蓝图链路里后面又调用旧的 compute pass 或未裁剪 indirect draw，最终画面会被覆盖。

测试时应该让 `Execute Test Frustum Culled Indirect Draw` 成为最后一个写入 `RT_GPUComputeOutput` 的节点。
