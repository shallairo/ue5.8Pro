# Next Phase GPU-Driven Execution Plan

## 1. Document Goal

This document defines the next execution phase for the current UE project.
It is written against the current repository state, where:

- The main game module is still minimal.
- The `GPUDrivenPipeline` plugin already exists and compiles.
- Shader directory mapping is in place.
- `SimpleComputeShader.usf` exists.
- `ExecuteSimpleComputeShader()` is still a placeholder and does not dispatch a real compute pass.
- The project does not yet contain a real benchmark scene or a GPU-driven draw path.

The purpose of this phase is to move the project from "plugin skeleton + planning documents" to "runnable rendering prototype with measurable baseline data".

## 2. Phase Positioning

### 2.1 Core Theme

The next phase should focus on:

1. Turning the current compute shader path into a real executable render pipeline step.
2. Building a minimum visual validation loop inside UE.
3. Establishing the first GPU-driven rendering MVP based on indirect draw preparation.
4. Creating a benchmark and debugging workflow that can support later optimization work.

### 2.2 Non-Goals For This Phase

The following items should not be treated as primary goals in this phase:

1. Full Hi-Z occlusion culling.
2. Full GPU LOD selection system.
3. Particle simulation pipeline.
4. Cross-vendor compatibility polishing.
5. Final presentation-quality art scene.

These can be planned after the first end-to-end prototype is stable.

## 3. Strategic Objective

At the end of this phase, the project should be able to demonstrate:

1. A real compute shader dispatch from UE code to GPU.
2. A visible render target result that proves the custom shader pipeline works.
3. A first structured GPU data path for instance-oriented rendering.
4. A minimum indirect rendering prototype or a very clear pre-indirect buffer preparation layer.
5. Reproducible profiling and benchmark data.

## 4. Deliverables

The planned output of this phase is:

1. A working compute pass path in the plugin.
2. A dedicated test map and validation assets under `Content/`.
3. A benchmark mode and a visual mode with clear measurement rules.
4. A first batch of GPU-driven core data structures.
5. A written result summary document with baseline metrics and next-step observations.

## 5. Success Criteria

This phase is considered complete only if the following criteria are met:

1. `ExecuteSimpleComputeShader()` performs a real GPU dispatch instead of only logging.
2. The output render target shows stable, correct shader output in editor or PIE.
3. The test scene can be launched repeatedly without crashes or obvious resource leaks.
4. At least one benchmark scene exists and can be measured with `stat unit`, `stat gpu`, and `stat scenerendering`.
5. The codebase contains a first reusable data path for instance data and GPU-visible draw metadata.
6. A before/after baseline has been recorded, even if the optimization gain is still small.

## 6. Work Breakdown

## 6.1 Workstream A: Make The Compute Shader Path Real

### Goal

Convert the current placeholder blueprint-callable interface into a real RDG/RHI-backed compute shader execution path.

### Current Status

- Shader registration exists.
- Shader declaration exists.
- Shader source exists.
- Blueprint function exists.
- Actual dispatch logic does not exist yet.

### Main Tasks

1. Implement render-thread safe execution entry for `ExecuteSimpleComputeShader()`.
2. Retrieve the render target resource correctly from `UTextureRenderTarget2D`.
3. Create the UAV binding needed by `FSimpleComputeShader`.
4. Build a proper shader parameter struct population path.
5. Dispatch the compute shader with thread group counts derived from render target size.
6. Add resource state handling and synchronization suitable for UE's render graph path.
7. Record execution timing in a way that is meaningful for later profiling.
8. Add robust null checks and failure logs for invalid render targets or missing resources.

### Suggested Files To Touch

1. `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/ComputeShaderInterface.cpp`
2. `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/ComputeShaderInterface.h`
3. `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/SimpleComputeShader.h`
4. `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/SimpleComputeShader.cpp`

### Acceptance Criteria

1. Blueprint or C++ can call the function successfully.
2. The render target visibly updates with the shader result.
3. Output log reports meaningful execution info.
4. No editor hitch, crash, or stale resource error is observed during repeated calls.

## 6.2 Workstream B: Build The Minimum Validation Scene

### Goal

Create the smallest possible content setup that proves the compute path works visually and repeatedly.

### Required Assets

1. `RT_ComputeShaderOutput`
2. `M_ComputeShaderPreview`
3. `BP_ComputeShaderTest`
4. `Maps/TestLevel_ComputeShader`

### Main Tasks

1. Create a render target asset with fixed known dimensions.
2. Create a preview material that samples the render target.
3. Create an actor blueprint or C++ test actor to trigger the shader.
4. Build a clean test level with a visible preview plane or mesh.
5. Support at least one manual trigger and one begin-play trigger path.

### Acceptance Criteria

1. Opening the map and pressing Play shows visible output.
2. Re-running PIE yields the same result consistently.
3. The test actor can be reused later for debug views and profiling.

