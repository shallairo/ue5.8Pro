# GPU 实例数据路径

## 本篇定位

这是项目里第一条"CPU 生成数据 → 上传 GPU → GPU 读取并验证 → 结果读回 CPU"的完整闭环路径。它的目标不是渲染，而是确认 GPU-driven 渲染所需的数据基础设施已经就绪。

通过本篇的学习，你应该能回答以下问题：
- UE 中 CPU 如何把结构体数组上传到 GPU？StructuredBuffer 是什么？
- 如何在 HLSL 中声明和读取与 C++ 对齐的结构体？
- GPU 并行写入同一块内存时为什么需要原子操作？
- FRHIGPUBufferReadback 的工作流程是怎样的？
- 为什么 readback 后需要 FlushRenderingCommands 或延时？

---

## 一、涉及源码总览

| 文件 | 行范围 | 职责 |
|------|--------|------|
| `GPUDrivenInstanceData.h` | 全部 | **核心数据结构**：CPU/GPU 共享的结构体定义 |
| `GPUDrivenInstanceBufferInterface.h` | 全部 | **蓝图接口声明**：Upload + GetResult |
| `GPUDrivenInstanceBufferInterface.cpp` | 全部 | **业务逻辑**：生成数据、上传、调度、readback |
| `InstanceDataValidationShader.h` | 全部 | **Compute shader 包装类**：参数结构体声明 |
| `InstanceDataValidationShader.cpp` | 全部 | **Shader 注册**：IMPLEMENT_GLOBAL_SHADER |
| `InstanceDataValidation.usf` | 全部 | **HLSL compute shader**：验证逻辑 |

> 路径前缀：`Plugins/GPUDrivenPipeline/`（下文简略）

> 路径：`Source/GPUDrivenPipeline/Private/GPUData/*.cpp`、`Source/GPUDrivenPipeline/Public/GPUData/*.h`、`Shaders/*.usf`

---

## 二、完整执行链路追踪

### Part A：数据结构对齐（Cross-Compilation Alignment）

#### A-1 C++ 结构体定义

```cpp
// Public/GPUData/GPUDrivenInstanceData.h:8-13
struct FGPUDrivenInstanceData
{
    FVector3f Position = FVector3f::ZeroVector;   // [L9]  12 bytes
    float Radius = 0.0f;                           // [L10]  4 bytes
    FVector3f Scale = FVector3f::OneVector;        // [L11] 12 bytes
    uint32 Flags = 0;                              // [L12]  4 bytes
};                                                 // Total: 32 bytes
```

**布局细节：**
- `FVector3f` 是 `struct { float X, Y, Z; }`，size = 12 bytes
- 结构体连续排列，没有 padding（float3 + float + float3 + uint = 32 bytes）

#### A-2 HLSL 结构体定义

```hlsl
// Shaders/InstanceDataValidation.usf:7-13
struct FGPUDrivenInstanceData
{
    float3 Position;
    float Radius;
    float3 Scale;
    uint Flags;
};
```

**对齐要求：** C++ 和 HLSL 中字段的声明**顺序和类型必须完全一致**。GPU 读取时是按 byte offset 直接解释的，如果顺序错位，读出来的数据全是乱码。

---

### Part B：CPU 侧生成测试数据

```cpp
// Private/GPUData/GPUDrivenInstanceBufferInterface.cpp:26-53
// GenerateValidationTestInstances(InstanceCount)
```

**执行流程：**
```
[L32]  GridWidth = CeilToInt(Sqrt(InstanceCount))   // 如 1024 → GridWidth = 32
[L35-L50] 双层循环填充每个实例：
  Instance.Position = (X * 100, Y * 100, 0)          // 间隔 100 单位
  Instance.Radius = 50.0f                            // 全部正数
  Instance.Scale = (1, 1, 1)
  Instance.Flags = Index % 4                         // 循环 0, 1, 2, 3
```

**为什么这样设计：**
- 数据是确定性的，GPU 验证结果可以和 CPU 预期严格比对
- 间隔 100、Radius = 50，表明实例间有间隙，为后续 culling 测试预留空间
- Flags 循环 0-3，验证 GPU 正确读取到了每个实例的 Flag 字段

---

### Part C：上传到 GPU（渲染线程内执行）

```
Private/GPUData/GPUDrivenInstanceBufferInterface.cpp:156-253
ENQUEUE_RENDER_COMMAND → lambda → 渲染线程
```

#### C-1 创建 InstanceBuffer（L164-L170）

