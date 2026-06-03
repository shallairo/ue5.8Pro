# GPU-Driven Execution Plan

## Current State

The project is a UE5.8 source-built C++ project focused on a `GPUDrivenPipeline` runtime plugin.

The main `pro` game module is still minimal. The active rendering work lives in the plugin:

- Shader directory mapping is registered during plugin startup.
- `SimpleComputeShader.usf` exists under the plugin shader directory.
- `UComputeShaderInterface::ExecuteSimpleComputeShader()` dispatches a real compute pass on the render thread.
- The compute pass writes a red-green UV gradient into a UAV-capable render target.
- `GetLastExecutionTime()` reports CPU-side dispatch timing, not GPU elapsed time.

## Objective

Move from a working compute shader validation pass to a small GPU-driven rendering prototype with measurable baseline data.

## Success Criteria

- The compute shader pass can be triggered repeatedly in Editor or PIE without crashes.
- The output render target visibly shows the expected gradient.
- A reusable validation level and assets exist under `Content/`.
- A baseline benchmark can be captured with `stat unit`, `stat gpu`, and `stat scenerendering`.
- The plugin contains the first reusable data structures for GPU-visible instance data.
- The next prototype step toward indirect drawing is clearly implemented or isolated behind a small interface.

## Phase 1: Compute Pass Validation

Tasks:

- Create a dedicated validation level.
- Create a render target with UAV support.
- Create a material that samples the render target.
- Create a blueprint actor that calls `ExecuteSimpleComputeShader()`.
- Record expected visual output and basic log behavior.

Deliverables:

- `Content/Maps/TestLevel_ComputeShader`
- `RT_ComputeShaderOutput`
- `M_ComputeShaderTest`
- `BP_ComputeShaderTest`
- A short validation result report once tested.

## Phase 2: Benchmark Baseline

Tasks:

- Define repeatable test settings: resolution, RHI, editor/PIE mode, scalability level, and GPU model.
- Measure empty scene baseline.
- Measure compute pass scene baseline.
- Record CPU frame time, GPU frame time, draw calls, and dispatch log timing.

Deliverable:

- `report-YYYY-MM-DD-compute-pass-baseline.md`

## Phase 3: GPU Data Path

Tasks:

- Define instance metadata needed for the first rendering MVP.
- Add structured buffer creation and lifetime handling.
- Add a small CPU-to-GPU upload path.
- Keep the data path independent from the current gradient pass where practical.

Initial data candidates:

- World transform or compact transform representation.
- Bounds center and radius.
- Instance visibility flag.
- Draw or mesh identifier.

## Phase 4: Indirect Draw MVP

Tasks:

- Prepare indirect draw arguments.
- Connect visible instance count or draw metadata to the rendering path.
- Render a simple repeated mesh scene.
- Compare CPU cost with the non-indirect baseline.

Non-goals for the first MVP:

- Full Hi-Z occlusion culling.
- Full GPU LOD selection.
- Production-quality renderer integration.
- Final art scene polish.

## Documentation Rules For Future Plans

Every new development plan must be created under `docs/plan/` using:

```text
plan-YYYY-MM-DD-topic.md
```

Each plan should include:

- Current State
- Objective
- Success Criteria
- Tasks
- Deliverables
- Non-goals

When a plan becomes obsolete, either update it to match reality or rename it as `archive-topic.md`.
