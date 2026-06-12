# GPU Frustum Culling MVP 开发计划

## 当前状态

当前项目已经具备两条关键基础路径：

- `FGPUDrivenInstanceData` 的 structured buffer 上传与 GPU 验证路径已完成。
- `Execute Test Indirect Instance Draw` 已验证通过，GPU 可以写出 indirect args 并驱动最小实例绘制。

这意味着下一阶段不需要再证明“GPU 能不能画”，而是要开始证明：

```text
GPU 能不能决定哪些实例应该被画
```

## 目标

建立第一条最小 GPU frustum culling 路径，用来验证下面这件事：

```text
CPU 提供测试实例 -> GPU compute 做视锥裁剪 -> GPU 写出可见实例列表和 indirect args -> indirect draw 只绘制可见实例
```

本阶段的关键不是接入真实场景，也不是做高性能优化，而是先证明：

```text
可见性判断已经从 CPU 转移到 GPU，并且可以直接影响最终 draw 结果
```

## 范围约束

第一版只做最小 MVP，不做这些内容：

- Hi-Z 或 occlusion culling
- LOD 选择
- StaticMesh / Nanite 渲染路径接入
- 自动扫描关卡 Actor 生成实例
- Tick 每帧持续上传复杂场景数据
- 真正的 GPU 时间统计

## 设计选择

### 输入数据

继续复用现有：

```cpp
FGPUDrivenInstanceData
```

第一版仍允许 CPU 生成规则测试实例。

### 裁剪依据

第一版使用简化视锥信息即可，优先选择下面两种方案之一：

1. CPU 传入相机位置、朝向、FOV、近平面、远平面，再在 compute shader 中做判断。
2. CPU 直接传入 6 个 frustum plane，compute shader 用包围球做 plane test。

推荐优先做第 2 种，因为 shader 侧判断更直接，也更接近后续扩展方向。

### 输出结果

compute shader 至少写出两类结果：

- `VisibleInstanceBuffer`
  只保存通过 culling 的实例数据，供后续 vertex shader 使用。
- `IndirectArgsBuffer`
  其中 `InstanceCount` 必须等于可见实例数量。

必要时可以新增一个 debug summary/readback，用来把可见实例数量回读到 CPU，方便验证。

## 关键代码改动

### 1. 新增 frustum culling compute shader

建议新增：

- `Plugins/GPUDrivenPipeline/Shaders/FrustumCullInstances.usf`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/FrustumCullShader.h`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/FrustumCullShader.cpp`

职责：

- 读取 `StructuredBuffer<FGPUDrivenInstanceData>`
- 对每个实例做包围球视锥测试
- 把可见实例写入新的 structured buffer
- 统计可见实例数量
- 写出 indirect draw 所需的 `InstanceCount`

### 2. 新增蓝图测试入口

建议新增蓝图节点：

```cpp
Execute Test Frustum Culled Indirect Draw(
    UTextureRenderTarget2D* OutputRenderTarget,
    int32 InstanceCount = 1024)
```

第一版可沿用当前 deterministic test instances 的生成逻辑。

### 3. 改造 draw 读取路径

当前 `IndirectDrawInstances.usf` 里的 vertex shader 读取的是原始 `InstanceData`。

本阶段要改成：

- vertex shader 读取 `VisibleInstanceBuffer`
- `DrawPrimitiveIndirect` 的实例数量来自 culling shader 写出的 args buffer

### 4. 增加调试验证

建议至少保留一个最小调试能力：

- Output Log 输出总实例数和可见实例数
- 或提供一个 readback 节点读取 `VisibleCount`

第一版的重点不是零 readback，而是快速确认 culling 结果是否正确。

## 数据流

```text
BeginPlay
-> CPU 生成测试实例
-> Upload Instance Buffer
-> Dispatch Frustum Culling Compute Shader
-> GPU 写 VisibleInstanceBuffer
-> GPU 写 IndirectArgsBuffer.InstanceCount
-> DrawPrimitiveIndirect
-> RT_GPUComputeOutput 显示可见实例
```

## 测试计划

用户编译后，在 UE 中测试：