## 6.3 Workstream C: Create Benchmark Rules Before Optimization

### Goal

Define how performance will be measured before implementing more complex GPU-driven features.

### Reason

The project currently enables several expensive modern rendering features by default, including DX12/SM6, ray tracing, Lumen-like settings, and Substrate. Without a clear benchmark policy, later gains from GPU-driven rendering will be hard to explain.

### Benchmark Modes

1. Visual Mode
   Used for final demo quality and screenshot/video output.

2. Benchmark Mode
   Used to isolate rendering pipeline cost and reduce noise from unrelated features.

### Benchmark Mode Recommendations

1. Prepare a documented console variable preset.
2. Reduce or disable features that hide the true effect of GPU-driven work.
3. Keep resolution and camera path fixed.
4. Use repeatable actor counts and placement rules.

### Suggested Measurements

1. Frame time
2. Game thread time
3. Render thread time
4. GPU time
5. Draw call count
6. Visible instance count
7. CPU submission cost

### Required Outputs

1. One markdown benchmark template in `docs/`
2. One fixed measurement checklist
3. One baseline result record after compute pass is working

## 6.4 Workstream D: Prepare GPU-Driven Core Data Structures

### Goal

Do the first architecture work needed for real indirect rendering, even if full indirect draw is not fully complete in this phase.

### Scope

This workstream should focus on data ownership and buffer layout, not on premature feature expansion.

### Main Tasks

1. Define instance data structure for transform and bounds.
2. Define GPU-visible structured buffer layout for instance input.
3. Define visible instance list buffer layout.
4. Define indirect argument buffer layout.
5. Separate CPU authoring data from GPU execution data.
6. Decide whether the first MVP draw path will be plugin-owned or attached to a custom actor/component.

### Suggested Data Types

1. Object transform
2. Object bounds
3. Instance ID
4. Material or mesh index
5. LOD metadata placeholder
6. Visible object output list

### Acceptance Criteria

1. Buffer layout is documented in code comments and markdown.
2. The data path is reusable for both culling and indirect draw stages.
3. No ad hoc one-off buffer design is introduced that must be rewritten immediately.

## 6.5 Workstream E: Indirect Rendering MVP

### Goal

Build the smallest meaningful GPU-driven draw prototype.

### Recommended MVP Direction

The first MVP should not try to solve every rendering problem at once.
It should target one repeated mesh type or a tightly controlled instanced scenario.

### Proposed MVP Scope

1. One mesh type.
2. Many instances.
3. Structured buffer input.
4. Indirect arguments generated or updated from GPU-visible data.
5. One simple visibility decision path.

### Main Tasks

1. Choose the first render representation:
   - Custom component path.
   - Instanced static mesh backed prototype.
   - Narrow plugin-only experimental path.
2. Build instance upload path from CPU to GPU buffer.
3. Build indirect args initialization path.
4. Connect compute output to visible instance selection.
5. Execute indirect draw or a close pre-indirect validation step if full draw integration is still too large.
6. Compare CPU-driven and prototype GPU-driven submission behavior.

### Acceptance Criteria

1. A controlled many-instance scene can be rendered through the prototype path.
2. The prototype exposes enough metrics to compare against a traditional path.
3. The architecture remains extensible toward culling and LOD.

## 6.6 Workstream F: First GPU Culling MVP

### Goal

Add the first real GPU-driven decision stage after the compute pass is stable.

### Recommended Scope

Only implement frustum culling first.
Do not combine frustum, occlusion, and LOD in one jump.

### Main Tasks

1. Upload bounds data to GPU.
2. Pass camera frustum planes into the compute shader.
3. Test object visibility in compute.
4. Write visible instance IDs into an append or indexed output buffer.
5. Update visible draw count or indirect argument count from compute output.
6. Add debug visualization support for visible versus rejected objects.

### Acceptance Criteria

1. Camera movement changes visible counts as expected.
2. Draw submission reflects culling output.
3. The culling result can be visualized or logged for debugging.

## 6.7 Workstream G: Debugging And Profiling Workflow

### Goal

Make sure the project can be debugged and explained, not only run.

### Main Tasks

1. Add lightweight log categories for the plugin.
2. Add optional on-screen debug text for counts and timings.
3. Validate the compute pass through RenderDoc or PIX captures.
4. Capture at least one known-good frame after the compute path works.
5. Record the expected call flow in a short debug note.

### Acceptance Criteria

1. We can identify the compute dispatch in GPU capture tools.
2. We can explain which buffer is input, output, and debug-relevant.
3. Basic timing and visible count information can be viewed quickly in editor.

## 7. Recommended Execution Order

The recommended order for implementation is:

1. Workstream A: Real compute pass
2. Workstream B: Minimum validation scene
3. Workstream C: Benchmark rules
4. Workstream D: Core data structures
5. Workstream E: Indirect rendering MVP
6. Workstream F: Frustum culling MVP
7. Workstream G: Profiling and debug consolidation

