# Compute Pass 基线采集

## 本篇定位

本文记录项目第一个 GPU compute pass demo 的基线采集工作和测试环境。基线不是为了证明性能已经很好，而是为了给后续阶段提供对照——之后加入 structured buffer、indirect draw 或 GPU culling 时，都可以和这次最小 pass 做比较。

通过本篇的学习，你应该能回答以下问题：
- 一个最小 GPU compute pass 从蓝图触发到画面显示经历了哪些步骤？
- 线程组数量（GroupCount）如何计算？
- 什么是"CPU dispatch time"？它为什么不能反映 GPU 的真实性能？

---

## 一、涉及源码总览

| 文件 | 行范围 | 职责 |
|------|--------|------|
| `ComputeShaderInterface.cpp` | 全部 | 蓝图执行入口和渲染线程调度 |
| `ComputeShaderInterface.h` | 全部 | UBlueprintFunctionLibrary 声明 |
| `SimpleComputeShader.h` | 全部 | 参数结构体（OutputTexture + TextureSize） |
| `SimpleComputeShader.cpp` | 全部 | 注册编译，设定线程组大小 8x8 |
| `SimpleComputeShader.usf` | 全部 | HLSL 实现：每个线程写一个像素 |
| `GPUDrivenPipelineModule.cpp` | L20-L31 | 注册 Shaders/ 目录映射 |

> 以上文件均在 `Plugins/GPUDrivenPipeline/` 下，路径请参照源码目录结构。

---

## 二、完整执行链路追踪（逐行）

### 2.1 蓝图层 → C++ 函数调用

```
BeginPlay → Execute Simple Compute Shader (RT_GPUComputeOutput)
```

对应 CPP：
```
ComputeShaderInterface.h:18     UFUNCTION(BlueprintCallable, ...)
ComputeShaderInterface.cpp:16   void UComputeShaderInterface::ExecuteSimpleComputeShader(...)
```

### 2.2 参数校验（L18-L57）

```
[L18-L22] 空指针检查
[L24-L30] 尺寸检查：必须 > 0
[L34-L39] bSupportsUAV 检查（来自上一篇的崩溃体验）
[L41-L49] 获取 FTextureRenderTarget2DResource 指针
[L51-L57] 获取有效的纹理尺寸 FIntPoint
```

这些校验确保 dispatch 前一切就绪。任何一项不通过都会直接 return，不会走到 GPU dispatch。

### 2.3 线程组数量计算（L85-L89）

```cpp
TShaderMapRef<FSimpleComputeShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
const FIntVector GroupCount(
    FMath::DivideAndRoundUp(TextureSize.X, 8),   // GroupCount.X
    FMath::DivideAndRoundUp(TextureSize.Y, 8),   // GroupCount.Y
    1);                                            // GroupCount.Z = 1
```

**计算原理：**
- Shader 声明 `[numthreads(8, 8, 1)]`，每个线程组有 8×8 = 64 个线程
- 纹理尺寸 1024×1024 时：`GroupCount = (128, 128, 1)`
- 总线程数 = 128 × 128 × 64 = 1,048,576 = 1024 × 1024（每个线程处理 1 个像素）
- `DivideAndRoundUp` 确保当纹理尺寸不能被 8 整除时，最后多出来的像素也有线程覆盖

### 2.4 渲染线程内执行（L61-L111）

```
┌─────────────────────────────────────────────────────────────┐
│ ENQUEUE_RENDER_COMMAND lambda ──── 渲染线程执行              │
│                                                             │
│  [L64] 获取 FTextureRHIRef (纹理的 RHI 句柄)               │
│  [L72] 获取 UAV (UnorderedAccessView)                       │
│        若无效则 return                                       │
│  [L81] 填充 PassParameters:                                  │
│         - OutputTexture = UAV                                │
│         - TextureSize = FVector2f(1024, 1024)               │
│  [L85] 获取 TShaderMapRef<FSimpleComputeShader>             │
│  [L86] 计算 GroupCount (128, 128, 1)                        │
│  [L92] Transition: ERHIAccess::SRVMask → UAVCompute         │
│  [L95] FComputeShaderUtils::Dispatch (★ 实际调度 GPU 工作) │
│  [L98] Transition: ERHIAccess::UAVCompute → SRVMask          │
│  [L94+96] FPlatformTime::Seconds() 测量 dispatch 耗时       │
│  [L100-110] 存储耗时、输出日志                               │
└─────────────────────────────────────────────────────────────┘
```

