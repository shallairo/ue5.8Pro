# Compute Shader Validation Test

## Purpose

Validate that the `GPUDrivenPipeline` plugin can dispatch `SimpleComputeShader.usf` and write a visible gradient into a render target.

## Requirements

- UE5.8 Editor.
- `GPUDrivenPipeline` plugin enabled in `pro.uproject`.
- Project running with a shader model that supports SM5 or higher.
- A GPU/RHI configuration that supports UAV writes to render targets.
- The render target asset must be created with UAV support enabled.

## Test Assets

Create these assets if they do not already exist:

- Level: `Content/GPUDrivenDemo/Maps/L_GPUComputePassDemo`
- Render target: `Content/GPUDrivenDemo/Textures/RT_GPUComputeOutput`
- Material: `Content/GPUDrivenDemo/Materials/M_GPUComputeOutput`
- Blueprint actor: `Content/GPUDrivenDemo/Blueprints/BP_GPUComputePassDemo`

## Setup

1. Create an empty level and save it as `L_GPUComputePassDemo`.
2. Create a `TextureRenderTarget2D` named `RT_GPUComputeOutput`.
3. Set the render target size to `1024 x 1024`.
4. Enable UAV support on the render target asset.
5. Create `M_GPUComputeOutput`.
6. Set the material Shading Model to `Unlit`.
7. Add a texture sample using `RT_GPUComputeOutput`.
8. Connect the texture sample RGB output to `Emissive Color`.
9. Add a plane to the level and apply `M_GPUComputeOutput`.
10. Create `BP_GPUComputePassDemo` as an Actor blueprint.
11. On BeginPlay, call `Execute Simple Compute Shader` with `RT_GPUComputeOutput`.

## Expected Result

When PIE starts, the plane should show a stable gradient:

- Red increases from left to right.
- Green increases from top to bottom.
- Blue remains constant around `0.5`.

The Output Log should include a message similar to:

```text
GPUDrivenPipeline: Dispatched SimpleComputeShader ...
```

## Failure Checks

Black output:

- Confirm the blueprint event is firing.
- Confirm the material samples the correct render target.
- Confirm the render target resource is initialized.

No dispatch log:

- Confirm the plugin is enabled.
- Confirm the blueprint node is from `GPUDrivenPipeline`.
- Rebuild the Editor target if the C++ API is missing.
- Confirm `RT_GPUComputeOutput` was created with UAV support enabled.

Shader compile failure:

- Confirm shader directory mapping is registered.
- Confirm `SimpleComputeShader.usf` is under `Plugins/GPUDrivenPipeline/Shaders/`.
- Restart the editor after plugin or shader mapping changes.

## Measurement Notes

`GetLastExecutionTime()` currently reports CPU-side dispatch timing only. Use Unreal GPU profiling tools for real GPU time:

- `stat unit`
- `stat gpu`
- RenderDoc
- PIX for Windows
- Unreal Insights with GPU tracing
