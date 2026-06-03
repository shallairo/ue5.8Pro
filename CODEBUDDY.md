# CODEBUDDY.md

This file provides project guidance for AI coding assistants working in this repository.

## Project Overview

This is a UE5.8 source-built C++ project focused on a GPU-driven rendering prototype.

The main game module is intentionally minimal. Most active development is in:

```text
Plugins/GPUDrivenPipeline/
```

## Current Architecture

- `Source/pro/`: Minimal primary game module.
- `Plugins/GPUDrivenPipeline/`: Runtime plugin for GPU rendering experiments.
- `Plugins/GPUDrivenPipeline/Shaders/`: Plugin shader source directory.
- `docs/`: Active documentation, plans, tests, and guides.

The current plugin already contains:

- Shader directory mapping during module startup.
- `SimpleComputeShader.usf`.
- A blueprint-callable compute shader interface.
- Render-thread dispatch into a UAV-capable render target.

## Build System

Primary build system: Unreal Build Tool.

Targets:

- `Source/pro.Target.cs`: Game runtime target.
- `Source/proEditor.Target.cs`: Editor target.

Main module dependencies:

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput"
});
```

Plugin dependencies include rendering modules such as `Renderer`, `RenderCore`, `RHI`, and `RHICore`.

## Rendering Configuration

The project is configured for desktop rendering experiments:

- DX12 on Windows.
- SM6 shader target.
- Lumen GI and reflections.
- Ray tracing enabled.
- Virtual shadow maps enabled.
- Substrate enabled.

## Documentation Rules

Use `docs/index.md` as the documentation entry point.

Future development plans must live under:

```text
docs/plan/
```

Plan naming rule:

```text
docs/plan/plan-YYYY-MM-DD-topic.md
```

Learning log location:

```text
docs/learning/
```

Learning log naming rule:

```text
docs/learning/YYYY-MM-DD-HHMM-topic.md
```

Use:

- `guide-topic.md` for setup and operation guides.
- `test-topic.md` for repeatable validation procedures.
- `report-YYYY-MM-DD-topic.md` for measured results.
- `note-topic.md` for lightweight technical notes.
- `archive-topic.md` for inactive historical material.

Keep documents concise and aligned with the current repository state.

Every completed development task should also produce a short teaching-oriented explanation for the user, covering:

- what changed,
- why it changed,
- the key engine, rendering, or code concepts involved,
- the most important debugging or implementation pitfalls.

When the task produces meaningful learning value, add a learning log under `docs/learning/` using the naming rule above. The title should encode time plus topic so the logs remain chronological and easy to scan.

Do not put active development plans at the root of `docs/`; place them under `docs/plan/`.

## Development Notes

- Do not treat old planning text as truth if it conflicts with current code.
- Prefer small, verifiable GPU rendering milestones.
- For timing, distinguish CPU dispatch timing from real GPU elapsed time.
- Avoid committing generated directories such as `Binaries/`, `Intermediate/`, `Saved/`, and `DerivedDataCache/`.
- When a task finishes, act as both implementer and teacher: explain the core knowledge in plain language instead of stopping at code delivery.
- If a task requires compilation, notify the user and let the user perform the compile step.
- Do not modify `pcgDoc/` unless the user explicitly asks for it.
