// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GPUDrivenIndirectDrawInterface.generated.h"

class UTextureRenderTarget2D;

UCLASS()
class GPUDRIVENPIPELINE_API UGPUDrivenIndirectDrawInterface : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "GPU Driven Pipeline|Indirect Draw", meta = (DisplayName = "Execute Test Indirect Instance Draw", ToolTip = "Uploads deterministic instance data, lets a compute shader build indirect draw arguments, and renders colored quads into the supplied render target."))
    static void ExecuteTestIndirectInstanceDraw(UTextureRenderTarget2D* OutputRenderTarget, int32 InstanceCount = 1024);
};
