# GPU Visible Count 与 Camera Frustum 学习记录

## 这次做了什么

这次是在 `GPU Frustum Culling MVP` 基础上，把下一阶段计划里的两件关键事情接上了：

1. 给 frustum culling 增加 GPU visible count readback。
2. 新增一个使用真实相机 frustum plane 的执行入口。

相关源码：

- [GPUDrivenIndirectDrawInterface.h](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/GPUDrivenIndirectDrawInterface.h)
- [GPUDrivenIndirectDrawInterface.cpp](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/GPUData/GPUDrivenIndirectDrawInterface.cpp)
- [FrustumCullShader.h](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/GPUData/FrustumCullShader.h)
- [FrustumCullInstances.usf](/D:/ue/ue5.8Pro/Plugins/GPUDrivenPipeline/Shaders/FrustumCullInstances.usf)

## 新增接口

### `Get Last Frustum Cull Result`

新增蓝图查询接口：

```cpp
Get Last Frustum Cull Result(FGPUDrivenFrustumCullResult& OutResult)
```

它返回的结构体包括：

- `InstanceCount`
- `EstimatedVisibleCount`
- `GpuVisibleCount`
- `BufferSizeBytes`
- `CpuCullDispatchTimeMs`
- `CpuDrawDispatchTimeMs`

这里最关键的是：

```text
GpuVisibleCount
```

这个值不再是 CPU 估算，而是从 GPU summary buffer readback 回来的真实结果。

### `Execute Camera Frustum Culled Indirect Draw`

新增相机版入口：

```cpp
Execute Camera Frustum Culled Indirect Draw(
    UTextureRenderTarget2D* OutputRenderTarget,
    AActor* CameraActor,
    int32 InstanceCount = 1024,
    float FieldOfViewDegrees = 90.0f,
    float NearPlane = 10.0f,
    float FarPlane = 10000.0f)
```

这个入口不再使用固定测试平面，而是从 `CameraActor` 的世界变换构造 6 个世界空间 frustum plane。

## GPU visible count readback 是怎么接上的

之前的 culling shader 只写：

- `VisibleInstanceBuffer`
- `IndirectArgsBuffer`

这次新增了：

```hlsl
RWBuffer<uint> OutSummary;
```

在 shader 结束时写入：

```hlsl
OutSummary[0] = VisibleCount;
OutSummary[1] = InstanceCount;
```

然后 C++ 侧把这个 summary buffer 通过 `FRHIGPUBufferReadback` 复制回 CPU。

游戏线程查询 `Get Last Frustum Cull Result(...)` 时，会先检查：

```text
Readback 是否 ready
```

ready 后再把 summary 解包到 `FGPUDrivenFrustumCullResult`。

这样做的意义是：

```text
我们终于可以严格比较 CPU 估算数量和 GPU 实际可见数量
```

## 真实相机 frustum 是怎么构造的

当前实现没有直接依赖 UE 主渲染视图，而是从 `CameraActor` 的 transform 手工构造 frustum plane。

核心输入是：

- `Origin`
- `Forward`
- `Right`
- `Up`
- `FieldOfViewDegrees`
- `NearPlane`
- `FarPlane`
- RenderTarget 宽高推导出的 `AspectRatio`

思路是：

1. 先求出 near plane 的中心点。
2. 再根据 FOV 和 aspect 求出 near plane 的半宽和半高。
3. 构造 near plane 四个角：
   - `NearTopLeft`
   - `NearTopRight`
   - `NearBottomLeft`
   - `NearBottomRight`
4. 用这些点和相机原点构造 left / right / top / bottom 四个侧平面。
5. near / far 平面直接由中心点和 `Forward` 方向构造。

这里有个很关键的细节：

```text
平面法线方向可能会反
```

所以代码里不是盲目相信叉积方向，而是用一个 frustum 内部点去做一次校正。如果内部点落在负侧，就把法线翻转。

