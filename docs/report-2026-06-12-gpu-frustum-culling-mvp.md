# 2026-06-12 GPU Frustum Culling MVP 阶段报告

## 摘要

本报告记录 `GPU Frustum Culling MVP` 的第一轮验证结果。

当前项目已经从：

```text
GPU 写 indirect args 并驱动最小 draw
```

推进到：

```text
GPU 根据实例可见性决定最终画哪些实例
```

这一阶段的目标不是做高性能 culling，而是先验证整条 GPU 路径是否正确接通。

## 当前实现范围

当前实现位于：

- [GPUDrivenIndirectDrawInterface.cpp](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/GPUDrivenIndirectDrawInterface.cpp)
- [FrustumCullShader.h](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/FrustumCullShader.h)
- [FrustumCullShader.cpp](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/FrustumCullShader.cpp)
- [FrustumCullInstances.usf](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Shaders/FrustumCullInstances.usf)

当前行为：

- CPU 生成规则网格测试实例
- CPU 生成固定测试 frustum plane
- GPU compute shader 对实例做包围球平面测试
- GPU 写出 compact visible instance buffer
- GPU 写出 `DrawPrimitiveIndirect` 参数
- graphics shader 只绘制 visible instance buffer

## 实测日志

用户在 UE 中完成验证，关键日志如下：

```text
GPUDrivenPipeline: Frustum culled indirect draw submitted 256 source instances to RT_GPUComputeOutput (1024x1024, estimated-visible=64, instance-bytes=8192, cpu-cull=0.042 ms, cpu-draw=0.000 ms).
GPUDrivenPipeline: Frustum culled indirect draw submitted 1024 source instances to RT_GPUComputeOutput (1024x1024, estimated-visible=256, instance-bytes=32768, cpu-cull=0.018 ms, cpu-draw=0.000 ms).
GPUDrivenPipeline: Frustum culled indirect draw submitted 4096 source instances to RT_GPUComputeOutput (1024x1024, estimated-visible=1024, instance-bytes=131072, cpu-cull=0.009 ms, cpu-draw=0.000 ms).
```

## 结果解读

### 1. source instances

`source instances` 是 CPU 生成并上传到 GPU 的原始实例总数。

- `256`
- `1024`
- `4096`

这些数据说明不同实例规模下，buffer 创建、shader dispatch 和 indirect draw 提交都已成功发生。

### 2. estimated-visible

`estimated-visible` 是 CPU 使用和 GPU 相同的测试 frustum plane 做出的理论可见数量估算。

结果分别是：

- `256 -> 64`
- `1024 -> 256`
- `4096 -> 1024`

这与当前测试网格的中间四分之一区域保留逻辑一致。

在规则网格下，可见区域相当于宽度保留一半、高度保留一半，因此面积约为总实例数的 `1/4`。

### 3. instance-bytes

`instance-bytes` 反映上传到 GPU 的实例 buffer 大小：

- `256 -> 8192`
- `1024 -> 32768`
- `4096 -> 131072`

这说明当前 `FGPUDrivenInstanceData` 仍然是 `32` 字节一条记录。

### 4. cpu-cull / cpu-draw

这两个数字只是 CPU 提交命令的耗时，不是 GPU 真正执行耗时。

它们当前只能说明：

- culling dispatch 已提交
- indirect draw 命令已提交

它们不能用于判断 GPU culling 或 draw 的真实性能。

## 当前阶段已经证明的内容

- 最小 frustum culling compute shader 已经接通。
- GPU 可以把可见实例写入 `VisibleInstanceBuffer`。
- GPU 可以把可见实例数量写入 `IndirectArgsBuffer.InstanceCount`。
- `DrawPrimitiveIndirect` 已经开始消费 GPU culling 结果，而不是原始实例总数。
- 三个实例规模下路径都能稳定执行。

## 当前阶段还没有证明的内容

- 可见实例数量的 GPU readback 严格统计
- 跟随真实相机变化的 frustum culling
- 并行 culling 的性能表现
- 真实场景实例来源接入
- UE StaticMesh / Nanite 渲染路径接入

## 结论

当前 `GPU Frustum Culling MVP` 可以判定为：

```text
功能验证通过
```

项目当前已经具备一条完整但最小化的 GPU-driven 可见性路径：

```text
CPU 提供实例 -> GPU 决定哪些实例可见 -> GPU 写 indirect args -> GPU 只绘制可见实例
```

## 下一步建议

下一阶段建议按这个顺序推进：

1. 增加 GPU visible count readback，获得严格的 GPU 端可见数量验证。
2. 把固定测试 frustum plane 替换为真实相机 frustum plane。
3. 再把当前单线程 culling 改成并行版本，处理真正的性能问题。
