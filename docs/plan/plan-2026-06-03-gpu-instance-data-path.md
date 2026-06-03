# GPU Instance Data Path 开发计划

## 当前状态

项目当前已经完成第一条可运行的 GPU compute pass demo：

- `SimpleComputeShader.usf` 可以写入 `RT_GPUComputeOutput`。
- `BP_GPUComputePassDemo` 可以在 PIE 中触发 compute pass。
- `L_GPUComputePassDemo` 已经具备可见的红绿渐变输出。
- 已完成第一份基线报告：`docs/report-2026-06-03-compute-pass-baseline.md`。
- 已完成对应学习日志：`docs/learning/2026-06-03-2007-compute-pass-baseline.md`。

当前 demo 的价值是验证了 compute dispatch 和 UAV 写入能力。下一步需要把项目从“单个 shader 写 Render Target”推进到“GPU 可以接收、保存并处理一批实例数据”。

## 目标

本阶段目标是建立第一条 GPU-visible instance data path。

这条路径需要完成：

- 在 C++ 中定义可复用的实例元数据结构。
- 在渲染线程创建 GPU structured buffer。
- 将 CPU 侧实例数组上传到 GPU buffer。
- 通过一个最小 compute shader 验证 GPU 能读写这批实例数据。
- 为后续 indirect draw 和 GPU culling 预留清晰接口。

本阶段不是为了立刻渲染成千上万个实例，而是为后续真正的 GPU-driven pipeline 打地基。

## 成功标准

本阶段完成时应满足：

- 插件中存在明确的 instance data 类型和 buffer 管理入口。
- 可以从蓝图或测试入口提交一批实例数量，例如 `1024` 或 `4096`。
- 渲染线程能创建并上传 structured buffer。
- compute shader 能读取实例数据，并写出一个可验证结果。
- Output Log 能报告实例数量、buffer 字节数、dispatch group count 和 CPU dispatch timing。
- 新增代码与当前渐变 RT pass 解耦，不把验证 shader、实例 buffer、未来 indirect draw 混成一个函数。
- 完成后补一篇中文学习日志。

## 设计原则

本阶段坚持小步验证：

- 先验证数据路径，再考虑绘制路径。
- 先验证 buffer 创建和上传，再考虑 culling。
- 先使用固定数量和固定数据，再引入场景 actor 扫描。
- 先用日志或小型 readback 验证，再进入 indirect draw。

这样可以避免在 indirect draw、instance culling、UE mesh rendering integration 三个复杂问题之间同时迷路。

## 数据结构草案

建议先定义一个紧凑但可扩展的实例结构：

```cpp
struct FGPUDrivenInstanceData
{
    FVector3f Position;
    float Radius;
    FVector3f Scale;
    uint32 Flags;
};
```

字段含义：

- `Position`：实例中心点，后续可用于 culling 和调试可视化。
- `Radius`：包围球半径，后续用于 frustum culling。
- `Scale`：实例缩放或简化 transform 信息。
- `Flags`：预留标记位，例如可见性、LOD、调试状态。

暂时不直接上传完整 `FMatrix`，因为第一阶段只需要验证数据链路，完整 transform 可以留到 indirect draw MVP 阶段再扩展。

## 阶段 1：代码结构整理

### 任务

- 新建 `Public/GPUData/` 和 `Private/GPUData/` 目录。
- 新增实例数据头文件。
- 新增一个轻量的 buffer 上传接口。
- 保持现有 `ComputeShaderInterface` 的渐变 pass 可用。

### 建议文件

- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/GPUDrivenInstanceData.h`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/GPUDrivenInstanceBufferInterface.h`
- `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/GPUDrivenInstanceBufferInterface.cpp`

### 验收

- 代码结构清晰，instance data 与现有 gradient pass 分离。
- 接口命名能表达这是数据路径验证，不是最终 renderer。

## 阶段 2：CPU 侧实例数据生成

### 任务

- 提供一个蓝图可调用函数，例如 `UploadTestInstanceData(int32 InstanceCount)`。
- 在 C++ 中生成固定 pattern 的实例数据。
- 默认实例数量可从 `1024` 开始。
- 对非法数量做保护，例如小于等于 0 或超过上限时打印 warning。

### 建议规则