```cpp
FBufferRHIRef InstanceBuffer = UE::RHIResourceUtils::CreateBufferFromArray<FGPUDrivenInstanceData>(
    RHICmdList,
    TEXT("GPUDrivenPipeline.InstanceData"),
    EBufferUsageFlags::StructuredBuffer | EBufferUsageFlags::ShaderResource,
    ERHIAccess::SRVCompute,             // 创建后状态：Compute Shader 只读
    TConstArrayView<FGPUDrivenInstanceData>(Instances));  // 传入 CPU 数据
```

**函数内部做了什么（UE 封装）：**
1. 根据 `sizeof(FGPUDrivenInstanceData) × Num` 计算 Buffer size
2. 在显存上分配 `FRHIBuffer`
3. 通过 `RHICmdList.Transfer` 或 `Lock/Unlock` 将 CPU 数据复制到显存
4. 设置初始资源状态为 `ERHIAccess::SRVCompute`
5. 返回 `FBufferRHIRef`（RHI 层 Buffer 句柄）

**`StructuredBuffer` 的含义：** GPU 知道这个 Buffer 中每个元素的 size 是固定的（32 bytes），元素之间是连续排列的。这允许 shader 通过索引 `[n]` 随机访问。

#### C-2 创建 SRV（L178-L181）

```cpp
FShaderResourceViewRHIRef InstanceDataSRV = RHICmdList.CreateShaderResourceView(
    InstanceBuffer,
    FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(InstanceBuffer));
```

SRV 就是"shader 读取这个 Buffer 时的视图描述"。同一个 Buffer 可以有不同的 SRV 描述（比如 reinterpret 成另一种格式），但这里只是把 structured buffer 的 SRV 暴露给 shader。

#### C-3 创建 SummaryBuffer + UAV（L191-L206）

```cpp
FBufferRHIRef SummaryBuffer = UE::RHIResourceUtils::CreateBufferFromArray<uint32>(
    RHICmdList,
    TEXT("GPUDrivenPipeline.InstanceValidationSummary"),
    EBufferUsageFlags::StructuredBuffer | EBufferUsageFlags::ShaderResource
        | EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::SourceCopy,
    ERHIAccess::UAVCompute,
    TConstArrayView<uint32>(SummaryInitialValues, ValidationSummaryElementCount));
```

**重点：** SummaryBuffer 有 `UnorderedAccess` 标志——这是 GPU 要写入的 Buffer。

对比 InstanceBuffer 和 SummaryBuffer：

| | InstanceBuffer | SummaryBuffer |
|---|---|---|
| 用途 | GPU 读取 | GPU 写入 |
| UsageFlags | StructuredBuffer + ShaderResource | StructuredBuffer + UAV + SourceCopy |
| 初始状态 | SRVCompute | UAVCompute |
| 视图类型 | SRV | UAV |

#### C-4 绑定参数 + Dispatch（L215-L229）

```cpp
FInstanceDataValidationShader::FParameters PassParameters;
PassParameters.InstanceData = InstanceDataSRV;         // 输入：实例数据
PassParameters.OutSummary = SummaryUAV;                // 输出：统计结果
PassParameters.InstanceCount = static_cast<uint32>(InstanceCount);

TShaderMapRef<FInstanceDataValidationShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
const FIntVector GroupCount(
    FMath::DivideAndRoundUp(InstanceCount, ValidationThreadGroupSize),  // 1024/64 = 16
    1, 1);

FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, PassParameters, GroupCount);
```

**Dispatch 参数：**
- `InstanceCount = 1024`，`ValidationThreadGroupSize = 64`
- `GroupCount = (16, 1, 1)` → 16 个线程组，每组 64 个线程，共 1024 个线程
- 每个线程处理一个实例

#### C-5 发起 Readback（L234-L238）

```cpp
RHICmdList.Transition(FRHITransitionInfo(SummaryBuffer, ERHIAccess::UAVCompute, ERHIAccess::CopySrc));

TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback =
    MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("GPUDrivenPipeline.InstanceValidationReadback"));
Readback->EnqueueCopy(RHICmdList, SummaryBuffer, ValidationSummarySizeBytes);
```

**Readback 机制：**
1. SummaryBuffer 状态从 `UAVCompute` → `CopySrc`（GPU 可读 → 可复制）
2. `FRHIGPUBufferReadback` 创建一个"staging buffer"（CPU + GPU 都可访问的中间缓冲）
3. `EnqueueCopy` 把 SummaryBuffer 的内容复制到 staging buffer（这是一个 GPU → GPU 的 copy 操作）
4. CPU 稍后通过 `Lock/Unlock` 读取 staging buffer

---

### Part D：GPU Shader 执行验证

```
Shaders/InstanceDataValidation.usf:19-41
```

