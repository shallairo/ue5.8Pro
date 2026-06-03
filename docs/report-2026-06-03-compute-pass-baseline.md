# Compute Pass Baseline Report

## Summary

Date: 2026-06-03

Scenario: `L_GPUComputePassDemo`

Render target: `RT_GPUComputeOutput`

Render target size: `1024 x 1024`

Shader: `SimpleComputeShader.usf`

Result: the first GPU compute pass demo is visually working and produces the expected render target output. The pass dispatches successfully and writes to `RT_GPUComputeOutput`.

## Dispatch Log

Observed log:

```text
SimpleComputeShader to RT_GPUComputeOutput (1024x1024, groups=128,128,1, cpu-dispatch=0.004 ms)
```

Interpretation:

- `groups=128,128,1` is correct for a `1024 x 1024` target with an `8 x 8 x 1` thread group size.
- `cpu-dispatch=0.004 ms` is CPU-side command submission timing.
- This value is not the real GPU execution time of the compute shader.

## Frame Baseline

Source: `stat unit`

| Metric | Value |
| --- | ---: |
| Frame | 8.34 ms |
| Game | 6.46 ms |
| Draw | 4.40 ms |
| RHIT | 2.40 ms |
| GPU Time | 4.89 ms |
| Draws | 149 |
| Primitives | 13.0 K |

## GPU Timing Snapshot

Source: `stat gpu`

Graphics queue highlights:

| Metric | Avg | Max | Min |
| --- | ---: | ---: | ---: |
| Queue Total | 5.73 ms | 19.14 ms | 4.09 ms |
| Postprocessing | 1.72 ms | 4.75 ms | 1.33 ms |
| TemporalSuperResolution | 1.39 ms | 4.11 ms | 1.07 ms |
| RenderDeferredLighting | 0.94 ms | 3.97 ms | 0.56 ms |
| Lights | 0.46 ms | 1.98 ms | 0.27 ms |

Compute queue highlights:

| Metric | Avg | Max | Min |
| --- | ---: | ---: | ---: |
| Queue Total | 3.40 ms | 10.91 ms | 2.37 ms |
| LumenScreenProbeGather | 1.60 ms | 5.29 ms | 1.11 ms |
| LumenSceneLighting | 0.65 ms | 1.76 ms | 0.40 ms |
| Postprocessing | 0.51 ms | 2.11 ms | 0.19 ms |
| TemporalSuperResolution | 0.50 ms | 2.10 ms | 0.19 ms |

Important note: the current compute shader pass is not isolated in this view. The compute queue is dominated by engine-level features such as Lumen and TSR, so these numbers are useful as scene baseline data, not as direct shader cost.

## Scene Rendering Snapshot

Source: `stat scenerendering`

| Stat | Call Count | Inclusive Avg | Inclusive Max | Exclusive Avg | Exclusive Max |
| --- | ---: | ---: | ---: | ---: | ---: |
| RenderViewFamily | 1 | 3.08 ms | 3.85 ms | 1.36 ms | 1.73 ms |
| DeferredShadingSceneRenderer Lighting | 2 | 0.23 ms | 0.34 ms | 0.13 ms | 0.19 ms |
| InitViews | 1 | 0.16 ms | 0.30 ms | 0.06 ms | 0.21 ms |
| Lighting drawing | 1 | 0.06 ms | 0.10 ms | 0.00 ms | 0.00 ms |
| Dynamic shadow setup | 2 | 0.05 ms | 0.17 ms | 0.04 ms | 0.16 ms |
| Translucency drawing | 2 | 0.03 ms | 0.06 ms | 0.03 ms | 0.06 ms |
| Base pass drawing | 1 | 0.02 ms | 0.04 ms | 0.02 ms | 0.04 ms |

## What This Baseline Proves

This baseline proves:

1. The compute shader dispatch path is functional.
2. The render target is UAV-capable and receives shader output.
3. The demo scene is stable enough for repeatable manual validation.
4. The project now has a first measurable reference point before GPU-driven instance data and indirect rendering work.

## What This Baseline Does Not Prove Yet

This baseline does not yet prove:

1. Real GPU elapsed time for `SimpleComputeShader`.
2. Any CPU submission reduction from GPU-driven rendering.
3. Any draw-call reduction from indirect drawing.
4. Any visibility optimization from GPU culling.

Those require the next stages: GPU-visible instance buffers, indirect draw arguments, and then a culling pass that affects draw submission.

## Follow-Up

Recommended next development plan:

```text
docs/plan/plan-2026-06-03-gpu-instance-data-path.md
```

The next stage should introduce structured instance metadata and a CPU-to-GPU upload path without jumping directly into full indirect rendering.