- 实例位置按网格分布，方便后续扩展成可视化实例场景。
- 半径先固定为 `50.0f`。
- `Flags` 初始为 `0`。

### 验收

- 调用函数后 Output Log 能打印生成的实例数量。
- 数据生成逻辑不依赖关卡中的具体 actor。

## 阶段 3：GPU Structured Buffer 上传

### 任务

- 在 render thread 中创建 structured buffer。
- 使用 UE RHI 工具上传 CPU 数组。
- 创建 SRV，供 compute shader 读取。
- 记录 buffer 字节数和元素数量。

### 建议实现方向

- 使用 `UE::RHIResourceUtils::CreateBufferFromArray` 或 UE5.8 中相近的 RHI 工具。
- Buffer usage 至少包含 `StructuredBuffer` 和 `ShaderResource`。
- 生命周期先保持在一次 dispatch 内，后续再扩展成持久 buffer 管理对象。

### 验收

- 输出日志包含 instance count 和 buffer size。
- 多次调用不会崩溃。
- 不引入持久资源泄漏风险。

## 阶段 4：实例数据验证 Compute Shader

### 任务

- 新增一个 compute shader，用于读取 instance buffer。
- 写出一个简单验证结果。
- 首选写入一个小型 readback buffer 或 summary buffer。
- 如果 readback 复杂度过高，可先写日志级别的 dispatch 验证，并在下一阶段补 readback。

### 建议 Shader

- `Shaders/InstanceDataValidation.usf`

建议行为：

- 每个线程读取一个实例。
- 根据 `Flags` 或 `Radius` 做一个简单统计。
- 写出总实例数或有效实例数。

### 验收

- compute shader 能成功 dispatch。
- dispatch group count 与实例数量匹配。
- 结果可以通过日志或 readback 验证。

## 阶段 5：测试与文档

### 任务

- 在现有 demo Blueprint 中增加一次 instance data path 测试调用，或新建 `BP_GPUInstanceDataTest`。
- 记录 Output Log 中的关键行。
- 新增中文测试说明文档。
- 新增中文学习日志。

### 建议新增文档

- `docs/test-gpu-instance-data-path.md`
- `docs/learning/YYYY-MM-DD-HHMM-gpu-instance-data-path.md`

### 验收

- 文档能指导用户在 UE Editor 中重复触发测试。
- 学习日志解释 structured buffer、SRV、CPU-to-GPU upload、readback 或 summary buffer 的区别。

## 非目标

本阶段不做：

- 不实现 indirect draw。
- 不实现 GPU culling。
- 不接入 UE StaticMesh 渲染路径。
- 不扫描关卡 actor 自动生成实例。
- 不做 Hi-Z 或 occlusion culling。
- 不做 Tick 每帧上传。
- 不从助手侧编译项目；需要编译时通知用户执行。

## 风险与处理

### 风险：直接进入 indirect draw 导致范围过大

处理方式：本阶段只证明 GPU 能接收和处理实例数据。

### 风险：readback 引入 GPU 同步卡顿

处理方式：如果需要 readback，只用于测试路径，不进入未来 runtime 热路径。

### 风险：buffer 生命周期过早抽象

处理方式：先做一次性 dispatch 生命周期，确认 API 和数据布局后再升级成持久资源管理。

### 风险：实例结构后续不够用

处理方式：先保留 `Flags` 和基础 bounds 信息，完整 transform、mesh id、material id 放到 indirect draw MVP 阶段扩展。

## 推荐执行顺序

1. 新增中文计划并确认范围。
2. 新增 instance data 结构和上传接口。
3. 新增 CPU 测试数据生成函数。
4. 新增 structured buffer 上传逻辑。
5. 新增验证 compute shader。
6. 在 UE Editor 中编译并运行测试。
7. 根据日志生成测试记录和学习日志。

## 进入下一阶段的条件

当本阶段完成后，下一份计划应进入：

```text
docs/plan/plan-YYYY-MM-DD-indirect-draw-mvp.md
```

进入 indirect draw MVP 前必须已经具备：

- 可复用实例数据结构。
- 可上传的 GPU structured buffer。
- 可验证的 compute shader 读取路径。
- 清楚的日志和测试流程。

这样 indirect draw 阶段就可以专注于 draw arguments 和渲染提交，而不是同时补数据路径。