```hlsl
[numthreads(THREADGROUP_SIZE_X, 1, 1)]
void MainCS(uint3 ThreadID : SV_DispatchThreadID)
{
    const uint InstanceIndex = ThreadID.x;   // [L22] 线程 ID = 实例索引
    if (InstanceIndex >= InstanceCount)       // [L23-26] 边界保护
        return;

    FGPUDrivenInstanceData Instance = InstanceData[InstanceIndex];  // [L28]

    // [L30] InterlockedAdd(OutSummary[0], 1);    // 已处理计数 +1
    // [L33] 如果 Radius > 0 → InterlockedAdd(OutSummary[1], 1);  // 有效半径计数
    // [L37] InterlockedAdd(OutSummary[2], Instance.Flags);        // Flags 累加
    // [L39] QuantizedPositionX = abs(Position.x);
    // [L40] InterlockedAdd(OutSummary[3], QuantizedPositionX);    // 位置校验和
}
```

**原子操作的必要性：** GPU 的 1024 个线程同时执行，如果使用普通的 `OutSummary[0] = OutSummary[0] + 1`，最终结果可能远小于 1024。原因：
1. 线程 A 读取 OutSummary[0] = 5
2. 线程 B 读取 OutSummary[0] = 5（还没等到 A 写回）
3. A 写入 6
4. B 写入 6（覆盖了 A，正确应该是 7）

`InterlockedAdd` 是硬件级别的原子指令，保证多线程并发写入的正确性。

---

### Part E：CPU 读回结果

```
Private/GPUData/GPUDrivenInstanceBufferInterface.cpp:256-263  GetLastInstanceDataValidationResult
```

```cpp
bool UGPUDrivenInstanceBufferInterface::GetLastInstanceDataValidationResult(FGPUDrivenFrustumCullResult& OutResult)
{
    TryResolveReadback_GameThread();  // [L258] 尝试解析 readback
    FScopeLock Lock(&GValidationMutex);  // [L260] 线程安全复制结果
    OutResult = GValidationResult;       // [L261]
    return bValidationReadbackReady;     // [L262]
}
```

#### TryResolveReadback_GameThread 内部（L55-L108）

```
[L60-L66]  检查 Readback 是否 IsReady()
            未完成 → 直接 return
[L69-L103] ENQUEUE_RENDER_COMMAND 投递 readback 解析工作到渲染线程：
             - Readback->Lock(SummarySize)   // 获得 staging buffer 的 CPU 指针
             - FMemory::Memcpy(Summary, ReadbackData)  // 复制到栈变量
             - Readback->Unlock()
             - 将 Summary 写入 GValidationResult 全局变量
             - 设置 bValidationReadbackReady = true
[L107]     ★ FlushRenderingCommands()  // 强制等待渲染线程完成
```

**FlushRenderingCommands() 的作用：** 游戏线程在这里阻塞，直到渲染线程完成了`Lock → Memcpy → Unlock`。没有这一步，游戏线程立即检查 `GValidationResult` 时可能读到的是上一帧的数据（因为 lambda 还在渲染线程队列里没被执行）。

---

## 三、完整数据流程图

```
游戏线程                                                    GPU
───────                                                    ───
GenerateValidationTestInstances(1024)                      
  ↓                                                         
TArray<FGPUDrivenInstanceData> (CPU 内存)                   
  ↓                                                         
ENQUEUE_RENDER_COMMAND ──────────────────────────────┐      
  ↑                                                   │      
  │                                                   ▼      
  │                 渲染线程 (Rendering Thread)              
  │                 ─────────────────────                     
  │                  CreateBufferFromArray(Instances)        
  │                    ↓                                     
  │                  [显存] Instance StructuredBuffer        
  │                    + SRV                                 
  │                    + InitialData (来自 CPU 数组)         
  │                                                          
  │                  CreateBufferFromArray(SummaryInit)      
  │                    ↓                                     
  │                  [显存] Summary StructuredBuffer         
  │                    + UAV                                 
  │                    + InitialData = {0,0,0,0}             
  │                                                          
  │                  Dispatch (groups=16,1,1) ────────────┐  
  │                    ↑                                   │  
  │                    │                                   ▼  
  │                    │              ┌─────────────────────┐
  │                    │              │ InstanceData[0..1023]│
  │                    │              │    (只读 SRV)       │
  │                    │              ├─────────────────────┤
  │                    │              │ OutSummary[0..3]    │
  │                    │              │    (UAV 可写)       │
  │                    │              └─────────────────────┘
  │                    │                                      
  │                  Transition: UAVCompute → CopySrc         
  │                  EnqueueCopy(Summary, Staging) ────────┐  
  │                    ↑                                   │  
  │                    │                                   ▼  
  │                    │              [显存] Staging Buffer  
  │                    │              (GPU→CPU 可访问)       
  │                    │                                      
  │  ────────────────────────────────────────────────────┐   
  │  ↑                                                   │   
  ▼  │                                                   │   
蓝图: Delay(0.2) →                                        │   
  GetLastInstanceDataValidationResult()                    │   
    ↓                                                      │   
  TryResolveReadback_GameThread()                          │   
    ↓                                                      │   
  Check IsReady() ──── 不 ready → return false             │   
    ↓ ready                                                │   
  ENQUEUE_RENDER_COMMAND ──────────────────────────┐       │   
    ↑                                               │       │   
    │                                               ▼       │   
    │              Readback->Lock(16 bytes)                 │   
    │              Memcpy(Summary, ReadbackData)    ←───────┘   
    │              Readback->Unlock()                        
    │              Store to GValidationResult                
    │                                                      
    └── FlushRenderingCommands() (阻塞等待)               
    ↓                                                      
  返回 GValidationResult 给蓝图                             
```

