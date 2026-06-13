# 学习日志索引

本目录存放结合本仓库源码的 UE 渲染学习日志，按开发阶段先后排列。

## 约定

- **学习日志命名规则：** `YYYY-MM-DD-HHMM-topic.md`
- **每篇日志结构：** 源码总览表 → 逐行执行链路追踪 → 完整数据流程图 → 补充知识点 → 源码索引表 → 验证步骤 → 常见问题排查

**阅读建议：** 按编号顺序阅读。每个阶段都建立在上一个阶段的知识基础上。配置好两个编辑器分屏：一边是本项目代码，一边是学习文档，逐行对照阅读效果最佳。

---

## 阶段 1：Compute Pass 基础

### 1. GPU Pass UAV 崩溃排查

- [2026-06-03-1705-gpu-pass-demo-uav-crash.md](2026-06-03-1705-gpu-pass-demo-uav-crash.md)

**配套源码：** `ComputeShaderInterface.cpp/h`、`SimpleComputeShader.h/cpp/usf`、`GPUDrivenPipelineModule.cpp`

**内容：**
- 首次 compute shader dispatch 因 `bSupportsUAV = false` 导致 `D3D Device Removed` 的根因分析
- **行级流程追踪：** 从 `AddShaderSourceDirectoryMapping` → `IMPLEMENT_GLOBAL_SHADER` → 蓝图触发 → ENQUEUE_RENDER_COMMAND → Transition → Dispatch → Shader 执行的每行代码链路
- **补充知识点：** D3D12 资源状态与 `ERHIAccess` 映射表、SRV vs UAV 对比、`FGlobalShader` 生命周期、`GMaxRHIFeatureLevel` 含义

### 2. Compute Pass 基线采集

- [2026-06-03-2007-compute-pass-baseline.md](2026-06-03-2007-compute-pass-baseline.md)

**配套源码：** `ComputeShaderInterface.cpp`、`SimpleComputeShader.usf`

**内容：**
- 第一个 compute pass 的基线数据采集（GroupCount 计算、dispatch 耗时等）
- **行级流程追踪：** 参数校验 → 线程组计算 → 渲染线程执行 → Shader 边界保护 → 像素写入
- **补充知识点：** `SV_DispatchThreadID` vs `SV_GroupThreadID` vs `SV_GroupID` 区别、CPU Dispatch Time vs GPU Execution Time、`FComputeShaderUtils::Dispatch` 内部机制

---

## 阶段 2：CPU-GPU 交互原理

### 3. UE CPU-GPU 交互：三层架构与渲染管线

- [2026-06-04-1200-ue-cpu-gpu-interaction-rhi-and-render-thread.md](2026-06-04-1200-ue-cpu-gpu-interaction-rhi-and-render-thread.md)

**配套源码：** 本项目全部 C++ 文件 + 引擎 `RenderingThread.h` `RHICommandList.h`

**内容：**
- **三层架构详解：** 游戏线程 → 渲染线程 → RHI 层 → GPU 的职责边界和交互方式
- **行级项目源码索引：** `ENQUEUE_RENDER_COMMAND` 宏展开全过程、`FRenderCommandDispatcher::Enqueue` 实现
- **三层交互时序图：** 以 IndirectDraw MVP 流程为例展示线程间协作
- **补充知识点：** 多线程渲染的性能动机、ERHIAccess 状态转换完整语义、死锁场景分析、FGlobalShader 线程安全性、RHI 命令缓存机制

### 4. FRHICommandListImmediate 与 FlushRenderingCommands 源码详解

- [2026-06-04-1215-rhi-command-list-and-flush-rendering-commands.md](2026-06-04-1215-rhi-command-list-and-flush-rendering-commands.md)

**配套源码：** 引擎 `RHICommandList.h/cpp`、`RenderingThread.h/cpp`

**内容：**
- **类继承链：** `FRHICommandListBase` → `FRHIComputeCommandList` → `FRHICommandList` → `FRHICommandListImmediate` 每层新增能力
- **命令存储机制：** `FMemStackBase` 内存池 + 单向链表的命令录制过程
- **Submit 与 ImmediateFlush 源码解析：** `EImmediateFlushType` 各枚举的递进行为
- **FlushRenderingCommands 完整执行流程：** 从 StopRecording → 推送 FlushCmd → FFrameEndSync::Sync 的栅栏同步
- **本项目中 Flush 与不 Flush 的场景对比**

---

## 阶段 3：实例数据路径

### 5. GPU 实例数据路径

- [2026-06-04-0000-gpu-instance-data-path.md](2026-06-04-0000-gpu-instance-data-path.md)

**配套源码：** `GPUDrivenInstanceData.h`、`GPUDrivenInstanceBufferInterface.h/cpp`、`InstanceDataValidationShader.h/cpp`、`InstanceDataValidation.usf`

**内容：**
- **行级执行链路追踪：**
  - Part A: C++ 与 HLSL 结构体对齐（FGPUDrivenInstanceData 的 32 字节布局）
  - Part B: 确定性测试数据生成逻辑（网格排列、Flags 循环）
  - Part C: CreateBufferFromArray → SRV/UAV 创建 → Dispatch → Transition → EnqueueCopy 的完整过程
  - Part D: GPU Shader 中 InterlockedAdd 原子操作的原理
  - Part E: FRHIGPUBufferReadback 的 Lock/Memcpy/Unlock 流程和 FlushRenderingCommands