这一步能显著降低“视野内全被裁掉”这类错误。

## 为什么这一步比固定测试平面更重要

固定测试平面只能证明：

```text
GPU culling 逻辑本身是通的
```

真实相机 frustum 才能证明：

```text
GPU culling 已经开始接受真实视角输入
```

这意味着项目正在从“固定脚本式 demo”迈向“和视图相关的 GPU-driven 路径”。

## 现在还没解决什么

这一步虽然把功能接上了，但仍然有几个明确限制：

- 当前 shader 仍然是 `[numthreads(1, 1, 1)]`
- 还不是性能实现
- 还没有接 UE 的真实 StaticMesh / Nanite 渲染路径
- 还没有把相机 frustum 从主渲染视图直接抽出来

所以当前阶段的重点仍然是：

```text
把 correctness 做严谨
```

不是：

```text
立刻追求高性能
```

## 测试时最该盯什么

### 1. `EstimatedVisibleCount` 和 `GpuVisibleCount`

固定测试平面下，这两个值应该一致。

如果不一致，先怀疑：

- summary readback 生命周期
- shader 写 summary 的位置
- 上一轮结果被新一轮覆盖

### 2. 相机背向网格时结果是否接近 0

这是检查平面方向最直接的办法。

如果相机都背过去了，结果还很多，通常说明 frustum plane 法线方向有问题。

### 3. 画面变化是否和 readback 数量一致

RenderTarget 上看到的实例数量变化，应该和 `GpuVisibleCount` 的变化方向一致。

如果数量显示下降了，但画面还很多，通常要回头检查：

- draw 读的是不是 `VisibleInstanceBuffer`
- indirect args 是否真的来自 GPU culling 结果
## 当前阶段测试数据记录清单

后续这条链路的测试结果，统一至少记录下面这些数据，并同步补到这份学习文档里。

### 1. 测试基本信息

- 测试日期
- 测试人
- 测试关卡
- 使用节点
- RenderTarget 名称与分辨率
- InstanceCount
- 相机模式：固定测试 frustum / 真实相机 frustum

### 2. 输入参数

- `FieldOfViewDegrees`
- `NearPlane`
- `FarPlane`
- `CameraActor` 名称
- 相机位置
- 相机朝向

如果是固定测试 frustum，可以把相机参数记为“不适用”，但要明确写明使用的是 `Execute Test Frustum Culled Indirect Draw`。

### 3. 核心结果数据

- `InstanceCount`
- `EstimatedVisibleCount`
- `GpuVisibleCount`
- `BufferSizeBytes`
- `CpuCullDispatchTimeMs`
- `CpuDrawDispatchTimeMs`

其中最关键的是：

```text
EstimatedVisibleCount
GpuVisibleCount
```

固定测试 frustum 阶段，这两个值应该一致。真实相机 frustum 阶段，至少要确认它们的变化趋势和画面变化一致。

### 4. 画面观察结果

- 当前画面是否正常渲染
- 可见实例主要分布区域
- 相机移动后画面是否明显变化
- 相机背向网格后是否接近全裁掉

### 5. 日志记录

至少保留两类日志：

- submit 日志
- readback ready 日志

当前建议保留的关键日志格式为：

```text
GPUDrivenPipeline: Frustum culled indirect draw submitted ...
GPUDrivenPipeline: Frustum cull readback ready ...
```

### 6. 结论判断

每轮测试最后必须写一个简短结论：

- 是否通过
- 不通过时最可能的问题点
- 下一轮准备调整什么

## 推荐记录模板

```text
测试日期：
测试关卡：
使用节点：
RenderTarget：
InstanceCount：
相机模式：

FOV：
Near：
Far：
CameraActor：
相机位置：
相机朝向：

EstimatedVisibleCount：
GpuVisibleCount：
BufferSizeBytes：
CpuCullDispatchTimeMs：
CpuDrawDispatchTimeMs：

画面观察：
日志摘录：
结论：
下一步：
```

