# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a UE5.8 source-built C++ project for GPU-driven rendering experiments. The main game module (`Source/pro/`) is intentionally minimal. All active development happens in the `GPUDrivenPipeline` runtime plugin at `Plugins/GPUDrivenPipeline/`.

## Build System

**Engine:** UE5.8 source-built, identified by `{6DBFD1DD-4FAE-DEE6-7ED8-2DB9153CD0A2}` in `pro.uproject`.

**Targets:**
- `Source/pro.Target.cs` — Runtime game target (`TargetType.Game`)
- `Source/proEditor.Target.cs` — Editor target (`TargetType.Editor`)

**Build workflow** (run from the UE source directory, not this project):

```powershell
# Generate project files
Engine\Build\BatchFiles\RunUBT.bat -ProjectFiles -Project="<path-to>\pro.uproject"

# Build editor target
Engine\Build\BatchFiles\Build.bat -Project="<path-to>\pro.uproject" proEditor Win64 Development
```

Do not invoke builds directly — notify the user when compilation is needed and let them perform it.

**Plugin dependencies** (`GPUDrivenPipeline.Build.cs`): `Core`, `CoreUObject`, `Engine`, `Renderer`, `RenderCore`, `RHI`, `RHICore` (public); `Slate`, `SlateCore`, `Projects` (private).

**Main module dependencies** (`pro.Build.cs`): `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`.

## Rendering Configuration

Targeted for desktop rendering experiments on Windows:
- **RHI:** DX12 (`DefaultGraphicsRHI_DX12`)
- **Shader target:** SM6 (`PCD3D_SM6`)
- **Features:** Lumen GI, Lumen reflections, ray tracing, virtual shadow maps, Substrate enabled
- **Hardware class:** Desktop / Maximum performance

## Architecture: GPUDrivenPipeline Plugin

The plugin is the core of all GPU-driven rendering work. It loads at `PostConfigInit` so it's available early.

### Directory Layout

```
Plugins/GPUDrivenPipeline/
├── GPUDrivenPipeline.uplugin          # Plugin descriptor (Runtime, PostConfigInit)
├── Shaders/
│   └── SimpleComputeShader.usf        # HLSL compute shader source
└── Source/GPUDrivenPipeline/
    ├── GPUDrivenPipeline.Build.cs     # Module build rules
    ├── Public/
    │   ├── GPUDrivenPipelineModule.h  # IModuleInterface for plugin lifecycle
    │   ├── SimpleComputeShader.h      # FGlobalShader subclass declaration
    │   └── ComputeShaderInterface.h   # Blueprint-callable UBlueprintFunctionLibrary
    └── Private/
        ├── GPUDrivenPipelineModule.cpp  # Shader directory registration
        ├── SimpleComputeShader.cpp      # IMPLEMENT_GLOBAL_SHADER
        └── ComputeShaderInterface.cpp   # Render-thread dispatch logic
```

### Key Classes and Data Flow

1. **`FGPUDrivenPipelineModule`** (`GPUDrivenPipelineModule.h/.cpp`) — Plugin entry point. On startup, registers the `Shaders/` directory via `AddShaderSourceDirectoryMapping` with the virtual path `/Plugin/GPUDrivenPipeline`.

2. **`FSimpleComputeShader`** (`SimpleComputeShader.h/.cpp`) — An `FGlobalShader` subclass that declares:
   - `SHADER_PARAMETER_UAV(RWTexture2D<float4>, OutputTexture)` — UAV for render target output
   - `SHADER_PARAMETER(FVector2f, TextureSize)` — texture dimensions
   - Thread group size: 8×8×1 (set via `THREADGROUP_SIZE_X`/`_Y` compile defines)
   - Shader file mapped as `/Plugin/GPUDrivenPipeline/SimpleComputeShader.usf`, entry point `MainCS`, type `SF_Compute`
   - Compiles for SM5+ feature levels only

3. **`UComputeShaderInterface`** (`ComputeShaderInterface.h/.cpp`) — Blueprint-callable API:
   - `ExecuteSimpleComputeShader(UTextureRenderTarget2D*)` — Validates input (non-null, valid size, UAV support enabled), enqueues a render command that dispatches the compute shader, and records CPU dispatch timing
   - `GetLastExecutionTime()` — Returns last CPU-side dispatch time in ms (atomic float, thread-safe)

4. **Dispatch flow:**
   - Game thread validates `UTextureRenderTarget2D` (must have `bSupportsUAV = true`)
   - `ENQUEUE_RENDER_COMMAND` sends work to render thread
   - Render thread: acquires RHI UAV, transitions resource (`SRVMask → UAVCompute`), dispatches via `FComputeShaderUtils::Dispatch`, transitions back (`UAVCompute → SRVMask`), logs timing

### Shader Development

- **Shader sources** live in `Plugins/GPUDrivenPipeline/Shaders/`
- **Virtual path** for `IMPLEMENT_GLOBAL_SHADER`: `/Plugin/GPUDrivenPipeline/<shader>.usf`
- Route uses the `[numthreads(8, 8, 1)]` compute shader model with `RWTexture2D<float4>` UAV output
- Thread group dimensions are set both in the `.usf` `[numthreads]` attribute and as compile-time defines in `ModifyCompilationEnvironment`

## Language Convention

- **文档和开发计划：** 所有 `docs/` 下的文档（包括 `docs/plan/` 计划和 `docs/learning/` 学习笔记）必须使用**中文**撰写。
- **代码注释：** 所有 `.h`、`.cpp`、`.usf`、`.cs` 等源代码文件中的注释必须使用**英文**。
- **CLAUDE.md 和 README.md：** 使用英文（项目级技术文档，面向工具和协作者）。

## Documentation Conventions

- `docs/plan/` — 后续开发计划，中文撰写，命名规则见下表。
- `docs/learning/` — 每次代码开发后的知识点和难点讲解，中文撰写，命名规则见下表。

Entry point: `docs/index.md`. Follow these naming rules:

| Category | Location | Pattern | Example |
|----------|----------|---------|---------|
| Development plans | `docs/plan/` | `plan-YYYY-MM-DD-topic.md` | `plan-2026-06-03-gpu-driven-execution.md` |
| Learning logs | `docs/learning/` | `YYYY-MM-DD-HHMM-topic.md` | `2026-06-03-1705-gpu-pass-demo-uav-crash.md` |
| Guides | `docs/` | `guide-topic.md` | `guide-gpu-pass-demo-v1.md` |
| Test procedures | `docs/` | `test-topic.md` | `test-compute-shader-validation.md` |
| Results/reports | `docs/` | `report-YYYY-MM-DD-topic.md` | `report-2026-06-03-compute-pass-baseline.md` |
| Technical notes | `docs/` | `note-topic.md` | — |
| Archived material | `docs/` | `archive-topic.md` | — |

Use lowercase kebab-case for all document names. Keep docs aligned with current code state — update stale documents promptly.

Every completed task should include a teaching-oriented explanation: what changed, why, key engine/rendering concepts involved, and important debugging pitfalls. When learning value merits it, write a learning log under `docs/learning/`.

## Development Notes

- Do not treat old planning text as truth if it conflicts with current code.
- Prefer small, verifiable GPU rendering milestones.
- Distinguish CPU dispatch timing from GPU elapsed time — the current `GLastExecutionTimeMs` measures CPU dispatch only.
- Never commit generated directories: `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`.
- Never modify `pcgDoc/` unless explicitly asked.
- Notify the user when compilation is needed; let them perform the compile step.
