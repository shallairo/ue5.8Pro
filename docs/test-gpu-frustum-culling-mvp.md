# GPU Frustum Culling MVP 测试流程

## 目标

验证 `GPUDrivenPipeline` 插件已经具备下面这条最小 GPU frustum culling 路径：

- CPU 生成测试实例数据
- GPU compute shader 做包围球视锥裁剪
- GPU 写出 compact visible instance buffer
- GPU 写出 `DrawPrimitiveIndirect` 参数
- graphics shader 只绘制可见实例
- 绘制结果输出到 `RT_GPUComputeOutput`

## 前提

- 用户已经在本机完成 `Development Editor | Win64` 编译
- `GPUDrivenPipeline` 插件已启用
- `L_GPUComputePassDemo` 可以正常打开
- `RT_GPUComputeOutput` 可被平面材质正确显示
- `Execute Test Indirect Instance Draw` 已经验证通过

## 蓝图节点

分类：

```text
GPU Driven Pipeline | Indirect Draw
```

本阶段使用的新节点：

```text
Execute Test Frustum Culled Indirect Draw
```

参数：

- `Output Render Target`：传入 `RT_GPUComputeOutput`
- `Instance Count`：建议先用 `1024`

## 推荐测试链路

在 `BP_GPUComputePassDemo` 的 `BeginPlay` 链路中测试：

```text
BeginPlay
-> Execute Test Frustum Culled Indirect Draw(RT_GPUComputeOutput, 1024)
```

测试时不要同时让旧的 `Execute Simple Compute Shader` 或 `Execute Test Indirect Instance Draw` 在后面继续写入同一个 RenderTarget，否则后执行的节点会覆盖本阶段结果。

## 预期结果

Play 后，平面应该只显示一部分规则排列的小色块，而不是铺满整个 RenderTarget。

当前第一版使用固定测试 frustum plane，大致保留规则网格中间区域的实例，因此画面应该明显少于未裁剪 indirect draw 的满屏色块。

Output Log 应该出现类似信息：

```text
GPUDrivenPipeline: Frustum culled indirect draw submitted 1024 source instances to RT_GPUComputeOutput ...
```

日志中应重点关注：

- `source instances`
- `estimated-visible`
- `cpu-cull`
- `cpu-draw`

说明：`estimated-visible` 是 CPU 使用同一组测试平面估算出来的可见数量。真正控制 draw 数量的是 GPU 写入 indirect args 的 `InstanceCount`。

## 建议测试规模

按这个顺序验证：

1. `256`
2. `1024`
3. `4096`

观察点：

- 平面是否只显示局部实例
- 实例数量增加后，可见区域内的色块密度是否增加
- 多次 Play / Stop 是否稳定
- 是否出现 shader 编译错误、RHI warning 或 `device removed`

## 失败排查

### 画面仍然铺满

- 检查蓝图是否调用的是 `Execute Test Frustum Culled Indirect Draw`
- 检查后面是否还有旧 indirect draw 节点覆盖了 RenderTarget
- 检查 Output Log 是否出现新的 frustum culled indirect draw 日志

### 画面全黑

- 检查 shader 是否编译成功
- 检查 `RT_GPUComputeOutput` 是否仍被材质正确采样
- 检查 Output Log 中是否有 buffer、UAV、SRV 或 resource transition 的 warning

### 实例数量不符合预期

- 先用 `1024` 验证，再改成 `256` 和 `4096`
- 当前 frustum plane 是固定测试参数，不跟随相机移动
- 第一版没有 GPU visible count readback，因此以画面变化和 `estimated-visible` 做对照

## 当前限制

这一版是 correctness MVP，不是性能实现。

当前 culling shader 使用单线程循环，目的是先验证：

```text
GPU culling 结果可以直接决定 indirect draw 的实例数量
```

后续性能版需要把 culling 改成并行写入 visible list，并增加更严格的 GPU 端 visible count readback。