## 本轮问题定位补充：PlayerPawn 不等于当前游戏相机

这次测试里出现了一个很典型的问题：蓝图传入的是 `Get Player Pawn`，但玩家真正看到的视角，不一定等于这个 Pawn 的 Actor 变换。

常见原因有两类：

- 相机挂在 `SpringArm + CameraComponent` 上，位置和朝向都和 Pawn Root 不一致
- 玩家视角来自 `PlayerCameraManager`，Pawn 自己甚至可能没有 `CameraComponent`

如果 frustum 仍然按 Pawn 的 `ActorLocation`、`ActorForwardVector`、`ActorUpVector` 去构造，就会出现下面这种现象：

- 玩家感觉自己正对着目标
- 但 GPU culling 结果仍然裁掉一部分实例
- `EstimatedVisibleCount` 会明显低于“肉眼预期”

这不是 culling shader 本身先错了，而是“参与 culling 的相机姿态”取错了。

所以这轮修正的重点是：

1. 如果传入 Actor 上存在 `CameraComponent`，优先使用它的世界位置、世界朝向、FOV 和 AspectRatio。
2. 如果没有 `CameraComponent`，但传入的是 `PlayerPawn`，则继续尝试从 `PlayerController -> PlayerCameraManager` 读取当前真实游戏相机。
3. 只有两者都取不到时，才退回到 Actor 自身的 transform。

这条经验后面要记住：

```text
做 camera-frustum culling 时，最容易错的不是 plane test，而是“你到底拿的是哪台相机”
```

## 本轮问题定位补充：AspectRatio 不能默认跟 RenderTarget 一致

这轮又确认了另一个很关键的问题：

- 玩家实际看到的是游戏视口
- 当前输出纹理 `RT_GPUComputeOutput` 是 `1024x1024`
- 如果直接拿 RenderTarget 的宽高比去构造 camera frustum，就会得到 `aspect=1.000`

但真实游戏视口通常是宽屏，不是正方形。这样会出现：

```text
玩家视野里还看得到的横向区域
在 GPU culling 看来已经跑到正方形 frustum 外面了
```

表现上就会像：

- 画面不是全部错误
- 但边缘区域会提前发黑或缺块
- `estimated-visible` 看起来偏少，但不是接近 0

所以这一轮的修正原则是：

1. 如果当前视角来自 `PlayerCameraManager`，优先使用真实游戏视口尺寸计算 `AspectRatio`
2. 如果 Actor 自带 `CameraComponent`，再使用 `CameraComponent->AspectRatio`
3. 最后才退回到 RenderTarget 的宽高比

后续记录测试日志时，要额外记住一项：

```text
aspect-source
```

只有当 `aspect-source` 合理时，`fov` 相同才真的有意义。

## 本轮问题定位补充：RT 预览平面不等于实例世界位置

`aspect` 修正后，仍然会在某些角度出现整块黑屏。

这轮确认的关键点是：

```text
RenderTarget 上看到的彩色格子只是一个 2D 预览结果
GPU frustum culling 裁剪的是 FGPUDrivenInstanceData.Position 里的世界坐标
```

也就是说，场景里那块显示 `RT_GPUComputeOutput` 的平面只是“屏幕”，不是实例真实所在的世界空间。
如果实例仍然固定生成在世界原点附近的 `X/Y` 网格上，而玩家相机实际看向的是另一块地面区域，就会出现：

- 玩家视口里能看到显示 RT 的平面
- 但 frustum culling 认为那批实例不在相机视锥内
- indirect draw 的实例数变成 0 或很少
- RT 被清成黑色，看起来像“视野内被错误剔除”

这不是 `FOV / Near / Far / AspectRatio` 单独能解决的问题，而是测试数据的空间语义不一致。

这轮修正做了两件事：

