# 2026-06-03 17:05 GPU Pass Demo UAV 崩溃排查

## 发生了什么

运行第一个 GPU pass demo 时，UE 报错：

```text
GPU Crashed or D3D Device Removed
```

日志中的关键错误是：

```text
Incompatible Transition State for Resource RT_GPUComputeOutput - D3D12_RESOURCE_STATE_UNORDERED_ACCESS requires D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS.
```

## 相关源码

- [ComputeShaderInterface.cpp](../../Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/ComputeShaderInterface.cpp)
- [SimpleComputeShader.usf](../../Plugins/GPUDrivenPipeline/Shaders/SimpleComputeShader.usf)

## 根因

`ComputeShaderInterface.cpp` 中的 compute pass 需要把 RenderTarget 作为 UAV 写入。UAV 的意思是 `Unordered Access View`，也就是允许 compute shader 直接写 GPU 资源。

问题在于：`RT_GPUComputeOutput` 这个 RenderTarget 资产最初没有启用 UAV 支持，但代码却试图把它切换到 `UAVCompute` 状态并写入。

D3D12 对资源创建标志要求很严格。一个资源如果创建时没有 `ALLOW_UNORDERED_ACCESS` 标志，后续就不能强行当 UAV 使用。

## 修复方式

修复后的策略是：如果 RenderTarget 没有启用 UAV 支持，C++ 直接打印 warning 并返回，不再继续 dispatch。

核心思路：

```cpp
if (!OutputRenderTarget->bSupportsUAV)
{
    UE_LOG(...);
    return;
}
```

这样可以避免非法 GPU resource transition，保护 Editor 不崩溃。

## 学到的知识点

- RenderTarget 能显示，不代表它一定能被 compute shader 写。
- Compute shader 写资源通常需要 UAV。
- UAV 支持必须在资源创建时确定，不能总是在运行时补救。
- GPU crash 不一定是 shader 算法错，也可能是资源状态或资源创建 flags 错。

## 结论

图形开发里，资源配置和 shader 代码同样重要。对于 compute pass，先确认资源是否支持 UAV，再 dispatch，是一个必要的安全检查。
