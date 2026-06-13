# GPU-Driven 渲染执行计划

## 当前状态

项目是 UE5.8 源码版 C++ 项目，核心开发集中在 `GPUDrivenPipeline` 运行时插件。

当前已经完成：

- 插件启动时注册 shader 目录映射。
- `SimpleComputeShader.usf` 可以写入 UAV RenderTarget。
- `ExecuteSimpleComputeShader()` 可以从蓝图触发 compute shader。
- Demo 关卡可以显示渐变输出。
- 已记录 compute pass baseline。
- GPU 实例数据路径已经完成并验证通过。
- `Indirect Draw MVP` 已经完成并验证通过。
- `GPU Frustum Culling MVP` 已经完成并验证通过。

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

状态：已完成。

目标：

- 定义实例数据结构。
- 在 CPU 侧生成测试实例数据。
- 上传到 GPU structured buffer。
- 使用 compute shader 读取实例数据。
- 通过 summary buffer 和 readback 验证结果。

当前计划：

- [plan-2026-06-03-gpu-instance-data-path.md](plan-2026-06-03-gpu-instance-data-path.md)

## 阶段 4：Indirect Draw MVP

状态：已完成。

完成结果：

- GPU 实例数据结构稳定。
- structured buffer 上传路径可用。
- compute shader 可以验证读取结果。
- 有明确测试文档和学习日志。

已实现内容：

- 准备 indirect draw arguments。
- 构建最小 indirect draw 路径。
- 渲染一批简单实例。
- 将结果输出到 `RT_GPUComputeOutput` 并完成可视验证。

对应文档：

- [plan-2026-06-04-indirect-draw-mvp.md](plan-2026-06-04-indirect-draw-mvp.md)
- [test-indirect-draw-mvp.md](../test/test-indirect-draw-mvp.md)
- [2026-06-05-0830-indirect-draw-mvp.md](../learning/2026-06-05-0830-indirect-draw-mvp.md)

## 阶段 5：GPU Frustum Culling MVP

状态：已完成。

目标：

- 在 GPU 侧完成最小视锥裁剪。
- 输出可见实例列表。
- 让 indirect draw 只消费可见实例。
- 验证固定测试 frustum plane 可以裁掉外圈实例，并让 indirect draw 只绘制可见实例。

对应文档：

- [plan-2026-06-12-gpu-frustum-culling-mvp.md](plan-2026-06-12-gpu-frustum-culling-mvp.md)
- [test-gpu-frustum-culling-mvp.md](../test/test-gpu-frustum-culling-mvp.md)
- [2026-06-12-0000-gpu-frustum-culling-mvp.md](../learning/2026-06-12-0000-gpu-frustum-culling-mvp.md)

## 阶段 6：GPU Visible Count 与 Camera Frustum

状态：已实现，待 UE 验证。

目标：

- 增加 GPU visible count readback，获得严格的 GPU 端可见数量验证。
- 将固定测试 frustum plane 替换为真实相机视锥 plane。
- 证明相机位置或朝向变化会影响 GPU culling 和最终 indirect draw。
- 保持当前 RenderTarget demo 路径，不接入 StaticMesh / Nanite。

当前计划：

- [plan-2026-06-12-gpu-visible-count-and-camera-frustum.md](plan-2026-06-12-gpu-visible-count-and-camera-frustum.md)

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
