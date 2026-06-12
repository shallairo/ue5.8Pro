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

结合 `FGPUDrivenInstanceData`、structured buffer、`InstanceDataValidation.usf` 和 readback 讲解"CPU 上传实例数据，GPU 读取并验证"的完整路径。

### 4. UE CPU-GPU 交互：RHI 层与渲染线程

- [2026-06-04-1200-ue-cpu-gpu-interaction-rhi-and-render-thread.md](/D:/UGit/ue5.8Pro/docs/learning/2026-06-04-1200-ue-cpu-gpu-interaction-rhi-and-render-thread.md)

内容：

结合项目源码讲解 UE 的三层架构：游戏线程、渲染线程和 RHI 层。覆盖 ENQUEUE_RENDER_COMMAND、资源状态转换、SRV/UAV、Readback 等核心概念。

### 5. FRHICommandListImmediate 与 FlushRenderingCommands 源码详解

- [2026-06-04-1215-rhi-command-list-and-flush-rendering-commands.md](/D:/UGit/ue5.8Pro/docs/learning/2026-06-04-1215-rhi-command-list-and-flush-rendering-commands.md)

内容：

深入引擎源码讲解 FRHICommandListImmediate 的类继承、命令存储机制、Submit 流程，以及 FlushRenderingCommands 的实现原理和同步机制。

### 6. Indirect Draw MVP

- [2026-06-05-0830-indirect-draw-mvp.md](/D:/ue/ue5.8Pro/docs/learning/2026-06-05-0830-indirect-draw-mvp.md)

内容：

结合 `GPUDrivenIndirectDrawInterface`、`IndirectDrawShaders` 和 `IndirectDrawInstances.usf` 讲解“GPU 写 indirect args，并驱动最小实例绘制”的完整路径。

### 7. GPU Frustum Culling MVP

- [2026-06-12-0000-gpu-frustum-culling-mvp.md](/D:/ue/ue5.8Pro/docs/learning/2026-06-12-0000-gpu-frustum-culling-mvp.md)

内容：

结合 `FrustumCullInstances.usf`、`FFrustumCullInstancesShader` 和新的蓝图入口讲解“GPU 做最小视锥裁剪，并用可见实例驱动 indirect draw”的路径。