---

## 四、关键代码索引表

| 步骤 | 文件 | 行号 | 作用 |
|------|------|------|------|
| 结构体定义 C++ | `GPUDrivenInstanceData.h` | L8-L13 | FGPUDrivenInstanceData |
| 结构体定义 HLSL | `InstanceDataValidation.usf` | L7-L13 | 同上，对齐 C++ |
| 生成测试实例 | `GPUDrivenInstanceBufferInterface.cpp` | L26-L53 | GenerateValidationTestInstances |
| 清空上次结果 | 同上 | L130-L139 | Reset readback 状态 |
| 创建 Instance Buffer | 同上 | L164-L170 | CreateBufferFromArray |
| 创建 Instance SRV | 同上 | L178-L181 | CreateShaderResourceView |
| 创建 Summary Buffer | 同上 | L191-L196 | CreateBufferFromArray<uint32> |
| 创建 Summary UAV | 同上 | L204-L206 | CreateUnorderedAccessView |
| 绑定参数 | 同上 | L215-L218 | PassParameters |
| Dispatch compute | 同上 | L229 | FComputeShaderUtils::Dispatch |
| Transition + Readback | 同上 | L234-L238 | EnqueueCopy |
| 蓝图接口 Upload | 同上 | L111-L253 | UploadTestInstanceData |
| Readback 解析 | 同上 | L55-L108 | TryResolveReadback_GameThread |
| 蓝图接口 GetResult | 同上 | L256-L263 | GetLastInstanceDataValidationResult |
| FlushRenderingCommands | 同上 | L107 | 同步等待 GPU readback |
| Shader 入口 | `InstanceDataValidation.usf` | L19-L41 | MainCS |
| Shader 原子操作 | 同上 | L30-L40 | InterlockedAdd |
| Shader 编译环境 | `InstanceDataValidationShader.cpp` | L17-L22 | THREADGROUP_SIZE_X = 64 |

---

## 五、UE 渲染补充知识点

### 5.1 StructuredBuffer 在 UE/D3D12 中的实现

**D3D12 层面：**
```cpp
D3D12_RESOURCE_DESC desc = {};
desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
desc.Width = sizeof(FGPUDrivenInstanceData) * NumElements;  // 32 * 1024 = 32768 bytes
desc.StructureByteStride = sizeof(FGPUDrivenInstanceData);  // 32 bytes ← 这就是 "Structured"
desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;    // (如果需要 UAV)
```

**对比 ByteAddressBuffer 和 StructuredBuffer：**

| Buffer 类型 | 元素布局 | Shader 访问 |
|------------|---------|------------|
| StructuredBuffer\<T\> | 固定 stride | `Buffer[index].Field` |
| ByteAddressBuffer | 无结构，纯字节 | 需手动 `ByteAddressBuffer.Load(offset)` |
| TypedBuffer (RWBuffer\<uint4\>) | R32/UAV 格式 | 格式受限 |

StructuredBuffer 的优势：GPU 知道每个元素的 size，`[index]` 访问就是 O(1) 随机访问。

### 5.2 SRV/UAV 的"视图"概念

```
               ┌─────────────┐
               │ FRHIBuffer  │  ← 物理显存数据
               └──────┬──────┘
                      │
          ┌───────────┴───────────┐
          ▼                       ▼
    ┌─────────┐            ┌─────────┐
    │ SRV     │            │ UAV     │ ← "视图"描述了访问方式
    │ Read    │            │ Write   │
    │ Structured│           │ Structured│
    │ Compute │            │ Compute │
    └─────────┘            └─────────┘
```

