# Compute Shader 渐变输出测试

## 目标

验证 `GPUDrivenPipeline` 插件可以调度 `SimpleComputeShader.usf`，并把结果写入 `RT_GPUComputeOutput`。

## 前置条件

- UE5.8 Editor。
- `GPUDrivenPipeline` 插件已启用。
- `RT_GPUComputeOutput` 已启用 UAV 支持。
- `M_GPUComputeOutput` 正确采样 `RT_GPUComputeOutput`。
- `BP_GPUComputePassDemo` 在 `BeginPlay` 调用 `Execute Simple Compute Shader`。

## 测试步骤

1. 打开 `L_GPUComputePassDemo`。
2. 确认关卡中只有一个 `BP_GPUComputePassDemo`。
3. 点击 Play。
4. 观察平面材质。
5. 查看 Output Log。

## 预期结果

平面上应显示稳定渐变：

- 横向红色逐渐增强。
- 纵向绿色逐渐增强。
- 蓝色固定在中间值附近。

Output Log 应出现：

```text
GPUDrivenPipeline: Dispatched SimpleComputeShader ...
```

## 常见问题

画面为黑色：

- 检查蓝图是否执行到 `BeginPlay`。
- 检查材质是否采样了正确的 RenderTarget。
- 检查 Plane 是否使用 `M_GPUComputeOutput`。

蓝图节点找不到：

- 重新编译 Editor target。
- 重启 UE Editor。
- 确认插件已启用。

GPU crash：

- 优先检查 RenderTarget 是否启用 UAV 支持。
- 查看 Output Log 中是否有资源状态错误。

## 学习点

这个测试验证的是从蓝图到 C++，再到渲染线程和 GPU shader 的最短闭环。它证明 compute shader 可以写入一个 UE RenderTarget，但还不涉及实例数据、间接绘制或 GPU culling。
