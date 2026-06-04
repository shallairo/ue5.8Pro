# CODEBUDDY.md

本文件用于约束 AI 编程助手在本仓库中的协作方式。

## 项目概览

这是一个 UE5.8 源码版 C++ 项目，目标是逐步实现 GPU-driven rendering 原型。

主游戏模块 `Source/pro/` 保持最小骨架。当前所有主要开发都集中在：

```text
Plugins/GPUDrivenPipeline/
```

## 当前架构

- `Source/pro/`：最小主游戏模块。
- `Plugins/GPUDrivenPipeline/`：GPU-driven 渲染实验插件。
- `Plugins/GPUDrivenPipeline/Shaders/`：插件 shader 源文件目录。
- `docs/`：中文文档、计划、测试流程和学习日志。

当前插件已经具备：

- 插件启动时注册 shader 目录映射。
- `SimpleComputeShader.usf` 渐变输出 shader。
- 蓝图可调用的 compute shader 接口。
- 渲染线程 dispatch 到 UAV RenderTarget 的路径。
- GPU 实例数据 structured buffer 验证路径。

## 编译规则

主要构建系统是 Unreal Build Tool。

目标：

- `Source/pro.Target.cs`：Game target。
- `Source/proEditor.Target.cs`：Editor target。

如果任务需要编译，助手只通知用户，由用户手动编译。助手不直接执行编译命令。

## 渲染配置

项目面向桌面端渲染实验：

- Windows DX12。
- SM6 shader target。
- Lumen GI 和 Lumen reflections。
- Ray tracing。
- Virtual shadow maps。
- Substrate。

## 文档规则

所有项目文档必须使用中文，包括计划、指南、测试、报告、索引和学习日志。

文档入口：

```text
docs/index.md
```

开发计划必须放在：

```text
docs/plan/
```

计划命名规则：

```text
docs/plan/plan-YYYY-MM-DD-topic.md
```

学习日志必须放在：

```text
docs/learning/
```

学习日志命名规则：

```text
docs/learning/YYYY-MM-DD-HHMM-topic.md
```

其他文档命名：

- `guide-topic.md`：配置和操作指南。
- `test-topic.md`：可重复执行的测试流程。
- `report-YYYY-MM-DD-topic.md`：测试结果和阶段总结。
- `note-topic.md`：轻量技术笔记。
- `archive-topic.md`：历史或非当前主线材料。

禁止把新的开发计划直接放在 `docs/` 根目录，必须放入 `docs/plan/`。

## 教学规则

助手不仅是开发者，也要作为教学者协作。

每次完成有意义的开发任务后，都要向用户解释：

- 改了什么。
- 为什么这样改。
- 涉及哪些 UE、RHI、shader 或渲染概念。
- 哪些地方最容易踩坑。

当任务有学习价值时，必须在 `docs/learning/` 下新增或更新学习日志。

学习日志不能只写泛泛概念，必须结合源码讲解。至少应包含：

- 相关源码文件。
- 核心结构体、函数、变量或 shader 代码。
- 代码和知识点之间的关系。
- 后续开发时应该记住的结论。

## 开发注意事项

- 不要把旧计划当成事实，如果旧文档和当前代码冲突，以当前代码为准并更新文档。
- 优先推进小而可验证的 GPU 渲染里程碑。
- 区分 CPU dispatch timing 和真实 GPU elapsed time。
- 不提交 `Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/` 等生成目录。
- 不修改 `pcgDoc/`，除非用户明确要求。
- 修改 UE `.uasset` 或 `.umap` 路径时，优先指导用户在 UE 编辑器中操作，不直接通过文件系统硬改。
