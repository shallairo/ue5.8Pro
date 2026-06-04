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

下一阶段计划：用 GPU 写出的 indirect args 驱动一次最小可见实例绘制。

## 下一步建议

优先执行 `Indirect Draw MVP`，先验证最小可见 indirect draw，再进入 GPU culling 或 StaticMesh 渲染路径接入。
