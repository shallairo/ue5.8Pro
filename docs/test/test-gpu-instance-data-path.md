# GPU 实例数据路径测试

## 目标

验证 `GPUDrivenPipeline` 插件已经具备下面这条数据路径：

- CPU 生成测试实例数据
- 实例数据上传到 GPU structured buffer
- compute shader 读取实例数据
- shader 写出 summary
- CPU 通过 readback 读取 summary 并在蓝图中检查

## 使用的蓝图节点

分类：

```text
GPU Driven Pipeline | Instance Data
```

本测试使用两个节点：

- `Upload Test Instance Data`
- `Get Last Instance Data Validation Result`

## 推荐蓝图流程

可以直接在 `BP_GPUComputePassDemo` 中测试。

推荐执行链：

```text
BeginPlay
-> Execute Simple Compute Shader
-> Upload Test Instance Data(1024)
-> Delay(0.2)
-> Get Last Instance Data Validation Result
-> Branch
```

建议：

- `True` 分支打印验证结果
- `False` 分支先打印 `validation not ready`

推荐打印文本：

```text
processed={P} valid={V} flags={F} checksum={C}
```

其中：

- `P` 对应 `ProcessedCount`
- `V` 对应 `ValidRadiusCount`
- `F` 对应 `FlagSum`
- `C` 对应 `PositionChecksum`

## 实际验证日志

本阶段已经完成一次真实验证，Output Log 如下：

```text
GPUDrivenPipeline: Uploaded 1024 instance records (32768 bytes) and dispatched InstanceDataValidation (groups=16,1,1, cpu-dispatch=0.009 ms).
GPUDrivenPipeline: Instance validation readback ready (processed=1024, valid-radius=1024, flag-sum=1536, checksum=1587200).
```

## 1024 个实例的期望结果

如果 `Instance Count = 1024`，预期结果应为：

- `ProcessedCount = 1024`
- `ValidRadiusCount = 1024`
- `FlagSum = 1536`
- `PositionChecksum = 1587200`

说明：

- `ProcessedCount` 表示 GPU 读取并处理了多少条实例记录
- `ValidRadiusCount` 表示 `Radius > 0` 的实例数量
- `FlagSum` 用来验证 `Flags` 字段确实被 GPU 读取
- `PositionChecksum` 用来验证位置字段参与了 GPU 统计，而不是空读

## 排查步骤

### 蓝图里找不到节点

- 确认已经重新编译 `Development Editor | Win64`
- 重启 UE Editor
- 确认 `GPUDrivenPipeline` 插件已启用

### 一直没有 ready

- 不要在上传后立刻读取结果，先加一个短暂 `Delay`
- 检查 Output Log 是否存在 structured buffer 创建失败的 warning
- 检查 `InstanceDataValidation.usf` 是否有 shader 编译错误

### 结果数量不对

- 确认 `Upload Test Instance Data` 的 `Instance Count` 是你期望的值
- 确认蓝图没有在 Tick 或其他链路里重复上传
- 确认读取的是最新一次上传后的验证结果

## 说明

这条路径是验证路径，不是最终运行时热路径。

`Readback` 在这里的作用是帮助我们确认 GPU 真的读取并处理了结构化实例数据；等后续进入正式的 GPU-driven 渲染阶段，应尽量避免频繁做每帧 readback。
