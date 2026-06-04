# GPU-Driven 渲染执行计划

## 当前状态

项目是 UE5.8 源码版 C++ 项目，核心开发集中在 `GPUDrivenPipeline` 运行时插件。

当前已经完成：

- 插件启动时注册 shader 目录映射。
- `SimpleComputeShader.usf` 可以写入 UAV RenderTarget。
- `ExecuteSimpleComputeShader()` 可以从蓝图触发 compute shader。
- Demo 关卡可以显示渐变输出。
- 已记录 compute pass baseline。
- 已开始实现 GPU 实例数据路径。

## 总目标

把项目从“单个 compute shader 写 RenderTarget”推进到“GPU 可以接收、处理并最终驱动实例渲染”。

## 阶段 1：Compute Pass 可视化验证

状态：已完成。

目标：

- 蓝图能触发 compute shader。
- shader 能写入 `RT_GPUComputeOutput`。
- 材质能显示 RenderTarget。
- PIE 中可见稳定渐变。

产物：

- `Content/GPUDrivenDemo/Maps/L_GPUComputePassDemo`
- `RT_GPUComputeOutput`
- `M_GPUComputeOutput`
- `BP_GPUComputePassDemo`

## 阶段 2：Demo 清理和基线采集

状态：已完成。

目标：

- 修正资产命名。
- 移除或减少屏幕刷屏消息。
- 记录 `stat unit`、`stat gpu`、`stat scenerendering`。
- 形成第一份 baseline report。

产物：

- `docs/report-2026-06-03-compute-pass-baseline.md`
- `docs/learning/2026-06-03-2007-compute-pass-baseline.md`

## 阶段 3：GPU 实例数据路径

状态：进行中。

目标：

- 定义实例数据结构。
- 在 CPU 侧生成测试实例数据。
- 上传到 GPU structured buffer。
- 使用 compute shader 读取实例数据。
- 通过 summary buffer 和 readback 验证结果。

当前计划：

- [plan-2026-06-03-gpu-instance-data-path.md](plan-2026-06-03-gpu-instance-data-path.md)

## 阶段 4：Indirect Draw MVP

状态：未开始。

进入条件：

- GPU 实例数据结构稳定。
- structured buffer 上传路径可用。
- compute shader 可以验证读取结果。
- 有明确测试文档和学习日志。

目标：

- 准备 indirect draw arguments。
- 构建最小 indirect draw 路径。
- 渲染一批简单实例。
- 与 baseline 做对比。

## 非目标

当前阶段不做：

- 完整 Hi-Z occlusion culling。
- 完整 GPU LOD selection。
- 生产级 renderer integration。
- 大规模美术场景。

## 文档规则

后续计划必须放在 `docs/plan/`，并使用中文撰写。

命名规则：

```text
plan-YYYY-MM-DD-topic.md
```
