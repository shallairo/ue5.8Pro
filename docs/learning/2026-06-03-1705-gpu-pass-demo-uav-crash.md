# 2026-06-03 17:05 GPU Pass Demo UAV Crash

## What Happened

We built the first visible GPU pass demo and hit a `GPU Crashed or D3D Device Removed` error when entering PIE.

## Root Cause

The compute pass tried to transition and write to `RT_GPUComputeOutput` as a UAV resource, but the render target had not been created with UAV support enabled.

The key log message was:

```text
Incompatible Transition State for Resource RT_GPUComputeOutput - D3D12_RESOURCE_STATE_UNORDERED_ACCESS requires D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS.
```

## Key Concepts

- `RenderTarget` is the texture object we want to write into.
- `UAV` means Unordered Access View, which allows compute shaders to write directly into a resource.
- A resource must be created with UAV support before the GPU can legally use it in UAV state.
- A compute shader dispatch succeeding at the CPU call site does not guarantee the GPU resource setup is valid.

## Fix

- Stop trying to auto-upgrade a render target to UAV mode at runtime.
- Fail safely in C++ if the asset does not already support UAV writes.
- Require UAV support to be enabled on the render target asset itself.

## Takeaways

- GPU crash logs often point to resource state mismatches, not just shader logic bugs.
- For UE compute passes, asset configuration matters as much as HLSL code.
- Safe early validation is better than forcing a bad GPU transition and crashing the editor.
