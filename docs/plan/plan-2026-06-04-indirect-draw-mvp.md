# Indirect Draw MVP 开发计划

## Summary

下一阶段目标是做一个最小可见的 `indirect draw` demo：点击 Play 后，CPU 生成测试实例数据，GPU compute shader 写出 indirect draw 参数，随后 graphics shader 使用这些参数把一批实例绘制到 `RT_GPUComputeOutput`，并继续通过现有平面材质显示结果。

默认选择：

- 绘制目标：继续画到 `RT_GPUComputeOutput`，不直接接入 UE 场景 StaticMesh 渲染路径。
- 绘制内容：每个实例绘制一个小方块或小三角形，颜色由 `Flags` 决定。
- 触发方式：沿用 `BeginPlay`。
- 第一版不做 GPU culling、不做 Tick、不做真实 GPU 时间统计、不接 Nanite/StaticMesh pipeline。

## Key Changes

### 代码与接口

新增蓝图节点：

```cpp
Execute Test Indirect Instance Draw(UTextureRenderTarget2D* OutputRenderTarget, int32 InstanceCount = 1024)
```

作用：生成测试实例数据、上传 structured buffer、让 GPU 写 indirect args，并执行一次 indirect draw。

新增 indirect args compute shader：

- 输入：`InstanceCount`
- 输出：indirect args buffer
- 目标参数：

```text
VertexCountPerInstance = 6
InstanceCount = 输入实例数量
StartVertexLocation = 0
StartInstanceLocation = 0
```

新增简单 graphics shader：

- Vertex Shader 使用 `SV_VertexID` 生成一个 quad 的 6 个顶点。
- 使用 `SV_InstanceID` 读取 `FGPUDrivenInstanceData`。
- 把 `Position` 映射到 RenderTarget 的 2D 坐标。
- Pixel Shader 根据 `Flags` 输出不同颜色。

复用当前实例数据结构：

```cpp
FGPUDrivenInstanceData
```

本阶段不扩展 transform、mesh id、material id。

### 数据流

```text
BeginPlay
-> Execute Simple Compute Shader
-> Upload Test Instance Data / validation
-> Execute Test Indirect Instance Draw
-> RT_GPUComputeOutput 显示实例绘制结果
```

本阶段的关键不是“画得漂亮”，而是证明：

```text
GPU 写出的 indirect args 可以驱动一次真实 draw
```

### 文档

实现完成后新增或更新：

- `docs/test/test-indirect-draw-mvp.md`
- `docs/learning/YYYY-MM-DD-HHMM-indirect-draw-mvp.md`

学习日志必须结合源码讲解：

- indirect args buffer 的 4 个字段
- compute shader 如何写 draw 参数
- graphics shader 如何使用 `SV_VertexID` / `SV_InstanceID`
- 为什么本阶段先画到 RenderTarget，而不是直接接 StaticMesh 渲染路径

## Test Plan

用户编译后，在 UE 中测试：

1. 打开 `BP_GPUComputePassDemo`。
2. 在 `BeginPlay` 链路中调用 `Execute Test Indirect Instance Draw`。
3. `OutputRenderTarget` 传入 `RT_GPUComputeOutput`。
4. `InstanceCount` 先填 `1024`。
5. 点击 Play。

预期结果：

- 平面不再只是渐变，而是显示一批规则排列的彩色实例。
- Output Log 出现 indirect args 构建日志。
- Output Log 出现 indirect draw 执行日志。
- `InstanceCount = 1024` 时能稳定显示。
- 改成 `256`、`4096` 时仍能显示对应规模。
- 多次 Play 不崩溃、不出现 D3D device removed。

排查重点：

- 如果画面空白，先检查 RenderTarget 是否有效、graphics shader 是否编译成功。
- 如果实例数量不对，检查 indirect args buffer 的 `InstanceCount`。
- 如果颜色异常，检查 `Flags` 是否按实例正确读取。
- 如果 UE 崩溃，优先检查 buffer usage、resource transition 和 RenderTarget 状态。

## Assumptions

- 这是 `indirect draw MVP`，不是完整 GPU-driven renderer。
- 本阶段允许 CPU 生成测试实例数据，但 draw 参数必须由 GPU compute shader 写出。
- 第一版绘制到 RenderTarget，避免过早接入 UE 场景渲染、StaticMesh、材质系统和可见性系统。
- 需要编译时只通知用户，由用户手动编译。
- 不修改 `pcgDoc/`。
- 完成开发任务后，需要补源码结合型 learning 日志。