### 2.5 Shader 执行

```
SimpleComputeShader.usf:10-29
```

```hlsl
[numthreads(8, 8, 1)]
void MainCS(uint3 ThreadID : SV_DispatchThreadID)
{
    uint Width = (uint)TextureSize.x;    // [L13]
    uint Height = (uint)TextureSize.y;   // [L14]
    // [L16-19] 边界保护
    if (ThreadID.x >= Width || ThreadID.y >= Height) return;

    // [L22] UV = ThreadID / TextureSize
    float2 UV = float2(ThreadID.xy) / float2(Width, Height);
    // [L25] 渐变颜色：(R=UV.x, G=UV.y, B=0.5, A=1.0)
    float4 Color = float4(UV.x, UV.y, 0.5, 1.0);
    // [L28] 写入 UAV 纹理
    OutputTexture[ThreadID.xy] = Color;
}
```

**关键设计点：**
- 使用 `SV_DispatchThreadID` 而非 `SV_GroupThreadID`，直接获得全局像素坐标
- 边界保护是 compute shader 的标配——因为线程组数会向上取整
- 每个线程独立写一个像素，不存在写入冲突

---

## 三、数据流程图（带资源状态标注）

```
                        资源状态                                 UE 概念层
 游戏线程                                                       ┌──────────┐
    │                                                           │ Game     │
    │  UComputeShaderInterface::ExecuteSimpleComputeShader(RT)  │ Thread   │
    │    ├─ 读取 RT_GPUComputeOutput.bSupportsUAV               │          │
    │    └─ ENQUEUE_RENDER_COMMAND                              └──────────┘
    ▼
 渲染线程                                                       ┌──────────┐
    │                                                           │ Render   │
    │  lambda 开始执行                                          │ Thread   │
    │    ├─ RT.GetRenderTargetTexture() → FTextureRHIRef        │          │
    │    ├─ RT.GetUnorderedAccessViewRHI() → UAV                │          │
    │    │                                                      └──────────┘
    │    ├─ Transition( SRVMask ──→ UAVCompute )
    │    │      ^^^^^^^^          ^^^^^^^^^^
    │    │      只读模式          Compute写模式                   ┌──────────┐
    │    │                                                       │ RHI     │
    │    ├─ FComputeShaderUtils::Dispatch()                     │ Layer   │
    │    │    ├─ 绑定 UAV → OutputTexture                       │          │
    │    │    ├─ 绑定 TextureSize                               │          │
    │    │    ├─ 传入 GroupCount = (128,128,1)                  │          │
    │    │    └─ GPU 执行 SimpleComputeShader.usf               └──────────┘
    │    │                                                       ┌──────────┐
    │    │       [numthreads(8,8,1)]                             │ GPU     │
    │    │       MainCS(ThreadID)                                │          │
    │    │         ├─ 检查边界                                   │          │
    │    │         ├─ UV = ThreadID.xy / TextureSize             │          │
    │    │         └─ OutputTexture[ThreadID] = float4(UV, 0.5) │          │
    │    │                                                       └──────────┘
    │    ├─ Transition( UAVCompute ──→ SRVMask )
    │    └─ Log cpu-dispatch 耗时
    ▼
 材质系统读取 RT_GPUComputeOutput
    → M_GPUComputeOutput 采样 RT
    → 关卡平面显示红绿渐变
```

---

## 四、补充知识点

### 4.1 线程组（Thread Group）与线程 ID 的区别

| HLSL 语义 | 含义 | 范围 |
|-----------|------|------|
| `SV_GroupThreadID` | 线程在**当前线程组内**的索引 | `(0..7, 0..7, 0..0)` |
| `SV_GroupID` | 当前线程组在所有线程组中的索引 | `(0..127, 0..127, 0..0)` |
| `SV_DispatchThreadID` | 线程的**全局**唯一索引 | `GroupID × ThreadGroupSize + GroupThreadID` |
| `SV_GroupIndex` | 线程在组内的一维索引 | `0..63` |

