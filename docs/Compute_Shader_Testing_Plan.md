# Compute Shader Plugin Testing Plan

## Overview
This document provides a comprehensive testing plan for the GPUDrivenPipeline Compute Shader plugin. The plan includes test scene setup, step-by-step engine operations, and GPU debugging tool usage.

## Test Objectives
1. **Verify plugin functionality** - Ensure the compute shader executes correctly
2. **Validate performance** - Measure shader execution time
3. **Test integration** - Verify blueprint interface works as expected
4. **Debug capability** - Test GPU debugging tools with the shader

## Test Environment Requirements
- UE5.8 Editor with GPUDrivenPipeline plugin enabled
- GPU debugging tools installed (RenderDoc, PIX)
- Basic understanding of UE5 Editor interface

## Phase 1: Basic Functionality Test

### 1.1 Test Scene Setup

**Step 1: Create New Level**
1. Open UE5 Editor
2. Go to `File > New Level`
3. Select "Empty Level"
4. Save as `TestLevel_ComputeShader` in `Content/Maps/`

**Step 2: Create Render Target**
1. In Content Browser, right-click > `Materials & Textures > Render Target`
2. Name it `RT_ComputeShaderOutput`
3. Set properties:
   - Size X: 1024
   - Size Y: 1024
   - Format: RTF RGBA8
4. Save the render target

**Step 3: Create Test Material**
1. Right-click in Content Browser > `Material`
2. Name it `M_ComputeShaderTest`
3. Open material editor
4. Add `TextureSample` node
5. Connect to `RT_ComputeShaderOutput` render target
6. Connect to `Base Color`
7. Save and apply material

**Step 4: Create Test Mesh**
1. Add a plane to the scene: `Place Actors > Basic > Plane`
2. Scale to 10x10 units
3. Assign `M_ComputeShaderTest` material to the plane
4. Position camera to view the plane

### 1.2 Blueprint Test Setup

**Step 1: Create Test Blueprint**
1. Right-click in Content Browser > `Blueprint Class`
2. Select `Actor` as parent class
3. Name it `BP_ComputeShaderTest`
4. Open Blueprint editor

**Step 2: Add Components**
1. Add `Static Mesh` component
2. Set mesh to `Plane`
3. Scale to 10x10
4. Add material `M_ComputeShaderTest`

**Step 3: Create Test Function**
1. In Event Graph, create custom event `TestComputeShader`
2. Add nodes:
   - `Get Render Target` (reference `RT_ComputeShaderOutput`)
   - `Execute Simple Compute Shader` (from GPUDrivenPipeline plugin)
   - `Print String` (for success message)
3. Connect nodes properly

**Step 4: Set Auto-run**
1. In Event Graph, add `Event BeginPlay`
2. Connect to `TestComputeShader` event
3. Compile and save Blueprint

### 1.3 Execute Basic Test

**Step 1: Place Blueprint in Scene**
1. Drag `BP_ComputeShaderTest` into the level
2. Position to view the plane

**Step 2: Play Test**
1. Click "Play" button
2. Observe the plane - should show gradient pattern
3. Check Output Log for success messages

**Step 3: Verify Results**
- **Expected**: Plane shows red-green gradient
- **Possible Issues**:
  - Black plane: Shader not executing
  - Solid color: Shader executing but not writing correctly
  - Error messages: Check Output Log for details

## Phase 2: Performance Testing

### 2.1 Performance Measurement Setup

**Step 1: Add Performance Logging**
1. Modify `ComputeShaderInterface.cpp`:
```cpp
// Add timing measurement
double StartTime = FPlatformTime::Seconds();
// Execute shader
// ... shader execution code ...
double EndTime = FPlatformTime::Seconds();
GLastExecutionTime = (float)(EndTime - StartTime);
```

**Step 2: Create Performance Test Blueprint**
1. Create new Blueprint `BP_PerformanceTest`
2. Add function `RunPerformanceTest`:
   - Loop 100 times
   - Execute compute shader each iteration
   - Log execution time
   - Calculate average

**Step 3: Add UI for Results**
1. Add `Text Render` component to display results
2. Update text with performance metrics

### 2.2 Performance Test Execution

**Step 1: Test Different Texture Sizes**
Create test cases for:
- 256x256
- 512x512
- 1024x1024
- 2048x2048

**Step 2: Record Results**
Create performance table:

| Texture Size | Execution Time (ms) | FPS Impact |
|--------------|-------------------|------------|
| 256x256      |                   |            |
| 512x512      |                   |            |
| 1024x1024    |                   |            |
| 2048x2048    |                   |            |

**Step 3: Analyze Results**
- Compare with CPU-based equivalent
- Identify performance bottlenecks
- Document findings

## Phase 3: GPU Debugging with RenderDoc

### 3.1 RenderDoc Setup

**Step 1: Launch RenderDoc**
1. Open RenderDoc application
2. Go to `File > Launch Application`
3. Browse to UE5 Editor executable
4. Set working directory to project folder
5. Add command line: `-project="E:\unrealProject\pro\pro.uproject"`

**Step 2: Configure Capture Settings**
1. In RenderDoc, go to `Tools > Settings`
2. Set capture key (F12 recommended)
3. Enable "Allow fullscreen capture"
4. Set capture path to `Saved/RenderDocCaptures/`

**Step 3: Launch UE5 through RenderDoc**
1. Click "Launch" in RenderDoc
2. UE5 Editor will start with RenderDoc attached
3. Open test level

### 3.2 Frame Capture

**Step 1: Trigger Shader Execution**
1. Play the test scene
2. Let shader execute at least once

**Step 2: Capture Frame**
1. Press F12 (capture key)
2. RenderDoc will capture current frame
3. Capture should show in RenderDoc UI

