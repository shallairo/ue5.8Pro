# Demo 清理与基线采集计划

## 当前状态

第一个 GPU pass demo 已经可以运行：

- `BP_GPUComputePassDemo` 在 `BeginPlay` 调用 compute pass。
- `RT_GPUComputeOutput` 接收 shader 输出。
- `M_GPUComputeOutput` 把 RenderTarget 显示到平面上。
- PIE 中可以看到渐变结果。
- Output Log 能看到 `SimpleComputeShader` dispatch 日志。

## 目标

把原型 demo 整理成一个稳定、可重复、可讲解的验证场景，并采集第一份 baseline 数据。

## 已完成事项

- 通过 UE Content Browser 把 `Martirals` 重命名为 `Materials`。
- 通过 UE Content Browser 把 `testMap.umap` 重命名为 `L_GPUComputePassDemo.umap`。
- 修复 redirectors。
- 确认 RenderTarget 开启 UAV 支持。
- 完成 baseline 报告。
- 完成对应学习日志。

## 基线报告

报告位置：

```text
docs/report-2026-06-03-compute-pass-baseline.md
```

记录内容：

- RenderTarget 尺寸。
- dispatch group 数量。
- CPU 侧 dispatch timing。
- `stat unit`、`stat gpu`、`stat scenerendering` 的编辑器模式参考数据。

## 学习日志

学习日志位置：

```text
docs/learning/2026-06-03-2007-compute-pass-baseline.md
```

讲解内容：

- RenderTarget 和 UAV 的关系。
- 为什么 UE 资产重命名应通过 Content Browser。
- baseline 的意义。
- CPU dispatch time 和 GPU elapsed time 的区别。

## 后续衔接

本阶段完成后，进入 GPU 实例数据路径阶段：

```text
docs/plan/plan-2026-06-03-gpu-instance-data-path.md
```

下一阶段重点不再是显示渐变，而是验证 GPU 能否读取一批结构化实例数据。
