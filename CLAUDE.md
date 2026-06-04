# CLAUDE.md

本文件用于约束 Claude Code 或其他 AI 编程助手在本仓库中的工作方式。

## 项目概览

这是一个 UE5.8 源码版 C++ 项目，目标是逐步构建 GPU-driven rendering 原型。主游戏模块 `Source/pro/` 保持最小状态，主要开发集中在 `Plugins/GPUDrivenPipeline/`。

## 编译约定

如果需要编译，助手只通知用户，由用户执行编译。不要直接运行 UBT 或构建脚本。

常用目标：

- `proEditor Win64 Development`
- Visual Studio 中的 `Development Editor | Win64`

## 插件结构

核心插件：

```text
Plugins/GPUDrivenPipeline/
```

重要目录：

- `Shaders/`：插件 HLSL / USF shader。
- `Source/GPUDrivenPipeline/Public/`：公开 C++ 接口。
- `Source/GPUDrivenPipeline/Private/`：插件实现。

当前插件能力：

- 启动时注册 shader 虚拟路径 `/Plugin/GPUDrivenPipeline`。
- 通过 `SimpleComputeShader.usf` 写入 RenderTarget 渐变。
- 通过蓝图函数触发 compute shader。
- 通过 GPU instance data path 验证 structured buffer 上传和读取。

## 文档约定

所有项目文档必须使用中文。

文档入口：

```text
docs/index.md
```

开发计划放在：

```text
docs/plan/
```

学习日志放在：

```text
docs/learning/
```

学习日志必须结合源码讲解，不能只写概念总结。

## 协作方式

助手完成开发任务后，需要同时给出教学式总结，说明：

- 本次改动做了什么。
- 为什么要这么做。
- 关联了哪些源码文件。
- 涉及哪些 UE、RHI、shader 或 GPU-driven 概念。
- 后续最容易踩坑的地方。

## 禁止事项

- 不修改 `pcgDoc/`，除非用户明确要求。
- 不提交生成目录。
- 不直接编译项目。
- 不把英文文档或乱码文档作为最终项目文档保留。