1. `Execute Camera Frustum Culled Indirect Draw` 不再使用固定在世界原点的测试实例，而是生成一批锚定在当前相机看向地面区域附近的测试实例。
2. `IndirectDrawInstances.usf` 不再假设实例 `XY` 从 `0` 开始，而是新增 `GridWorldMin`，用真实实例 `XY` bounds 把结果归一化到 RenderTarget。

这样 camera frustum 测试里的三件事终于对齐：

- 相机视锥来自当前玩家相机
- culling 使用的实例位置在当前相机附近
- RT 预览根据同一批实例的真实范围进行显示

后续如果要做真正场景级 culling，下一步应该给接口传入真实实例来源或测试网格 Actor transform，而不是继续依赖临时生成的 debug grid。

## 本轮问题定位补充：必须显式传入 RT 显示平面 Actor

继续测试后确认，仅仅把 debug instances 锚定到相机前方仍然不够。
因为用户真正要验证的是：

```text
场景里显示 RT_GPUComputeOutput 的那块平面，是否按照当前相机视锥正确显示可见区域
```

这就要求三个对象使用同一个空间语义：

- `PlayerCameraManager` 提供当前游戏相机视锥
- `PlaneActor` 提供 RT 显示平面的世界 bounds
- `FGPUDrivenInstanceData.Position` 必须铺在这个 `PlaneActor` 的 bounds 范围内

因此新增了一个更明确的蓝图节点：

```text
Execute Plane Camera Frustum Culled Indirect Draw
```

这个节点额外要求传入：

```text
PlaneActor
```

推荐传入当前场景里承载 `RT_GPUComputeOutput` 材质的那块平面 Actor。

这个节点会：

1. 从 `CameraActor` / `PlayerCameraManager` 构造 camera frustum。
2. 从 `PlaneActor->GetActorBounds(...)` 读取平面世界 bounds。
3. 在这个 bounds 内生成测试实例。
4. 使用 GPU frustum culling 裁剪这些实例。
5. 把可见结果写回 `RT_GPUComputeOutput`。

后续验证时，要优先使用这个节点，而不是旧的 `Execute Camera Frustum Culled Indirect Draw`。

新的关键日志是：

```text
GPUDrivenPipeline: Using plane actor ... for camera frustum culling target ...
```

如果这个日志里的 `PlaneActor` 不是当前画面中显示 RT 的那块平面，测试结论就不成立。

## 当前阶段结论

截至这一轮，`camera frustum -> GPU culling -> indirect draw -> RT preview` 这条链路已经具备下面这些能力：

- 可以从运行时玩家相机获取真实视角参数，而不是只使用固定测试平面。
- 可以优先使用真实游戏视口的 `AspectRatio`，不再被 `1024x1024` 的 RenderTarget 宽高比误导。
- 可以显式指定承载 `RT_GPUComputeOutput` 的 `PlaneActor`，让 culling 的实例分布范围和场景里的 RT 显示平面绑定到同一套世界空间语义上。
- 在实际测试中，RT 画面已经能够随相机视锥变化而发生可见区域裁剪，不再是完全失配或固定结果。

当前仍然保留一个明确的已知问题：

```text
剔除范围已经大体正确，但和玩家肉眼看到的可见范围还没有完全一一对应
```

它更像是“尺寸和投影匹配度还不够精确”，而不是“完全没有使用相机视锥”。

现阶段更准确的判断应该是：

```text
camera-driven frustum culling 已经打通
但仍然属于 correctness MVP，尚未达到和最终画面严格像素级一致
```

后续如果继续完善，优先方向应该是：

1. 让测试实例分布与 RT 平面局部坐标系建立更严格的映射关系，而不只依赖 Actor bounds。
2. 检查当前 plane bounds、instance radius、grid spacing 与最终 RT 预览尺寸之间的比例关系。
3. 如果要做严格验证，考虑把“被裁剪对象”从 debug instances 进一步替换成与场景平面一一对应的测试采样点或真实实例来源。