- **数据流程图：** 以全流程示意图展示数据从 CPU → GPU → CPU 的完整流转路径
- **补充知识点：** StructuredBuffer 的 D3D12 实现、SRV/UAV 视图概念、Readback 工作原理、InterlockedAdd vs CPU atomic、线程安全全局变量模式
- **验证结果数值推导：** processed=1024, valid-radius=1024, flag-sum=1536, checksum=1587200 的数学验证

---

## 阶段 4：Indirect Draw

### 6. Indirect Draw MVP

- [2026-06-05-0830-indirect-draw-mvp.md](2026-06-05-0830-indirect-draw-mvp.md)

**配套源码：** `GPUDrivenIndirectDrawInterface.h/cpp`、`IndirectDrawShaders.h/cpp`、`IndirectDrawInstances.usf`

**内容：**
- **核心概念：** Direct Draw vs Indirect Draw 对比、`FRHIDrawIndirectParameters` 的 4 字段布局（D3D12/Vulkan/UE 三端对应）
- **行级执行链路追踪：**
  - Phase 1: 游戏线程前置校验
  - Phase 2a-2e: 渲染线程中的 Buffer 创建 → Dispatch compute shader → Transition → Graphics PSO 配置 → DrawPrimitiveIndirect
- **三 shader 源码详解：** `BuildIndirectArgsCS`（写 args）、`MainVS`（SV_VertexID 程序化 quad + SV_InstanceID 索引 SRV）、`MainPS`（透传颜色）
- **资源状态转换图：** Compute Pipeline 与 Graphics Pipeline 的交叉转换
- **补充知识点：** `GEmptyVertexDeclaration` 的原理、GPU 执行时机保证（命令列表顺序）、两阶段模式（Compute → Graphics）、`SetShaderParameters` 绑定机制

---

## 阶段 5：GPU Frustum Culling

### 7. GPU Frustum Culling MVP

- [2026-06-12-0000-gpu-frustum-culling-mvp.md](2026-06-12-0000-gpu-frustum-culling-mvp.md)

**配套源码：** `FrustumCullShader.h/cpp`、`FrustumCullInstances.usf`、`GPUDrivenIndirectDrawInterface.cpp`

**内容：**
- **数学原理：** 6 个 Frustum Plane 的定义、球体-平面测试公式推导（`dot + w >= -Radius`）
- **行级执行链路追踪：**
  - 1.1-1.3: 4 个 GPU Buffer 的创建与用途对比、SRV/UAV 双视图的 VisibleInstanceBuffer、culling dispatch、4 路 Transition
  - Shader: CullInstancesCS 的串行遍历 → compact write → 同时写 3 个 Buffer
- **数据流/状态转换图：** Compute Phase → Transition → Graphics Phase 的全流程资源状态
- **补充知识点：** `[numthreads(1,1,1)]` 单线程 MVP 的取舍理由、Compact vs Non-compact 可见列表、固定测试平面参数含义（保留中间 1/4 区域）

---

## 阶段 6：Camera Frustum + Readback

### 8. GPU Visible Count 与 Camera Frustum

- [2026-06-13-1000-gpu-visible-count-and-camera-frustum.md](2026-06-13-1000-gpu-visible-count-and-camera-frustum.md)

**配套源码：** `GPUDrivenIndirectDrawInterface.cpp`（BuildCameraFrustumPlanes、MakePlaneFromPointAndNormal、三级入口函数）、`FrustumCullInstances.usf`

**内容：**
- **行级执行链路追踪：**
  - 2.1: GPU visible count readback 完整流程（Summary 写入 → EnqueueCopy → Lock → FlushRenderingCommands）
  - 2.2: Camera Frustum 平面构造的数学推导（三级摄像机参数 fallback、Near 平面四角计算、6 平面的叉积构造、法线校正）
  - 2.3: 实例空间从"固定原点"到"Camera Anchored"到"Plane Bounded"的三次演进
  - 2.4: VS 中 GridWorldMin/GridWorldExtent 支持任意世界偏移
- **完整数据流图：** 游戏线程 → 渲染线程（compute → transition → graphics）→ 异步 readback
- **补充知识点：** FOV → Near 平面半宽半高的三角学推导、AspectRatio 的三种来源对比、PlayerCameraManager vs CameraComponent 的区别

---

## 补充知识系列（独立参考）

以下两篇是和具体开发阶段无关、但理解 UE 渲染所必需的底层原理文档：

### A. UE CPU-GPU 交互：RHI 层与渲染线程

→ [2026-06-04-1200-ue-cpu-gpu-interaction-rhi-and-render-thread.md](#3-ue-cpu-gpu-交互三层架构与渲染管线)

### B. FRHICommandListImmediate 与 FlushRenderingCommands 源码详解

→ [2026-06-04-1215-rhi-command-list-and-flush-rendering-commands.md](#4-frhicommandlistimmediate-与-flushrenderingcommands-源码详解)

---

## 关联资源

- **开发计划：** 见 [docs/plan/](../plan/index.md)
- **测试规程：** 见 [docs/test/](../test/)
- **性能报告：** 见 [docs/report-2026-06-03-compute-pass-baseline.md](../report-2026-06-03-compute-pass-baseline.md)
- **项目概述：** 见 [docs/index.md](../index.md)
- **编程规范：** 根目录 CLAUDE.md
