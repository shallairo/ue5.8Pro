# UE5.8 GPU-Driven Pipeline Prototype

This is a UE5.8 source-built C++ project for experimenting with GPU-driven rendering techniques.

The current implementation focuses on the `GPUDrivenPipeline` runtime plugin. The plugin includes a basic global compute shader path that writes a gradient into a render target through a blueprint-callable interface.

## Current Focus

- UE5.8 source-built project.
- DX12 / SM6 desktop rendering configuration.
- Runtime plugin: `GPUDrivenPipeline`.
- First working compute shader dispatch path.
- Next milestone: validation scene, benchmark baseline, GPU-visible instance data, and indirect draw MVP.

## Project Structure

- `Config/`: Unreal project configuration.
- `Content/`: Editor-created assets.
- `Plugins/GPUDrivenPipeline/`: GPU-driven rendering prototype plugin.
- `Source/pro/`: Minimal main game module.
- `docs/`: Active project documentation and development plans.
- `pro.uproject`: Unreal project descriptor.

## Documentation

Start with [docs/index.md](docs/index.md).

All future development plans should be created under `docs/` using:

```text
plan-YYYY-MM-DD-topic.md
```

Example:

```text
plan-2026-06-10-indirect-draw-mvp.md
```

## Setup Notes

1. Use the UE5.8 source-built engine configured for this project.
2. Generate Visual Studio project files from `pro.uproject`.
3. Build the `Development Editor | Win64` target.
4. Open the project in the Unreal Editor.

Generated directories such as `Binaries/`, `Intermediate/`, `Saved/`, and `DerivedDataCache/` should remain out of version control.
