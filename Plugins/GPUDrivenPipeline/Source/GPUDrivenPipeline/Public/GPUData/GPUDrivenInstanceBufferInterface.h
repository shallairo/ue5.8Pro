// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GPUDrivenInstanceBufferInterface.generated.h"

USTRUCT(BlueprintType)
struct GPUDRIVENPIPELINE_API FGPUDrivenInstanceValidationResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    int32 InstanceCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    int32 BufferSizeBytes = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    int32 ProcessedCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    int32 ValidRadiusCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    int32 FlagSum = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    int32 PositionChecksum = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    float CpuDispatchTimeMs = 0.0f;
};

UCLASS()
class GPUDRIVENPIPELINE_API UGPUDrivenInstanceBufferInterface : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "GPU Driven Pipeline|Instance Data", meta = (DisplayName = "Upload Test Instance Data", ToolTip = "Generates deterministic CPU instance data, uploads it to a GPU structured buffer, and dispatches a validation compute shader."))
    static void UploadTestInstanceData(int32 InstanceCount = 1024);

    UFUNCTION(BlueprintCallable, Category = "GPU Driven Pipeline|Instance Data", meta = (DisplayName = "Get Last Instance Data Validation Result", ToolTip = "Returns true when the asynchronous GPU validation readback is ready. This debug path may briefly stall when copying the result back to the CPU."))
    static bool GetLastInstanceDataValidationResult(FGPUDrivenInstanceValidationResult& OutResult);
};
