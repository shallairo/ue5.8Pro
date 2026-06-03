# Demo Cleanup And Baseline Plan

## Current State

The first GPU pass demo is working:

- `BP_GPUComputePassDemo` calls the compute pass during `BeginPlay`.
- `RT_GPUComputeOutput` receives the shader output.
- `M_GPUComputeOutput` displays the render target on a plane.
- PIE shows the expected gradient output.
- Output Log confirms `SimpleComputeShader` dispatch.

The demo is functional but still has prototype cleanup work:

- The material folder is currently named `Martirals` instead of `Materials`.
- The map asset is currently named `testMap.umap` instead of a project-style level name.
- The screen message can repeat heavily during PIE.
- No baseline performance report has been recorded yet.

## Objective

Turn the working GPU pass prototype into a clean, repeatable validation demo and capture the first baseline data for later GPU-driven rendering work.

## Success Criteria

- Demo assets use consistent names and paths.
- The demo level opens directly and contains one intentional `BP_GPUComputePassDemo` instance.
- PIE shows the gradient without GPU crash or repeated message spam.
- Output Log still records the compute dispatch.
- A baseline report exists under `docs/`.
- A learning log explains the concepts used in this cleanup stage.

## Phase 1: Asset Cleanup In UE Editor

User operations:

- Rename `Content/GPUDrivenDemo/Martirals` to `Content/GPUDrivenDemo/Materials` using the UE Content Browser.
- Rename `Content/GPUDrivenDemo/Maps/testMap.umap` to `Content/GPUDrivenDemo/Maps/L_GPUComputePassDemo` using the UE Content Browser.
- Fix redirectors in `Content/GPUDrivenDemo/`.
- Open `L_GPUComputePassDemo`.
- Confirm the level contains exactly one `BP_GPUComputePassDemo` actor.
- Confirm the actor uses `M_GPUComputeOutput`.
- Confirm `M_GPUComputeOutput` samples `RT_GPUComputeOutput`.
- Confirm `RT_GPUComputeOutput` has UAV support enabled.

Assistant responsibilities:

- Do not rename `.uasset` or `.umap` files directly through the filesystem.
- Update documentation paths after the UE Editor rename is complete.
- Check Git status after the user completes the asset cleanup.

## Phase 2: Reduce Demo Message Noise

Preferred implementation:

- Keep the compute pass triggered by `BeginPlay`.
- Remove or reduce repeated `Print String` behavior in the Blueprint.
- Keep the C++ `UE_LOG` dispatch message as the main verification signal.

Acceptable options:

- Remove the Blueprint `Print String` entirely.
- Or keep one short screen print if the Blueprint is confirmed to execute only once.

Assistant responsibilities:

- If Blueprint-only changes are needed, guide the user through the UE Editor steps.
- If C++ helper changes are needed, make them in the plugin and then notify the user to compile.

## Phase 3: Baseline Capture

User operations in PIE:

- Open `L_GPUComputePassDemo`.
- Run PIE at the normal editor viewport size.
- Open the console or Output Log command input.
- Run:

```text
stat unit
stat gpu
stat scenerendering
```

Capture or report:

- CPU frame time from `stat unit`.
- GPU frame time from `stat unit` or `stat gpu`.
- Draw call count from `stat scenerendering`.
- Render target size.
- Dispatch log line including group count and CPU dispatch time.

Assistant responsibilities:

- Create `docs/report-2026-06-03-compute-pass-baseline.md` from the captured values.
- Explain which numbers are useful now and which are only rough editor-mode references.

## Phase 4: Learning Log

Create a learning log under:

```text
docs/learning/
```

Suggested filename:

```text
2026-06-03-HHMM-gpu-pass-demo-cleanup-baseline.md
```

Topics to cover:

- UE asset rename safety and redirectors.
- Why `.uasset` paths should be changed through the editor.
- What a baseline report is for.
- Difference between visual validation and performance validation.
- Difference between CPU dispatch time and GPU execution time.

## Non-Goals

- Do not implement indirect drawing yet.
- Do not add GPU culling yet.
- Do not add Tick-based continuous dispatch yet.
- Do not modify `pcgDoc/`.
- Do not compile from the assistant side; notify the user when compilation is needed.

## Next Follow-Up After This Plan

After cleanup and baseline are complete, create the next plan for a first GPU data path:

```text
docs/plan/plan-YYYY-MM-DD-gpu-instance-data-path.md
```

That next plan should introduce structured buffers and instance metadata without jumping directly into full indirect rendering.
