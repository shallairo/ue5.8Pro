# GPU Pass Demo UAV 崩溃排查

## 本篇定位

本文记录项目第一个 GPU compute pass demo 所遇到的 D3D Device Removed 崩溃及其根因分析。这是整个项目 GPU 开发的第一道门槛：理解 **GPU 资源创建标志（Resource Creation Flag）** 和 **资源状态转换（Resource State Transition）** 的基本概念。

通过本篇的学习，你应该能回答以下问题：
- 为什么 RenderTarget 能正常在材质中显示，但 compute shader 写入它就会崩溃？
- D3D12 的 `ALLOW_UNORDERED_ACCESS` 标志是什么？
- UE 中如何在 C++ 侧安全地检查一个资源是否支持 UAV？

---

## 一、涉及源码总览

| 文件 | 行范围 | 职责 |
|------|--------|------|
| `Plugins/GPUDrivenPipeline/Private/ComputeShaderInterface.cpp` | 全部 | 蓝图入口，调度 compute shader |
| `Plugins/GPUDrivenPipeline/Public/ComputeShaderInterface.h` | 全部 | UBlueprintFunctionLibrary 声明 |
| `Plugins/GPUDrivenPipeline/Public/SimpleComputeShader.h` | 全部 | FGlobalShader 子类，参数结构体 |
| `Plugins/GPUDrivenPipeline/Private/SimpleComputeShader.cpp` | 全部 | IMPLEMENT_GLOBAL_SHADER 注册 |
| `Plugins/GPUDrivenPipeline/Shaders/SimpleComputeShader.usf` | 全部 | HLSL compute shader 源码 |
| `Plugins/GPUDrivenPipeline/Private/GPUDrivenPipelineModule.cpp` | 全部 | 插件启动时注册 Shaders/ 目录映射 |

---

## 二、完整的执行链路追踪（逐行）

### Step 1：插件启动时注册 Shaders 目录

```
GPUDrivenPipelineModule.cpp:20-31
```

```cpp
void FGPUDrivenPipelineModule::RegisterShaderDirectory()
{
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("GPUDrivenPipeline"));
    if (Plugin.IsValid())
    {
        FString PluginDir = Plugin->GetBaseDir();
        FString ShaderDir = FPaths::Combine(PluginDir, TEXT("Shaders"));
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/GPUDrivenPipeline"), ShaderDir);
    }
}
```

**流程细节：**
- `IPluginManager::Get().FindPlugin()` → 查找当前插件实例
- `Plugin->GetBaseDir()` → 获取插件根目录（即 `Plugins/GPUDrivenPipeline/`）
- `AddShaderSourceDirectoryMapping("/Plugin/GPUDrivenPipeline", ShaderDir)` → 将物理路径 `Shaders/` 映射为虚拟路径 `/Plugin/GPUDrivenPipeline`，后续 `IMPLEMENT_GLOBAL_SHADER` 中的这个虚拟路径才能被引擎找到

### Step 2：Shader 注册到引擎编译管线

```
SimpleComputeShader.cpp:7-10
```

```cpp
IMPLEMENT_GLOBAL_SHADER(FSimpleComputeShader,
    "/Plugin/GPUDrivenPipeline/SimpleComputeShader.usf",
    "MainCS",
    SF_Compute);
```

三个参数的含义：
1. **FSimpleComputeShader** — C++ 类名，引擎用它作为 shader 的 key 做缓存管理
2. **"/Plugin/GPUDrivenPipeline/SimpleComputeShader.usf"** — 虚拟路径，通过 Step 1 注册的映射翻译到物理 `Shaders/SimpleComputeShader.usf`
3. **"MainCS"** — entry point 函数名，告诉编译器在 `.usf` 中找 `void MainCS(...)` 或 `[numthreads] void MainCS(...)`
4. **SF_Compute** — shader 类型：Compute Shader

### Step 3：蓝图触发 C++ 入口

```
ComputeShaderInterface.h:18-19
```

```cpp
UFUNCTION(BlueprintCallable, ...)
static void ExecuteSimpleComputeShader(UTextureRenderTarget2D* OutputRenderTarget);
```

