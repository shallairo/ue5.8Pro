# 2026-06-04 00:00 GPU 实例数据路径

## 这次我们新增了什么

这次在 `GPUDrivenPipeline` 插件里，新建了一条“实例数据先上传到 GPU，再由 compute shader 读取并验证”的路径。

它的目标不是马上渲染成千上万个物体，而是先确认下面这件基础工作已经通了：

```text
CPU 生成实例数据 -> GPU 读取结构化数据 -> GPU 写出验证结果 -> CPU 读回结果确认
```

这一步很像给后面的 `indirect draw` 和 `GPU culling` 打地基。地基不稳，后面的渲染逻辑就会很难排查。

## 对应源码

这次主要涉及四组代码：

- [GPUDrivenInstanceData.h](/D:/UGit/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/GPUDrivenInstanceData.h)
- [GPUDrivenInstanceBufferInterface.h](/D:/UGit/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/GPUDrivenInstanceBufferInterface.h)
- [GPUDrivenInstanceBufferInterface.cpp](/D:/UGit/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/GPUDrivenInstanceBufferInterface.cpp)
- [InstanceDataValidation.usf](/D:/UGit/ue5.8Pro/Plugins/GPUDrivenPipeline/Shaders/InstanceDataValidation.usf)

如果你想复盘整条链路，这四个文件就是最值得反复看的入口。

## 第一部分：实例数据结构是什么

先看 [GPUDrivenInstanceData.h](/D:/UGit/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/GPUDrivenInstanceData.h)。

当前结构体是：

```cpp
struct FGPUDrivenInstanceData
{
    FVector3f Position = FVector3f::ZeroVector;
    float Radius = 0.0f;
    FVector3f Scale = FVector3f::OneVector;
    uint32 Flags = 0;
};
```

这四个字段分别在表达：

- `Position`：实例在世界里的位置
- `Radius`：包围球半径，后面可以参与 culling
- `Scale`：缩放信息
- `Flags`：一个简单的状态位，先拿来做验证，后续也能承载更多含义

这里最关键的知识点不是字段本身，而是“CPU 和 GPU 必须对这个结构的理解一致”。  
如果 C++ 里字段顺序是 `Position -> Radius -> Scale -> Flags`，那 HLSL 里也必须按同样顺序解释它，否则 GPU 读出来就会错位。

## 第二部分：蓝图入口在做什么

再看 [GPUDrivenInstanceBufferInterface.h](/D:/UGit/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/GPUDrivenInstanceBufferInterface.h)。

暴露给蓝图的核心接口有两个：

```cpp
static void UploadTestInstanceData(int32 InstanceCount = 1024);
static bool GetLastInstanceDataValidationResult(FGPUDrivenInstanceValidationResult& OutResult);
```

可以这样理解：

- `UploadTestInstanceData`：发起一次完整的“生成数据、上传 GPU、调度验证 shader”
- `GetLastInstanceDataValidationResult`：查询那次 GPU 验证的结果是否已经可以读回

这里的关键概念是：**readback 是异步的**。  
也就是说，蓝图不能在上传后立刻假设结果已经准备好，所以我们在 `BeginPlay` 链路里加了 `Delay(0.2)` 再去取结果。

## 第三部分：CPU 是怎么生成测试实例数据的

重点看 [GPUDrivenInstanceBufferInterface.cpp](/D:/UGit/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/GPUDrivenInstanceBufferInterface.cpp) 里的实例生成逻辑。

代码会先根据实例数量算一个网格宽度：

```cpp
const int32 GridWidth = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(InstanceCount))));
constexpr float Spacing = 100.0f;
```

它的意思是：

- 把一批实例尽量按接近正方形的网格排开
- 每个实例间隔 `100.0f`

后面每条实例会被填充成类似这样：

```cpp
Instance.Position = FVector3f(X * Spacing, Y * Spacing, 0.0f);
Instance.Radius = 50.0f;
Instance.Scale = FVector3f(1.0f, 1.0f, 1.0f);
Instance.Flags = static_cast<uint32>(Index % 4);
```

这几行很适合拿来理解“为什么验证结果能算得出来”：

- `Radius` 全部是正数，所以 `ValidRadiusCount` 应该等于实例总数
- `Flags` 按 `0, 1, 2, 3` 循环，所以最后的 `FlagSum` 不是随机数
- `Position` 不是全零，所以位置校验和 `PositionChecksum` 应该非零

也就是说，这不是随便生成一堆数据，而是在生成“容易验证正确性”的数据。

## 第四部分：Structured Buffer、SRV 和 UAV 分别是什么

同样在 [GPUDrivenInstanceBufferInterface.cpp](/D:/UGit/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/GPUDrivenInstanceBufferInterface.cpp) 里，真正上传 GPU 的关键是：

```cpp
UE::RHIResourceUtils::CreateBufferFromArray<FGPUDrivenInstanceData>(...)
```

它会把 `TArray<FGPUDrivenInstanceData>` 上传成 GPU 端的 buffer。

这里最值得记住的三个词：