**Step 3: Analyze Capture**
1. In RenderDoc, open captured frame
2. Look for compute shader dispatch call
3. Check input/output textures
4. Verify shader execution

### 3.3 Shader Debugging

**Step 1: View Shader Code**
1. In RenderDoc, find compute shader dispatch
2. Click on shader to view HLSL code
3. Check for compilation errors

**Step 2: Inspect Resources**
1. View bound resources (textures, buffers)
2. Check UAV (Unordered Access View) for output texture
3. Verify data is being written correctly

**Step 3: Step Through Shader**
1. Use RenderDoc's shader debugging features
2. Set breakpoints in shader code
3. Inspect variable values at runtime

## Phase 4: GPU Debugging with PIX

### 4.1 PIX Setup

**Step 1: Launch PIX**
1. Open PIX for Windows
2. Go to `File > New Capture`
3. Select "GPU Capture"

**Step 2: Configure Capture**
1. Set target application: UE5 Editor
2. Set working directory: Project folder
3. Add command line arguments: `-project="E:\unrealProject\pro\pro.uproject"`

**Step 3: Start Capture Session**
1. Click "Start" to launch UE5
2. PIX will attach to the process
3. Open test level

### 4.2 Performance Analysis

**Step 1: Capture Frame**
1. In PIX, click "Capture Frame" button
2. Execute shader in UE5
3. PIX will capture GPU workload

**Step 2: Analyze Timeline**
1. View GPU timeline in PIX
2. Identify compute shader execution
3. Measure execution time

**Step 3: View Counters**
1. Check GPU utilization
2. Monitor memory bandwidth
3. Analyze shader performance counters

### 4.3 Memory Analysis

**Step 1: Check Resource Usage**
1. View texture memory usage
2. Check buffer allocations
3. Identify memory leaks

**Step 2: Analyze Access Patterns**
1. Check texture read/write patterns
2. Verify cache efficiency
3. Identify optimization opportunities

## Phase 5: Integration Testing

### 5.1 Blueprint Integration Test

**Step 1: Test Multiple Calls**
1. Create Blueprint that calls shader 10 times in sequence
2. Verify each call completes successfully
3. Check for memory leaks or crashes

**Step 2: Test Different Parameters**
1. Modify shader to accept parameters
2. Test with different input values
3. Verify output changes accordingly

**Step 3: Error Handling Test**
1. Pass invalid parameters (null render target)
2. Verify graceful error handling
3. Check error messages

### 5.2 Stress Testing

**Step 1: High-frequency Execution**
1. Execute shader every frame
2. Run for 5 minutes
3. Monitor performance and stability

**Step 2: Large Texture Test**
1. Test with 4096x4096 textures
2. Monitor GPU memory usage
3. Check for performance degradation

**Step 3: Multiple Shader Instances**
1. Execute multiple different shaders
2. Verify no conflicts or resource issues
3. Measure total performance impact

## Test Documentation

### Test Results Template
```markdown
## Test Results: [Test Name]
**Date**: YYYY-MM-DD
**Tester**: [Name]
**Environment**: [Hardware/Software]

### Test Objective
[What this test aims to verify]

### Test Steps
1. [Step 1]
2. [Step 2]
3. [Step 3]

### Expected Result
[What should happen]

### Actual Result
[What actually happened]

### Pass/Fail
[Pass/Fail with explanation]

### Notes
[Any additional observations]
```

### Performance Report Template
```markdown
## Performance Report: Compute Shader
**Date**: YYYY-MM-DD
**Hardware**: [GPU Model, CPU, RAM]

### Test Configuration
- Texture Size: [Size]
- Thread Groups: [X, Y, Z]
- Iterations: [Number]

### Results
| Metric | Value | Notes |
|--------|-------|-------|
| Execution Time | [ms] | Average of [n] runs |
| GPU Utilization | [%] | During shader execution |
| Memory Usage | [MB] | Peak usage |

### Comparison
- CPU Equivalent: [Time]
- Performance Gain: [X]x faster

### Recommendations
- [Optimization suggestions]
```

## Troubleshooting Guide

### Common Issues and Solutions

**Issue 1: Shader Not Executing**
- **Symptoms**: Black texture, no output
- **Check**: 
  - Plugin enabled in .uproject
  - Shader compilation successful
  - Blueprint connected correctly
- **Solution**: Check Output Log for errors

**Issue 2: RenderDoc Capture Empty**
- **Symptoms**: No draw calls in capture
- **Check**:
  - RenderDoc attached before shader execution
  - Capture triggered at right time
- **Solution**: Use manual capture key

**Issue 3: Performance Worse Than Expected**
- **Symptoms**: High execution time
- **Check**:
  - Thread group size optimal
  - Texture format efficient
  - No unnecessary synchronization
- **Solution**: Use PIX to identify bottleneck

## Success Criteria

### Functional Requirements
- [ ] Shader executes without errors
- [ ] Output texture shows expected pattern
- [ ] Blueprint interface works correctly
- [ ] No memory leaks

### Performance Requirements
- [ ] Execution time < 1ms for 1024x1024
- [ ] No frame rate impact when not executing
- [ ] Stable performance over time

### Debugging Requirements
- [ ] Can capture frame with RenderDoc
- [ ] Can analyze with PIX
- [ ] Can debug shader code
- [ ] Can identify performance bottlenecks

## Next Steps After Testing

### Immediate Actions
1. Fix any identified issues
2. Optimize performance bottlenecks
3. Improve error handling

### Future Development
1. Add more complex shaders
2. Implement indirect rendering
3. Create GPU culling system

### Documentation Updates
1. Update API documentation
2. Create usage examples
3. Document best practices

This testing plan provides a comprehensive framework for validating the Compute Shader plugin functionality, performance, and debugging capabilities.