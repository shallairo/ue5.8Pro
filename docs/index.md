# 项目文档索引

本目录是当前项目文档的统一入口。

## 命名规则

所有文档文件名使用小写 `kebab-case`。

### 开发计划

开发计划统一放在：

```text
docs/plan/
```

命名规则：

```text
plan-YYYY-MM-DD-topic.md
```

示例：

- `docs/plan/plan-2026-06-03-gpu-driven-execution.md`
- `docs/plan/plan-2026-06-03-gpu-instance-data-path.md`
- `docs/plan/plan-2026-06-10-indirect-draw-mvp.md`

### 学习日志

学习日志统一放在：

```text
docs/learning/
```

命名规则：

```text
YYYY-MM-DD-HHMM-topic.md
```

学习日志需要结合源码讲解，至少说明：

- 对应的源码文件
- 关键结构体、函数或 shader
- 关键变量和它们的职责
- 这次任务背后的核心知识点

### 其他文档

- `guide-topic.md`：配置或操作指南
- `test/test-topic.md`：可重复执行的测试流程
- `report-YYYY-MM-DD-topic.md`：阶段结果和基线报告
- `note-topic.md`：轻量技术笔记
- `archive-topic.md`：历史归档材料

避免使用空格、下划线、大小写混用，以及 `Next_Phase`、`Final_Plan` 这类模糊命名。

## 当前文档

- [plan/plan-2026-06-03-gpu-driven-execution.md](plan/plan-2026-06-03-gpu-driven-execution.md)：GPU-driven 渲染总体执行计划
- [plan/plan-2026-06-03-demo-cleanup-and-baseline.md](plan/plan-2026-06-03-demo-cleanup-and-baseline.md)：Demo 规范化与基线整理计划
- [plan/plan-2026-06-03-gpu-instance-data-path.md](plan/plan-2026-06-03-gpu-instance-data-path.md)：GPU 实例数据路径计划与完成记录
- [plan/plan-2026-06-12-gpu-frustum-culling-mvp.md](plan/plan-2026-06-12-gpu-frustum-culling-mvp.md)：GPU 视锥裁剪 MVP 开发计划
- [plan/plan-2026-06-12-gpu-visible-count-and-camera-frustum.md](plan/plan-2026-06-12-gpu-visible-count-and-camera-frustum.md)：GPU 可见数量回读与真实相机视锥计划
- [guide-gpu-pass-demo-v1.md](guide-gpu-pass-demo-v1.md)：第一个 GPU pass demo 搭建指南
- [test/test-compute-shader-validation.md](test/test-compute-shader-validation.md)：渐变 RenderTarget pass 测试流程
- [test/test-gpu-instance-data-path.md](test/test-gpu-instance-data-path.md)：GPU 实例数据路径测试流程
- [test/test-indirect-draw-mvp.md](test/test-indirect-draw-mvp.md)：Indirect Draw MVP 测试流程
- [test/test-gpu-frustum-culling-mvp.md](test/test-gpu-frustum-culling-mvp.md)：GPU 视锥裁剪 indirect draw 测试流程
- [test/test-gpu-visible-count-and-camera-frustum.md](test/test-gpu-visible-count-and-camera-frustum.md)：GPU 可见数量回读与真实相机视锥测试流程
- [guide-mcp-configuration.md](guide-mcp-configuration.md)：Unreal MCP 配置指南
- [report-2026-06-03-compute-pass-baseline.md](report-2026-06-03-compute-pass-baseline.md)：compute pass 基线结果
- [learning/index.md](learning/index.md)：学习日志索引

## 文档策略

文档要和仓库当前状态保持一致。

如果实现已经超过某份计划的描述，要更新计划或补充完成记录，而不是让过期内容继续留在主文档里。

每次完成有学习价值的开发任务后，都要补一段讲解；必要时在 `docs/learning/` 下新增学习日志。

`docs/learning/` 下的文档不能只写概念，必须结合本仓库源代码来讲。
