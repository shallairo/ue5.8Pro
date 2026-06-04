# UE5.8 GPU-Driven Pipeline 原型

这是一个基于 UE5.8 源码版的 C++ 项目，用于学习和实现 GPU-driven rendering 相关技术。

当前核心开发集中在运行时插件 `GPUDrivenPipeline`。主游戏模块 `Source/pro/` 保持最小骨架，插件负责 shader 注册、compute shader 调度、RenderTarget 写入，以及后续 GPU 实例数据路径验证。

## 当前重点

- UE5.8 源码版项目。
- Windows 桌面渲染配置，使用 DX12 / SM6。
- 插件：`Plugins/GPUDrivenPipeline/`。
- 已完成第一个可视化 GPU pass demo。
- 已完成 compute pass baseline 记录。
- 当前下一阶段：GPU-visible instance data path。

## 项目结构

- `Config/`：Unreal 项目配置。
- `Content/`：UE 编辑器创建的资源。
- `Plugins/GPUDrivenPipeline/`：GPU-driven 渲染原型插件。
- `Source/pro/`：最小主游戏模块。
- `docs/`：中文项目文档、计划、测试流程、学习日志。
- `pro.uproject`：UE 项目描述文件。

## 文档入口

从 [docs/index.md](docs/index.md) 开始阅读。

开发计划统一放在：

```text
docs/plan/
```

学习日志统一放在：

```text
docs/learning/
```

所有项目文档必须使用中文。学习日志必须结合源码讲解。

## 编译说明

需要编译时，由用户在本机执行编译。助手只负责提醒，不直接运行 UBT。

常规目标：

```text
Development Editor | Win64
```

生成目录如 `Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/` 不应提交到版本控制。
