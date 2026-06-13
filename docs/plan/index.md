# 开发计划索引

本目录存放项目主线开发计划。

## 命名规则

统一使用：

```text
plan-YYYY-MM-DD-topic.md
```

例如：

- `plan-2026-06-03-gpu-driven-execution.md`
- `plan-2026-06-03-demo-cleanup-and-baseline.md`
- `plan-2026-06-03-gpu-instance-data-path.md`
- `plan-2026-06-04-indirect-draw-mvp.md`
- `plan-2026-06-12-gpu-frustum-culling-mvp.md`
- `plan-2026-06-12-gpu-visible-count-and-camera-frustum.md`

## 当前计划

### 1. GPU-driven 总体执行计划

- [plan-2026-06-03-gpu-driven-execution.md](/D:/UGit/ue5.8Pro/docs/plan/plan-2026-06-03-gpu-driven-execution.md)

说明：

定义项目整体 GPU-driven 路线图，用来约束阶段边界。

### 2. Demo 规范化与基线整理

- [plan-2026-06-03-demo-cleanup-and-baseline.md](/D:/UGit/ue5.8Pro/docs/plan/plan-2026-06-03-demo-cleanup-and-baseline.md)

说明：

用于整理 GPU pass demo 资产命名、测试地图和性能基线。

### 3. GPU 实例数据路径

- [plan-2026-06-03-gpu-instance-data-path.md](/D:/UGit/ue5.8Pro/docs/plan/plan-2026-06-03-gpu-instance-data-path.md)

说明：

本阶段已经完成，当前文档同时承担计划记录和完成总结的作用。

### 4. Indirect Draw MVP

- [plan-2026-06-04-indirect-draw-mvp.md](/D:/UGit/ue5.8Pro/docs/plan/plan-2026-06-04-indirect-draw-mvp.md)

说明：

本阶段已完成，并已在 UE 中验证通过：GPU 可以写出 indirect args，并驱动最小可见实例绘制到 `RT_GPUComputeOutput`。

### 5. GPU Frustum Culling MVP

- [plan-2026-06-12-gpu-frustum-culling-mvp.md](/D:/ue/ue5.8Pro/docs/plan/plan-2026-06-12-gpu-frustum-culling-mvp.md)

说明：

本阶段已完成并通过 UE 日志验证：GPU 可以输出可见实例列表和 indirect args，并让 draw 只绘制可见实例。

### 6. GPU Visible Count 与 Camera Frustum

- [plan-2026-06-12-gpu-visible-count-and-camera-frustum.md](/D:/ue/ue5.8Pro/docs/plan/plan-2026-06-12-gpu-visible-count-and-camera-frustum.md)

说明：

下一阶段计划：先增加 GPU visible count readback，严格验证 GPU 裁剪数量；再把固定测试 frustum plane 替换为真实相机视锥参数。

## 下一步建议

优先执行 `GPU Visible Count 与 Camera Frustum`，先把验证闭环做严谨，再让裁剪结果跟随真实相机变化。之后再考虑并行 culling 或真实场景实例来源。