- `StructuredBuffer`：一段“按结构体排列”的 GPU 数据
- `SRV`：Shader Resource View，表示 shader 从哪里读这段 buffer
- `UAV`：Unordered Access View，表示 shader 往哪里写结果

这次实现里：

- 实例数据 buffer 是给 shader 读的，所以会创建 `SRV`
- summary buffer 是给 shader 写统计结果的，所以会创建 `UAV`

你可以把它理解成：

```text
实例数据 = 输入
summary 缓冲 = 输出
```

## 第五部分：GPU shader 真正做了什么

看 [InstanceDataValidation.usf](/D:/UGit/ue5.8Pro/Plugins/GPUDrivenPipeline/Shaders/InstanceDataValidation.usf)。

HLSL 里也定义了一份和 C++ 对齐的结构：

```hlsl
struct FGPUDrivenInstanceData
{
    float3 Position;
    float Radius;
    float3 Scale;
    uint Flags;
};
```

然后 shader 的主要输入输出是：

```hlsl
StructuredBuffer<FGPUDrivenInstanceData> InstanceData;
RWStructuredBuffer<uint> OutSummary;
uint InstanceCount;
```

含义分别是：

- `InstanceData`：GPU 要读取的实例数组
- `OutSummary`：GPU 要写回的统计结果
- `InstanceCount`：本次要处理多少条实例

shader 中最基础的一步是边界保护：

```hlsl
const uint InstanceIndex = ThreadID.x;
if (InstanceIndex >= InstanceCount)
{
    return;
}
```

这是因为 dispatch 的线程数通常会向上取整，最后一组线程里可能会有超出实例数量的线程，不拦住就会越界读 buffer。

真正的统计逻辑使用了 `InterlockedAdd`：

```hlsl
InterlockedAdd(OutSummary[0], 1);
InterlockedAdd(OutSummary[1], 1);
InterlockedAdd(OutSummary[2], Instance.Flags);
InterlockedAdd(OutSummary[3], QuantizedPositionX);
```

这里的知识点是：GPU 上很多线程会同时写同一个 summary buffer。  
如果不用原子操作，统计结果就会互相覆盖，最后的数字不可信。`InterlockedAdd` 的作用，就是让这些“并发写同一块结果”的操作保持正确。

## 第六部分：为什么要做 readback

还是看 [GPUDrivenInstanceBufferInterface.cpp](/D:/UGit/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/GPUDrivenInstanceBufferInterface.cpp)。

当前实现使用了：

```cpp
FRHIGPUBufferReadback
```

它的用途不是给正式渲染路径长期依赖，而是给我们一个“确认 GPU 到底算出了什么”的窗口。

流程大致是：

1. GPU 把统计结果写进 summary buffer
2. `FRHIGPUBufferReadback` 把这小段结果转到 CPU 可读区域
3. `GetLastInstanceDataValidationResult` 先检查 `IsReady()`
4. ready 之后才 `Lock / Memcpy / Unlock`

所以蓝图里那段 `Delay(0.2)` 并不是绕路，而是在尊重 GPU 和 CPU 的异步关系。

## 第七部分：这次真实跑出来的结果说明了什么

这次用户在 UE 里已经跑出了下面这组日志：

```text
GPUDrivenPipeline: Uploaded 1024 instance records (32768 bytes) and dispatched InstanceDataValidation (groups=16,1,1, cpu-dispatch=0.009 ms).
GPUDrivenPipeline: Instance validation readback ready (processed=1024, valid-radius=1024, flag-sum=1536, checksum=1587200).
```

这组结果很有信息量：

- `processed=1024`：GPU 真的处理了全部实例
- `valid-radius=1024`：所有实例的 `Radius` 都被识别为有效
- `flag-sum=1536`：`Flags` 字段被正确读取，而且与 CPU 生成规则一致
- `checksum=1587200`：位置字段也参与了 GPU 统计，不是空跑

这说明我们已经不只是“shader 被 dispatch 了”，而是“shader 正确读懂了我们定义的结构化数据”。

## 这一阶段真正证明了什么

第一阶段的渐变 demo 证明的是：

```text
Compute Shader 可以写 RenderTarget
```

而这次证明的是：

```text
CPU 可以把一批实例结构体上传给 GPU，GPU 可以读取、统计并返回结果
```

这两者的差别很大。

前者证明“GPU pass 通了”，后者证明“GPU-driven 所依赖的数据路径通了”。  
后续要做 `indirect draw`、`GPU culling`，真正靠的是第二件事。

## 记住这次的关键点

如果把这次的知识压成几句话，就是：

- `FGPUDrivenInstanceData` 是 CPU 和 GPU 之间约定好的实例格式
- `StructuredBuffer` 是 GPU 读取实例数组的方式
- `SRV` 负责读输入数据
- `UAV` 负责写输出结果
- `FRHIGPUBufferReadback` 负责把很小的一段验证结果带回 CPU
- 真正有价值的不是“调度成功”，而是“结果和我们预期一致”

这就是这条实例数据路径的意义，也是下一步进入 `indirect draw` 之前最关键的一块地基。
