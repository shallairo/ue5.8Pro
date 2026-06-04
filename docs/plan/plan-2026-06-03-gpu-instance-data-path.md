# GPU 实例数据路径开发计划

## 当前状态

本阶段已经完成，并且已经在 UE 编辑器中完成一次真实验证。

当前项目已经具备两条最小 GPU 路径：

- `SimpleComputeShader.usf` 可以写入 `RT_GPUComputeOutput`
- `BP_GPUComputePassDemo` 可以在 `BeginPlay` 触发渐变输出
- `L_GPUComputePassDemo` 可以显示 RenderTarget 结果
- `FGPUDrivenInstanceData`、`UGPUDrivenInstanceBufferInterface` 和 `InstanceDataValidation.usf` 已经接通
- 蓝图可以上传实例数据并异步读取 GPU 验证结果

本阶段的完成，意味着项目已经从“GPU 能写一张纹理”推进到“GPU 能读取一批结构化实例数据并返回摘要结果”。

## 目标

建立第一条可验证的 GPU-visible instance data path，用来证明下面这件事：

```text
CPU 可以生成实例数据 -> 上传到 GPU structured buffer -> GPU compute shader 读取并处理 -> CPU 通过 readback 验证结果
```

这一步不是为了直接开始大规模绘制，而是为后续的 `indirect draw`、`GPU culling` 和实例调度建立数据基础。

## 已完成内容

### 1. 实例数据结构

已新增：

- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/GPUDrivenInstanceData.h`

当前结构：

```cpp
struct FGPUDrivenInstanceData
{
    FVector3f Position;
    float Radius;
    FVector3f Scale;
    uint32 Flags;
};
```

字段职责：

- `Position`：实例位置
- `Radius`：包围球半径，后续可用于 culling
- `Scale`：缩放信息
- `Flags`：预留状态位，用于验证和后续扩展

### 2. 蓝图测试入口

已新增：

- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/GPUDrivenInstanceBufferInterface.h`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/GPUDrivenInstanceBufferInterface.cpp`

当前可用蓝图节点：

- `Upload Test Instance Data`
- `Get Last Instance Data Validation Result`

### 3. GPU structured buffer 上传

当前实现会：

- 在 CPU 侧生成固定 pattern 的测试实例数据
- 使用 `UE::RHIResourceUtils::CreateBufferFromArray` 创建并上传 structured buffer
- 创建 `SRV` 供 compute shader 读取
- 创建 summary `UAV` 供 compute shader 写回统计结果

### 4. 实例数据验证 shader

已新增：

- `Plugins/GPUDrivenPipeline/Shaders/InstanceDataValidation.usf`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/InstanceDataValidationShader.h`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/InstanceDataValidationShader.cpp`

当前 shader 会统计：

- `ProcessedCount`
- `ValidRadiusCount`
- `FlagSum`
- `PositionChecksum`

### 5. Readback 验证路径

当前实现使用 `FRHIGPUBufferReadback` 把很小的一段 summary 数据读回 CPU，并通过蓝图节点供关卡验证。

## 实际验证结果

用户已经在 UE 中完成验证，Output Log 结果如下：

```text
GPUDrivenPipeline: Uploaded 1024 instance records (32768 bytes) and dispatched InstanceDataValidation (groups=16,1,1, cpu-dispatch=0.009 ms).
GPUDrivenPipeline: Instance validation readback ready (processed=1024, valid-radius=1024, flag-sum=1536, checksum=1587200).
```

这说明：

- 成功上传了 `1024` 条实例数据
- 每条实例大小为 `32` 字节，总计 `32768` 字节
- GPU 端实际处理了全部 `1024` 条记录
- 所有实例的 `Radius` 都是有效的
- `Flags` 的累加结果与 `0, 1, 2, 3` 循环模式一致
- `PositionChecksum` 非零，说明位置字段也被 GPU 正确读取

## 成功标准完成情况

- [x] 存在清晰的 instance data 结构
- [x] 蓝图可提交实例数量
- [x] 渲染线程可创建并上传 structured buffer
- [x] compute shader 可读取实例数据
- [x] Output Log 可输出实例数量、字节数、dispatch group 和 CPU dispatch 时间
- [x] 已有中文测试文档
- [x] 已有结合源码的学习日志

## 非目标

本阶段没有做这些内容：

- indirect draw
- GPU culling
- StaticMesh 渲染接入
- 自动扫描关卡 Actor 生成实例
- Hi-Z 或 occlusion culling
- Tick 每帧上传实例数据

## 下一步建议

下一阶段建议进入：

```text
docs/plan/plan-YYYY-MM-DD-indirect-draw-mvp.md
```

进入前已经具备的数据基础：

- 可复用的实例数据结构
- 可上传的 GPU structured buffer
- 可验证的 compute shader 读取路径
- 明确的蓝图测试流程
- 可复盘的日志、测试文档和学习日志
