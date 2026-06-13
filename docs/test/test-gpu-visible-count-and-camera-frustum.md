# GPU Visible Count 与 Camera Frustum 测试流程

## 目标

验证当前 `GPUDrivenPipeline` 已具备下面这两条能力：

- `Get Last Frustum Cull Result` 可以回读 GPU 端 visible count。
- `Execute Camera Frustum Culled Indirect Draw` 可以使用真实相机 frustum plane 影响 GPU culling 和最终 indirect draw。

## 前提

- 用户已经在本机完成 `Development Editor | Win64` 编译
- `GPUDrivenPipeline` 插件已启用
- `L_GPUComputePassDemo` 可以正常打开
- `RT_GPUComputeOutput` 可被平面材质正确显示
- 现有 `Execute Test Frustum Culled Indirect Draw` 已可正常运行

## 蓝图节点

分类：

```text
GPU Driven Pipeline | Indirect Draw
```

本阶段新增或会用到的节点：

```text
Execute Test Frustum Culled Indirect Draw
Execute Camera Frustum Culled Indirect Draw
Get Last Frustum Cull Result
```

## 测试 1：固定 frustum 的 GPU readback

推荐链路：

```text
BeginPlay
-> Execute Test Frustum Culled Indirect Draw(RT_GPUComputeOutput, 1024)
-> Delay(0.2)
-> Get Last Frustum Cull Result
-> Branch
```

`True` 分支打印：

```text
estimated={E} gpu={G} source={S}
```

建议验证规模：

1. `256`
2. `1024`
3. `4096`

预期：

- `EstimatedVisibleCount == GpuVisibleCount`
- `256 -> 64`
- `1024 -> 256`
- `4096 -> 1024`

如果 `Get Last Frustum Cull Result` 返回 `False`，先把 `Delay` 增加到 `0.3` 或 `0.5` 再测。

## 测试 2：真实相机 frustum

推荐链路：

```text
BeginPlay
-> Execute Camera Frustum Culled Indirect Draw(RT_GPUComputeOutput, CameraActor, 1024, 90.0, 10.0, 10000.0)
-> Delay(0.2)
-> Get Last Frustum Cull Result
-> Branch
```

其中：

- `CameraActor` 传入关卡中的相机 Actor
- 如果场景里已有专门测试相机，优先使用它

建议做三组测试：

1. 相机看向网格中心
2. 相机偏离网格
3. 相机背向网格

预期：

- 看向网格中心时，`GpuVisibleCount` 较高
- 偏离网格时，`GpuVisibleCount` 下降
- 背向网格时，`GpuVisibleCount` 接近 `0`
- RenderTarget 上的彩色实例数量变化应和 `GpuVisibleCount` 一致

## 重点观察

- `GpuVisibleCount` 是否已经能稳定回读
- `EstimatedVisibleCount` 和 `GpuVisibleCount` 是否一致
- 画面是否会随相机变化
- Output Log 是否出现 `Frustum cull readback ready`
- 多次 `Play / Stop` 是否稳定

## 失败排查

### 一直没有 readback

- 增加 `Delay`
- 确认日志里先出现 `readback=pending`
- 检查 Output Log 中是否有 summary buffer 或 readback warning

### 相机移动了但数量不变

- 检查蓝图是否调用的是 `Execute Camera Frustum Culled Indirect Draw`
- 检查 `CameraActor` 是否真的传入了目标相机
- 检查相机是否确实朝向网格，而不是位置变化很小但方向没变

### 背向网格仍然有大量实例

- 优先怀疑 frustum plane 法线方向
- 对比 `EstimatedVisibleCount` 和 `GpuVisibleCount`
- 检查相机的 forward / right / up 是否符合预期

## 当前限制

这一版仍然不是性能版。

当前 shader 仍然是单线程循环，目标只是先验证：

```text
GPU visible count readback 正确
真实相机 frustum 可以影响 GPU culling
```

后续性能版再处理并行 culling 和更正式的场景接入。
