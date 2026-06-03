// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ComputeShaderInterface.generated.h"

class UTextureRenderTarget2D;

UCLASS()
class GPUDRIVENPIPELINE_API UComputeShaderInterface : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Execute the simple validation compute shader and write its output into the render target.
    UFUNCTION(BlueprintCallable, Category = "GPU Driven Pipeline", meta = (DisplayName = "Execute Simple Compute Shader", ToolTip = "Dispatches the GPUDrivenPipeline validation compute shader into the supplied render target."))
    static void ExecuteSimpleComputeShader(UTextureRenderTarget2D* OutputRenderTarget);

    // Return the most recent CPU-side dispatch timing in milliseconds.
    UFUNCTION(BlueprintCallable, Category = "GPU Driven Pipeline", meta = (DisplayName = "Get Last Compute Dispatch Time", ToolTip = "Returns the last CPU-side compute dispatch duration in milliseconds. This is not GPU elapsed time."))
    static float GetLastExecutionTime();
};