- 一个 Buffer 可以有多个 SRV/UAV（不同视图）
- SRV 不能写，UAV 可以读写
- 同一个 Buffer 在不同阶段的用途决定创建哪种 View

### 5.3 FRHIGPUBufferReadback 的工作原理

```
GPU 显存                    Staging Buffer                CPU 内存
┌────────────┐             ┌──────────────┐             ┌────────┐
│ Summary    │───Copy────→ │ Staging      │───Lock────→ │ TArray │
│ (UAV)      │  (GPU→GPU)  │ (Readback)   │  (DMA 完成) │        │
└────────────┘             └──────────────┘             └────────┘
                                  Lock 之前:
                                  Lock() 会等待 GPU copy 完成
                                  如果 copy 未完成，Lock 会阻塞 CPU
```

**为什么蓝图需要 Delay(0.2)：**
- Readback->IsReady() 检查的是 GPU copy 是否完成
- GPU 是异步的，dispatch 后不会立即完成
- 蓝图不能阻塞渲染线程（会死锁），所以用 Delay 等待几帧
- `TryResolveReadback_GameThread` 中同时检查 `IsReady()` 和 `bValidationReadbackReady` 标志，避免重复解析

### 5.4 InterlockedAdd 对比 CPU 原子操作

| | GPU InterlockedAdd | CPU std::atomic |
|---|---|---|
| 硬件 | 全局内存原子指令（Global Atomix） | CPU cache-line lock / CAS |
| 性能 | 高竞争下会有 bank conflict | 效率取决于缓存一致性 |
| 数量限制 | 同一 UAV 地址无限制 | 无限制 |
| 粒度 | 32-bit / 64-bit | 任意 |

### 5.5 线程安全的全局变量模式

本代码中的线程安全模式：

```cpp
// Anonymous namespace 全局变量
FCriticalSection GValidationMutex;                          // 互斥锁
TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> GValidationReadback;  // 线程安全智能指针
FGPUDrivenInstanceValidationResult GValidationResult;      // 共享结果
bool bValidationReadbackReady = false;                      // 状态标志

// 写操作：渲染线程写时加锁
FScopeLock Lock(&GValidationMutex);
GValidationResult.XXX = YYY;

// 读操作：游戏线程读时加锁
FScopeLock Lock(&GValidationMutex);
OutResult = GValidationResult;
```

这种模式可以安全地在游戏线程和渲染线程之间传递小数据量结果。

---

## 六、验证结果解读

当代码正确执行时，Output Log 输出：

```
GPUDrivenPipeline: Uploaded 1024 instance records (32768 bytes) and dispatched InstanceDataValidation (groups=16,1,1, cpu-dispatch=0.009 ms).
GPUDrivenPipeline: Instance validation readback ready (processed=1024, valid-radius=1024, flag-sum=1536, checksum=1587200).
```

**数值验证：**
| 字段 | 值 | 预期来源 |
|------|-----|---------|
| processed=1024 | 1024 | GPU 处理了全部实例，InterlockedAdd 正确 |
| valid-radius=1024 | 1024 | 所有实例 Radius = 50 > 0 |
| flag-sum=1536 | 1536 | 实例编号 0..1023 `% 4` → `0+1+2+3` 循环 256 次 → `(0+1+2+3) × 256 = 1536` |
| checksum=1587200 | 1587200 | `sum(abs(Position.x)) = 1024 个实例的 x 坐标绝对值之和` |

**这些值验证了：**
1. StructuredBuffer 上传内容正确，GPU 读到的和 CPU 生成的一致
2. InterlockedAdd 原子操作在 1024 线程并发下正确汇总
3. Readback 路径完整，数据从 GPU 端回到 CPU 端

---

## 七、常见问题排查

| 现象 | 可能原因 | 排查方法 |
|------|---------|---------|
| readback 永远不 ready | GPU copy 未完成 | 检查 `Latency > Delay`，增大 Delay 值 |
| | Readback 已被析构 | 检查 `GValidationReadback` 生命周期管理 |
| flag-sum 不正确 | HLSL Flags 字段读取偏移错误 | 检查 C++/HLSL 结构体排列顺序是否一致 |
| processed 为 0 | Dispatch 未执行 | 检查 `InstanceCount` 是否为正数 |
| | Shader 边界保护过早返回 | 检查 `ThreadIndex >= InstanceCount` |
| 蓝图取不到结果 | 条件节点 false | 使用 `Delay(0.1~0.5)` 重试 |
| CPU dispatch 时间 > 1ms | 首次 shader 编译 | 第二次调用恢复正常 |