关系：`SV_DispatchThreadID = SV_GroupID * numthreads + SV_GroupThreadID`

本 shader 直接使用 `SV_DispatchThreadID`，不需要额外计算坐标。

### 4.2 线程组数量的选择依据

`GroupCount` 的选取原则：
```
GroupCount.X = DivideAndRoundUp(TextureSize.X, numthreads.x)
GroupCount.Y = DivideAndRoundUp(TextureSize.Y, numthreads.y)
GroupCount.Z = 1  (2D 纹理不需要 Z 维度)
```

**为什么不用 CPU 循环代替？**
- GPU 有数千个核心（ALU），可并行处理全部像素
- CPU 串行循环遍历 1024×1024 = 1M 像素，每帧需要数毫秒
- GPU compute shader 可以在几十微秒内完成同样的工作

### 4.3 CPU Dispatch Time vs GPU Execution Time

本代码中记录的 dispatch 耗时测量的是：
```cpp
const double StartTime = FPlatformTime::Seconds();
FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, PassParameters, GroupCount);
const double EndTime = FPlatformTime::Seconds();
const float DispatchTimeMs = (EndTime - StartTime) * 1000.0;
```

**这测的是 CPU 提交 dispatch 命令的耗时**，不是 GPU 实际执行时间。

原因：`Dispatch` 调用通常不会阻塞等待 GPU 完成。它只是把命令写入 RHI command list，然后立即返回。GPU 真正的执行是**异步**的。

如果要测量 GPU 实际耗时，使用以下工具：
| 工具 | 测的内容 | 精度 |
|------|---------|------|
| `stat gpu` | UE 内置的 GPU 时间戳查询 | 帧级别 |
| RenderDoc | GPU 事件的精确 timing | 命令级别 |
| PIX (Win + D3D12) | D3D12 API 调用级 profiling | 最精确 |
| Unreal Insights GPU Trace | UE 完整帧分析 | 帧/Pass 级别 |

### 4.4 FComputeShaderUtils::Dispatch 内部做了什么

`FComputeShaderUtils::Dispatch` 是 UE 封装的一个辅助函数，位于 `Engine/Source/Runtime/RenderCore/Private/ShaderParameterStruct.cpp`。

它内部完成的工作：
1. 将 `FParameters` 结构体中的每个成员绑定到 shader 的对应寄存器 slot
2. 调用 RHI 层的 `RHICmdList.SetComputeShader`
3. 调用 RHI 层的 `RHICmdList.DispatchComputeShader(GroupCount.X, GroupCount.Y, GroupCount.Z)`

### 4.5 当前基线的"基线"意义

基线数据（单次 dispatch）：
```
GroupCount = (128, 128, 1)  // 1024×1024 纹理
总线程数 = 128×128×64 = 1,048,576
CPU dispatch ≈ 微妙级（0.0x ms）
画面 = 红绿渐变
```

后续如果要：
- 增加 structured buffer 读取 → 对比 dispatch 耗时变化
- 增加 indirect draw → 对比 CPU draw call 耗时
- 增加 GPU culling → 对比总帧耗时

这些对比都以这次的干净基线为参照。没有基线，就无法定量判断新方案的性能变化。

---

## 五、常见问题排查

| 现象 | 可能原因 | 排查方法 |
|------|---------|---------|
| 关卡平面全黑 | RT 资产没选对材质 | 检查 `M_GPUComputeOutput` 是否采样了 `RT_GPUComputeOutput` |
| | Dispatch 未执行 | 检查 Output Log 是否有 `GPUDrivenPipeline: Dispatched` |
| | UAV 资源无效 | 检查 `bSupportsUAV` 是否勾选 |
| 画面不更新 | 只在 BeginPlay 执行一次 | 使用 Event Tick 或 Timeline 循环执行 |
| CPU dispatch 时间异常高（>1ms） | Driver 编译 shader（首次） | 第二次调用会恢复 |
| | RHI 线程阻塞 | 检查是否有其他重负载 GPU 工作 |
