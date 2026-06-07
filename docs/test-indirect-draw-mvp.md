# Indirect Draw MVP 测试流程

## 目标

验证 `GPUDrivenPipeline` 插件已经具备下面这条最小 indirect draw 路径：

- CPU 生成测试实例数据
- GPU compute shader 写出 `DrawPrimitiveIndirect` 参数
- graphics shader 使用 `SV_VertexID` 和 `SV_InstanceID` 绘制实例
- 绘制结果输出到 `RT_GPUComputeOutput`

## 前提

- 用户已经在本机完成 `Development Editor | Win64` 编译
- `GPUDrivenPipeline` 插件已启用
- `L_GPUComputePassDemo` 可以正常打开
- `RT_GPUComputeOutput` 可被平面材质正确显示

## 蓝图节点

分类：

```text
GPU Driven Pipeline | Indirect Draw
```

本阶段使用的新节点：

```text
Execute Test Indirect Instance Draw
```

参数：

- `Output Render Target`：传入 `RT_GPUComputeOutput`
- `Instance Count`：建议先用 `1024`

## 推荐测试链路

在 `BP_GPUComputePassDemo` 的 `BeginPlay` 链路中测试：

```text
BeginPlay
-> Execute Test Indirect Instance Draw(RT_GPUComputeOutput, 1024)
```

如果你还想保留旧的渐变 pass 做对照，建议暂时不要在同一条链路里同时调用两个节点，否则后调用的那一个会覆盖前一个写入的 RenderTarget 结果。

## 预期结果

Play 后，原本显示渐变的平面应该变成一批规则排列的小色块。

颜色由 `Flags` 决定，因此你应该看到重复分布的几种颜色，而不是单一颜色或纯黑画面。

Output Log 应该出现类似信息：

```text
GPUDrivenPipeline: IndirectDraw MVP rendered 1024 instances to RT_GPUComputeOutput ...
```

日志中应重点关注：

- `InstanceCount`
- `instance-bytes`
- `cpu-compute`
- `cpu-draw`

## 建议测试规模

按这个顺序验证：

1. `256`
2. `1024`
3. `4096`

观察点：

- 平面是否稳定显示
- 色块数量是否明显增多
- 多次 Play / Stop 是否稳定
- 是否出现 RHI 警告、shader 编译错误或 device removed

## 失败排查

### 画面还是旧的渐变

- 检查蓝图是否仍然先后调用了旧的 `Execute Simple Compute Shader`
- 检查 `BeginPlay` 最后一个写入 `RT_GPUComputeOutput` 的节点是谁

### 画面全黑

- 检查 `RT_GPUComputeOutput` 是否仍被材质正确采样
- 检查新 shader 是否编译成功
- 检查 Output Log 中是否有 indirect draw args buffer 或 render target 的 warning

### 没有出现新日志

- 检查蓝图是否已经改为调用 `Execute Test Indirect Instance Draw`
- 检查 C++ 是否已经成功编译并重启编辑器

### UE 崩溃或报 RHI 错误

- 优先检查 Output Log 中的 resource transition、draw indirect、shader compile 相关信息
- 如果只在大实例数量下崩溃，先退回 `256` 或 `1024` 验证最小路径

## 说明

这一阶段的目标不是证明性能收益，而是证明：

```text
GPU 写出的 indirect args 已经能够驱动一次真实 draw
```

等这个 MVP 稳定之后，下一步再进入更正式的实例可见性和 GPU culling 路径。
