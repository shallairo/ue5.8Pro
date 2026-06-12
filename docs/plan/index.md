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

下一阶段计划：在 indirect draw 之前加入最小 GPU frustum culling，输出可见实例列表，并让 indirect draw 只绘制可见实例。

## 下一步建议

优先执行 `GPU Frustum Culling MVP`，先验证“GPU 决定哪些实例可见，再驱动 indirect draw”，之后再考虑更真实的实例来源或 UE 场景渲染路径接入。
