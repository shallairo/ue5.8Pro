# 学习日志索引

本目录存放结合源码的学习日志。

## 约定

学习日志命名规则：

```text
YYYY-MM-DD-HHMM-topic.md
```

每篇学习日志都应结合本仓库源代码，至少覆盖：

- 对应源码文件
- 关键结构体、函数或 shader
- 关键变量与职责
- 这次任务背后的核心知识点

## 当前学习日志

### 1. GPU pass UAV 崩溃排查

- [2026-06-03-1705-gpu-pass-demo-uav-crash.md](/D:/UGit/ue5.8Pro/docs/learning/2026-06-03-1705-gpu-pass-demo-uav-crash.md)

内容：

记录 RenderTarget 缺少 UAV 支持导致 `D3D Device Removed` 的原因，以及修正思路。

### 2. Compute pass 基线

- [2026-06-03-2007-compute-pass-baseline.md](/D:/UGit/ue5.8Pro/docs/learning/2026-06-03-2007-compute-pass-baseline.md)

内容：

整理第一阶段 GPU pass demo 的测试环境、运行观察和基线意义。

### 3. GPU 实例数据路径

- [2026-06-04-0000-gpu-instance-data-path.md](/D:/UGit/ue5.8Pro/docs/learning/2026-06-04-0000-gpu-instance-data-path.md)

内容：

结合 `FGPUDrivenInstanceData`、structured buffer、`InstanceDataValidation.usf` 和 readback 讲解“CPU 上传实例数据，GPU 读取并验证”的完整路径。