蓝图调用此函数时传入 `RT_GPUComputeOutput` 实例。

### Step 4：C++ 入口做前置校验

```
ComputeShaderInterface.cpp:17-57
```

```
[L18-L22] 检查 OutputRenderTarget 是否为 nullptr
[L24-L30] 检查 SizeX / SizeY 是否有效
[L34-L39] ★ 检查 bSupportsUAV —— 这就是崩溃的根因防护
[L41-L49] 获取 FTextureRenderTarget2DResource 资源指针
[L51-L57] 获取纹理尺寸，再次确认有效
```

关键校验点 `L34-L39`：
```cpp
if (!OutputRenderTarget->bSupportsUAV)
{
    UE_LOG(..., TEXT("...does not have UAV support enabled..."));
    return;
}
```

**这个检查是崩溃后加的**。首次运行时 RT_GPUComputeOutput 的 `bSupportsUAV = false`，但代码直接进入 dispatch → D3D Device Removed。

### Step 5：获取 RHI 资源

```
ComputeShaderInterface.cpp:41-42
```

```cpp
FTextureRenderTarget2DResource* RenderTargetResource =
    static_cast<FTextureRenderTarget2DResource*>(OutputRenderTarget->GameThread_GetRenderTargetResource());
```

`GameThread_GetRenderTargetResource()` 是游戏线程安全的方法，返回一个指向 `FTextureRenderTarget2DResource` 的指针。这个对象封装了 RHI 层的纹理/RT 信息。

### Step 6：ENQUEUE_RENDER_COMMAND 投递到渲染线程

```
ComputeShaderInterface.cpp:59-111
```

```cpp
ENQUEUE_RENDER_COMMAND(GPUDrivenPipeline_ExecuteSimpleComputeShader)(
    [RenderTargetResource, TextureSize, RenderTargetName](FRHICommandListImmediate& RHICmdList)
    {
        // 此 lambda 内的代码在渲染线程执行
        ...
    });
```

**投递机制详解：** `ENQUEUE_RENDER_COMMAND` 宏展开后调用 `FRenderCommandDispatcher::Enqueue`（参见 `Engine/Source/Runtime/RenderCore/Public/RenderingThread.h:992`），将 lambda 包装成 `TRenderCommandTag` 命令对象，放入 `FRenderThreadCommandPipe` 队列。渲染线程（由引擎启动，也叫 `RenderingThread`）在主循环中从该队列取出命令并执行。

### Step 7：渲染线程内获得 UAV 并绑定参数

```
ComputeShaderInterface.cpp:63-89
```

```
[L64-L70]   从 RenderTargetResource 获取 FTextureRHIRef
[L72-L79]   从 RenderTargetResource 获取 FUnorderedAccessViewRHIRef (UAV)
[L81-L83]   填充 FSimpleComputeShader::FParameters
             - PassParameters.OutputTexture = OutputUAV  ← 绑定 UAV
             - PassParameters.TextureSize = FVector2f     ← 传递纹理尺寸
[L85-L90]   获取 TShaderMapRef<FSimpleComputeShader> 实例
             计算 GroupCount（纹理尺寸 ÷ 线程组大小 8×8，向上取整）
```

### Step 8：资源状态转换 + Dispatch

```
ComputeShaderInterface.cpp:92-98
```

```cpp
// L92: 从 SRVMask（只读）→ UAVCompute（可写）
RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture,
    ERHIAccess::SRVMask, ERHIAccess::UAVCompute));

// L95: ★ 实际调度 GPU 计算
FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, PassParameters, GroupCount);

// L98: 从 UAVCompute（可写）→ SRVMask（只读）
RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture,
    ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
```

### Step 9：Shader 执行

```
SimpleComputeShader.usf:10-29
```

```hlsl
[numthreads(8, 8, 1)]
void MainCS(uint3 ThreadID : SV_DispatchThreadID)
{
    // [L13-L19] 边界检查：防止越界
    // [L22-L24] 计算 UV
    // [L27] 写入 OutputTexture[ThreadID.xy] = float4(UV.x, UV.y, 0.5, 1.0)
}
```

每个线程处理一个像素，写入渐变颜色。

---

