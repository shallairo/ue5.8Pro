# GPU Pass Demo v1 Guide

## Goal

Create a minimal UE Editor demo that dispatches `SimpleComputeShader.usf` on `BeginPlay`, writes a red-green UV gradient into a render target, and displays the result on a plane.

This guide uses Blueprint assets for the demo scene and keeps C++ limited to the reusable plugin function library.

## Code Entry Point

Use the Blueprint node:

```text
GPU Driven Pipeline > Execute Simple Compute Shader
```

Input:

- `Output Render Target`: `RT_GPUComputeOutput`

Optional debug node:

```text
GPU Driven Pipeline > Get Last Compute Dispatch Time
```

This reports CPU-side dispatch time only.

## Asset Layout

Create these assets in the UE Editor:

```text
Content/GPUDrivenDemo/Maps/L_GPUComputePassDemo
Content/GPUDrivenDemo/Textures/RT_GPUComputeOutput
Content/GPUDrivenDemo/Materials/M_GPUComputeOutput
Content/GPUDrivenDemo/Blueprints/BP_GPUComputePassDemo
```

## Render Target

Create `RT_GPUComputeOutput` as a `TextureRenderTarget2D`.

Settings:

- Size X: `1024`
- Size Y: `1024`
- Format: `RTF RGBA8`
- Enable UAV support on the render target asset before running the demo.

If the render target is not created with UAV support, the compute pass will now refuse to run and print a warning instead of attempting an unsafe GPU transition.

## Material

Create `M_GPUComputeOutput`.

Settings:

- Material Domain: `Surface`
- Shading Model: `Unlit`

Graph:

1. Add `TextureSample`.
2. Assign `RT_GPUComputeOutput`.
3. Connect RGB to `Emissive Color`.

## Blueprint Actor

Create `BP_GPUComputePassDemo` as an Actor blueprint.

Components:

- Add `StaticMeshComponent`.
- Set Static Mesh to the Engine plane mesh.
- Assign `M_GPUComputeOutput`.
- Scale the plane large enough to view clearly.

Event Graph:

1. Add `Event BeginPlay`.
2. Call `Execute Simple Compute Shader`.
3. Pass `RT_GPUComputeOutput`.
4. Add `Print String` with:

```text
GPU pass dispatched
```

Optional:

1. Call `Get Last Compute Dispatch Time`.
2. Convert the float to string.
3. Print it as CPU dispatch ms.

## Level Setup

1. Create or open `L_GPUComputePassDemo`.
2. Drag `BP_GPUComputePassDemo` into the level.
3. Add a camera or position the viewport so the plane is visible.
4. Save all assets.

## Run

1. Build `Development Editor | Win64`.
2. Open the project in UE5.8 Editor.
3. Open `L_GPUComputePassDemo`.
4. Click Play.

Expected result:

- The plane shows a stable red-green gradient.
- Screen message shows `GPU pass dispatched`.
- Output Log includes:

```text
GPUDrivenPipeline: Dispatched SimpleComputeShader ...
```

## Validation Matrix

Run the demo with these render target sizes:

- `512 x 512`
- `1024 x 1024`
- `2048 x 2048`

Pass criteria:

- The gradient appears at each size.
- Repeated Play/Stop does not crash.
- No shader compile or UAV creation warnings appear in Output Log.

## Troubleshooting

Black output:

- Confirm the Blueprint `BeginPlay` path reaches the print node.
- Confirm `RT_GPUComputeOutput` is the render target passed into the compute node.
- Confirm `M_GPUComputeOutput` samples the same render target.

Missing Blueprint node:

- Rebuild the Editor target.
- Confirm `GPUDrivenPipeline` is enabled in `pro.uproject`.
- Restart the editor after rebuilding.

Shader failure:

- Confirm `SimpleComputeShader.usf` is under `Plugins/GPUDrivenPipeline/Shaders/`.
- Confirm the plugin module loads before shader compilation.
- Check Output Log for `GPUDrivenPipeline` warnings.