This order is important because the project currently lacks a stable verification loop.
Without that loop, later indirect and culling work will become harder to debug and harder to present.

## 8. Milestone Plan

## Milestone 1: Compute Pass Closed Loop

### Objective

Get the plugin from placeholder state to real visible GPU execution.

### Expected Outputs

1. Real dispatch code
2. Render target output
3. Test map
4. Initial timing logs

### Completion Signal

The team can launch the map, trigger the shader, and see deterministic output.

## Milestone 2: Measurement Baseline

### Objective

Lock down the benchmark method before the architecture grows.

### Expected Outputs

1. Benchmark mode rules
2. Fixed test scene rules
3. First metric table
4. First profiling capture

### Completion Signal

The team can measure the same scene repeatedly and explain what the numbers mean.

## Milestone 3: GPU-Driven Rendering MVP

### Objective

Introduce the first data-driven instance rendering prototype.

### Expected Outputs

1. Structured instance buffer path
2. Visible instance output path
3. Indirect rendering MVP or near-indirect validation path
4. First CPU-driven versus GPU-driven comparison notes

### Completion Signal

The project has a genuine GPU-driven prototype rather than only an isolated compute shader demo.

## Milestone 4: Frustum Culling Integration

### Objective

Make the first GPU decision stage influence draw submission.

### Expected Outputs

1. Frustum culling compute pass
2. Visible count integration
3. Debug visualization or logging
4. Measured effect on submission behavior

### Completion Signal

The project can show camera-dependent visibility changes produced by GPU logic.

## 9. Suggested Timeline

This phase can be treated as a focused 2 to 3 week execution block.

### Week 1

1. Finish real compute pass implementation.
2. Build test assets and map.
3. Verify output and repeated execution stability.

### Week 2

1. Lock benchmark rules.
2. Define core GPU-driven data structures.
3. Start instance buffer and indirect args path.

### Week 3

1. Finish indirect MVP.
2. Add frustum culling MVP.
3. Produce first comparison results and technical notes.

If time becomes constrained, the minimum acceptable stop point is:

1. Real compute pass complete.
2. Benchmark loop complete.
3. Core data structures complete.
4. Indirect rendering MVP partially functional and clearly documented.

## 10. Engineering Constraints

The following constraints should shape implementation decisions:

1. Keep the first prototype narrow.
2. Avoid mixing too many rendering features in the same iteration.
3. Keep code modular enough that culling, indirect draw, and profiling can evolve independently.
4. Prefer explicit debugability over premature abstraction.
5. Do not build the final showcase scene before the pipeline is measurable.

## 11. Risks And Mitigation

## 11.1 Risk: The Project Becomes A Shader Sandbox Instead Of A Pipeline Demo

### Mitigation

Always connect each compute step to a visible rendering or measurement outcome.

## 11.2 Risk: Benchmark Data Becomes Meaningless Due To Heavy Renderer Features

### Mitigation

Introduce a documented benchmark mode early and measure with fixed settings.

## 11.3 Risk: Indirect Rendering Integration Scope Explodes

### Mitigation

Target one mesh type and one controlled instancing scenario first.

## 11.4 Risk: Debugging GPU Buffers Is Too Slow

### Mitigation

Add debug counts, validation logs, and a capture workflow before expanding feature scope.

## 11.5 Risk: Architecture Gets Rewritten Repeatedly

### Mitigation

Define buffer ownership and data layout before large-scale feature coding.

## 12. Recommended Repository Additions During This Phase

Suggested new documents:

1. `docs/Benchmark_Baseline_Template.md`
2. `docs/GPU_Buffer_Layout_Notes.md`
3. `docs/Frame_Capture_Checklist.md`

Suggested new runtime assets:

1. `Content/Maps/TestLevel_ComputeShader`
2. `Content/Materials/M_ComputeShaderPreview`
3. `Content/RenderTargets/RT_ComputeShaderOutput`
4. `Content/Blueprints/BP_ComputeShaderTest`

Suggested code structure evolution:

1. `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/Compute/`
2. `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/Indirect/`
3. `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Public/Culling/`
4. `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/Compute/`
5. `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/Indirect/`
6. `Plugins/GPUDrivenPipeline/Source/GPUDrivenPipeline/Private/Culling/`

## 13. Exit Conditions For The Phase

This phase should end when:

1. The project has one trustworthy end-to-end compute validation path.
2. The project has one narrow but real GPU-driven rendering prototype.
3. The project has baseline metrics and a repeatable benchmark method.
4. The next phase can reasonably target occlusion culling, LOD, deeper optimization, and presentation polish.

## 14. Recommended Next Phase After This One

After this phase is complete, the following phase should focus on:

1. Hi-Z generation and occlusion culling.
2. GPU-side LOD selection.
3. Better scene scale and asset richness.
4. Stronger debug visualization.
5. Structured before/after performance reporting for portfolio presentation.