## 三、第一次运行时崩溃的根因分析

### 崩溃现场

```
GPU Crashed or D3D Device Removed
Incompatible Transition State for Resource RT_GPUComputeOutput
- D3D12_RESOURCE_STATE_UNORDERED_ACCESS requires D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
```

### 根因链

```
RT_GPUComputeOutput 资产创建时未勾选 bSupportsUAV
  → D3D12 创建资源时不带 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
  → C++ 代码尝试 Transition 到 ERHIAccess::UAVCompute
  → D3D12 驱动检测到非法状态转换 → Device Removed
```

**在 D3D12 中：**
- 资源的创建标志（`D3D12_RESOURCE_FLAGS`）是**不可逆的**
- 一个纹理如果创建时未设置 `ALLOW_UNORDERED_ACCESS`，它终身不能作为 UAV 使用
- 试图将它的状态切换到 `D3D12_RESOURCE_STATE_UNORDERED_ACCESS` 会导致 Immediate Context 崩溃

**在 UE 中：**
- `UTextureRenderTarget2D::bSupportsUAV` 对应 D3D12 的 `ALLOW_UNORDERED_ACCESS` 标志
- 但它**不会自动为 true**——需要在 RenderTarget 资产详情页中手动勾选
- 如果没有勾选，`GetUnorderedAccessViewRHI()` 返回的 UAV 指针是无效的

### 修复措施的精髓

```cpp
// ComputeShaderInterface.cpp:34-39
if (!OutputRenderTarget->bSupportsUAV) { return; }
```

以及：

```cpp
// ComputeShaderInterface.cpp:74-79
if (!OutputUAV.IsValid()) { return; }
```

**双重保险：**
1. 游戏线程层检查 `bSupportsUAV` 属性
2. 渲染线程层检查 UAV RHI 指针有效性

两者缺一不可——`bSupportsUAV = true` 是"资源创建时的承诺"，`UAV.IsValid()` 是"运行时状态的确认"。

---

## 四、执行流可视化

```
┌──────────────────────────────────────────────────────────────────┐
│ 游戏线程 (Game Thread)                                            │
│                                                                   │
│  UComputeShaderInterface::ExecuteSimpleComputeShader(RT)          │
│    ├─ [L34] 检查 bSupportsUAV                                     │
│    ├─ [L41] 获取 RenderTargetResource                             │
│    └─ [L59] ENQUEUE_RENDER_COMMAND ──────────────────────────┐    │
└───────────────────────────────────────────────────────────────────┘
                                                                │
                                                                ▼
┌──────────────────────────────────────────────────────────────────┐
│ 渲染线程 (Render Thread)                                         │
│                                                                   │
│  Lambda inside ENQUEUE_RENDER_COMMAND:                            │
│    ├─ [L64] 获取 FTextureRHIRef                                   │
│    ├─ [L72] 获取 UAV (GetUnorderedAccessViewRHI)                 │
│    ├─ [L81] 填充 FParameters (绑定 UAV)                          │
│    ├─ [L85] 获取 TShaderMapRef (FGlobalShader 实例)               │
│    ├─ [L92] Transition: SRVMask → UAVCompute                     │
│    ├─ [L95] FComputeShaderUtils::Dispatch                         │
│    ├─ [L98] Transition: UAVCompute → SRVMask                     │
│    └─ [L103] Log 输出 CPU dispatch 耗时                          │
└───────────────────────────────────────────────────────────────────┘
                                                                │
                                                                ▼
┌──────────────────────────────────────────────────────────────────┐
│ GPU                                                              │
│                                                                   │
│  SimpleComputeShader.usf                                          │
│    └─ MainCS [numthreads(8,8,1)]                                  │
│         ├─ 边界检查 (ThreadID >= Width/Height → return)           │
│         ├─ 计算 UV = ThreadID / TextureSize                       │
│         └─ OutputTexture[ThreadID] = float4(UV.x, UV.y, 0.5, 1)  │
└───────────────────────────────────────────────────────────────────┘
```

---

## 五、UE 渲染补充知识点

### 5.1 D3D12 资源状态 vs UE ERHIAccess

