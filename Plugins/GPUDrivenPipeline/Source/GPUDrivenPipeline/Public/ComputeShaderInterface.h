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
    // Execute the compute shader
    UFUNCTION(BlueprintCallable, Category = "GPU Driven Pipeline")
    static void ExecuteSimpleComputeShader(UTextureRenderTarget2D* OutputRenderTarget);

    // Get shader execution time
    UFUNCTION(BlueprintCallable, Category = "GPU Driven Pipeline")
    static float GetLastExecutionTime();
};