1. 在 `BP_GPUComputePassDemo` 的 `BeginPlay` 链路中调用新的 frustum-culling 节点。
2. `OutputRenderTarget` 传入 `RT_GPUComputeOutput`。
3. `InstanceCount` 先填 `1024`。
4. 使用当前固定测试 frustum plane 覆盖部分实例，而不是覆盖全部实例。
5. 点击 Play。

预期结果：

- 平面上只显示一部分实例，而不是全部实例。
- 当前固定测试 frustum plane 会稳定裁掉外圈实例，只保留中间区域。
- Output Log 能看到总实例数和可见实例数。
- 多次 Play 不崩溃、不出现 `device removed`。

建议测试规模：

1. `256`
2. `1024`
3. `4096`

## 成功标准

- [x] 存在最小 frustum culling compute shader
- [x] GPU 能输出可见实例列表
- [x] GPU 能写出匹配可见实例数量的 indirect args
- [x] draw 阶段只绘制可见实例
- [x] Output Log 能输出同参数 CPU 估算可见数量，便于和画面做第一版对照
- [x] 有对应中文测试文档
- [x] 有对应中文学习日志

## 当前实现说明

第一版已经实现为单 dispatch、单线程 GPU 循环。这样做的目的不是性能，而是避免第一版引入跨线程组同步、append counter 或 prefix sum 的复杂度。

当前日志中的 `estimated-visible` 是 CPU 使用同一组测试 frustum plane 计算出的估算值；真正控制绘制数量的是 `FrustumCullInstances.usf` 写入 `IndirectArgsBuffer.InstanceCount` 的 GPU 结果。

后续性能版应改为并行 culling，并增加 GPU visible count readback 或 debug buffer，以获得更严格的 GPU 端验证数据。

## 实际验证结果

用户已经在 UE 中完成当前 MVP 的最小验证，关键日志如下：

```text
GPUDrivenPipeline: Frustum culled indirect draw submitted 256 source instances to RT_GPUComputeOutput (1024x1024, estimated-visible=64, instance-bytes=8192, cpu-cull=0.042 ms, cpu-draw=0.000 ms).
GPUDrivenPipeline: Frustum culled indirect draw submitted 1024 source instances to RT_GPUComputeOutput (1024x1024, estimated-visible=256, instance-bytes=32768, cpu-cull=0.018 ms, cpu-draw=0.000 ms).
GPUDrivenPipeline: Frustum culled indirect draw submitted 4096 source instances to RT_GPUComputeOutput (1024x1024, estimated-visible=1024, instance-bytes=131072, cpu-cull=0.009 ms, cpu-draw=0.000 ms).
```

这说明：

- 当前固定测试 frustum plane 稳定保留中间区域实例。
- `estimated-visible` 与规则网格下的四分之一区域保留逻辑一致。
- 三个实例规模都能稳定提交 culling 和 indirect draw 命令。
- 当前阶段已经证明：GPU culling 结果可以控制最终的 indirect draw 实例数量。

## 风险与排查重点

### 1. 可见实例写入冲突

如果多个线程同时写 visible buffer，需要明确写入索引分配方式，否则容易出现覆盖或计数错误。

### 2. indirect args 数量不匹配

如果 `InstanceCount` 和 visible buffer 真实写入数量不一致，画面可能缺实例、越界读或出现不可预测结果。

### 3. 资源状态转换错误

这一阶段至少会涉及：

- visible instance buffer 的 UAV 写入到 SRV 读取
- indirect args buffer 的 UAV 写入到 `IndirectArgs`
- render target 的 `SRVMask` 到 `RTV`

任何一个状态错了，都可能出现黑屏或 RHI warning。

### 4. 调试观察不足

如果第一版完全没有可见实例计数输出，排查成本会明显升高。因此建议保留最小 readback 或日志输出。

## 完成后需要补充的文档

实现完成后新增或更新：

- `docs/test-gpu-frustum-culling-mvp.md`
- `docs/learning/YYYY-MM-DD-HHMM-gpu-frustum-culling-mvp.md`
- `docs/plan/index.md`
- `docs/plan/plan-2026-06-03-gpu-driven-execution.md`