| D3D12 Resource State | UE ERHIAccess | 含义 |
|----------------------|---------------|------|
| `D3D12_RESOURCE_STATE_RENDER_TARGET` | `ERHIAccess::RTV` | RenderTarget 写入 |
| `D3D12_RESOURCE_STATE_UNORDERED_ACCESS` | `ERHIAccess::UAVCompute` | Compute Shader 写入 |
| `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE` | `ERHIAccess::SRVMask` | Pixel Shader 读取 |
| `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` | `ERHIAccess::SRVCompute` | Compute Shader 读取 |
| `D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT` | `ERHIAccess::IndirectArgs` | Indirect Draw 参数读取 |

**核心原则：** GPU 是流水线架构，不同阶段的资源访问方式不同，每次切换资源用途前都必须显式告知驱动（Transition）。忘记 Transition 会导致：
- 驱动崩溃（Device Removed）
- 画面渲染错误（黑色、花屏）
- GPU 调试层报错（如果已启用 Debug Layer）

### 5.2 SRV vs UAV

| | SRV (Shader Resource View) | UAV (Unordered Access View) |
|---|---|---|
| 访问方向 | GPU 读 CPU 显存 | GPU 读写显存 |
| 并发限制 | 只读，可任意并发 | 写操作需要原子指令或同步 |
| 创建要求 | 很低，几乎所有资源都支持 | 需要资源创建时带 ALLOW_UNORDERED_ACCESS |
| 典型用途 | 纹理采样、Buffer 读取 | Compute Shader 输出、RWTexture、RWBuffer |

### 5.3 FGlobalShader 的注册与生命周期

`IMPLEMENT_GLOBAL_SHADER` 注册的 shader 是引擎级别的单例：
- 在引擎初始化时编译
- 缓存在 `FGlobalShaderMap` 中
- 通过 `GetGlobalShaderMap(GMaxRHIFeatureLevel)` 获取
- 渲染线程安全（只读）

对比 `FShaderResource`（材质/网格相关的 shader），`FGlobalShader` 更轻量，常用于：
- Compute shader 后处理
- 全屏 pass
- 工具/调试 shader

### 5.4 GMaxRHIFeatureLevel 的含义

`GMaxRHIFeatureLevel` 是当前 GPU 支持的最高 Feature Level：
- `ERHIFeatureLevel::SM5` → DX11 时代 GPU（Shader Model 5.0）
- `ERHIFeatureLevel::SM6` → DX12 时代 GPU（Shader Model 6.0）

`FSimpleComputeShader::ShouldCompilePermutation` 中限制为 `IsFeatureLevelSupported(Platform, SM5)`，意味着在 SM5 以下的 GPU（如集成显卡或旧架构）上不会编译这个 shader。

---

## 六、部署/测试检查清单

如果要在新项目或新关卡中复现此路径，检查以下条件：

- [ ] `RT_GPUComputeOutput` 的 `bSupportsUAV = true`（右键资产 → 详情 → Render Target → "Support UAV"）
- [ ] 在关卡中放置了显示 `M_GPUComputeOutput` 材质的平面
- [ ] `BP_GPUComputePassDemo` 的 Event Graph 在 `BeginPlay` 中调用了 `Execute Simple Compute Shader`
- [ ] Output Log 可以看到 `GPUDrivenPipeline: Dispatched SimpleComputeShader to ...`
- [ ] 关卡视口中平面显示红绿渐变（对应 UV.x = 红色通道, UV.y = 绿色通道）

---

## 七、DEBUG 经验

1. **GPU crash 最常见的原因不是 shader 语法错，而是资源状态错。** D3D 的 Validation Layer 已经非常成熟，出现 Device Removed 时先在 Output Log 中搜索 `D3D12_RESOURCE_STATES` 相关的错误提示。
2. **RenderTarget 能显示 ≠ 它支持 UAV。** 材质读取 RT 只用到 SRV，不需要 UAV 标志。所以即使 `bSupportsUAV = false`，材质预览仍然正常。
3. **`bSupportsUAV` 是资产属性，不是运行时动态设置的。** 它必须在创建 RT 时确定（或者重新创建 RT），不能在已有 RT 上"开启"UAV。
