// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GPUDrivenIndirectDrawInterface.generated.h"

class UTextureRenderTarget2D;
class AActor;

USTRUCT(BlueprintType)
struct GPUDRIVENPIPELINE_API FGPUDrivenFrustumCullResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    int32 InstanceCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    int32 EstimatedVisibleCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    int32 GpuVisibleCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    int32 BufferSizeBytes = 0;

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    float CpuCullDispatchTimeMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "GPU Driven Pipeline")
    float CpuDrawDispatchTimeMs = 0.0f;
};

UCLASS()
class GPUDRIVENPIPELINE_API UGPUDrivenIndirectDrawInterface : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "GPU Driven Pipeline|Indirect Draw", meta = (DisplayName = "Execute Test Indirect Instance Draw", ToolTip = "Uploads deterministic instance data, lets a compute shader build indirect draw arguments, and renders colored quads into the supplied render target."))
    static void ExecuteTestIndirectInstanceDraw(UTextureRenderTarget2D* OutputRenderTarget, int32 InstanceCount = 1024);

    UFUNCTION(BlueprintCallable, Category = "GPU Driven Pipeline|Indirect Draw", meta = (DisplayName = "Execute Test Frustum Culled Indirect Draw", ToolTip = "Uploads deterministic instance data, lets a compute shader cull instances and build indirect draw arguments, then renders only visible colored quads into the supplied render target."))
    static void ExecuteTestFrustumCulledIndirectDraw(UTextureRenderTarget2D* OutputRenderTarget, int32 InstanceCount = 1024);

    UFUNCTION(BlueprintCallable, Category = "GPU Driven Pipeline|Indirect Draw", meta = (DisplayName = "Execute Camera Frustum Culled Indirect Draw", ToolTip = "Uploads deterministic instance data, builds world-space frustum planes from a camera actor, lets a compute shader cull instances and build indirect draw arguments, then renders only visible colored quads into the supplied render target."))
    static void ExecuteCameraFrustumCulledIndirectDraw(UTextureRenderTarget2D* OutputRenderTarget, AActor* CameraActor, int32 InstanceCount = 1024, float FieldOfViewDegrees = 90.0f, float NearPlane = 10.0f, float FarPlane = 10000.0f);

    UFUNCTION(BlueprintCallable, Category = "GPU Driven Pipeline|Indirect Draw", meta = (DisplayName = "Execute Plane Camera Frustum Culled Indirect Draw", ToolTip = "Builds camera frustum planes from a camera actor, generates debug instances over a supplied plane actor's world bounds, culls those instances on GPU, then renders the visible result into the supplied render target."))
    static void ExecutePlaneCameraFrustumCulledIndirectDraw(UTextureRenderTarget2D* OutputRenderTarget, AActor* CameraActor, AActor* PlaneActor, int32 InstanceCount = 1024, float FieldOfViewDegrees = 90.0f, float NearPlane = 10.0f, float FarPlane = 10000.0f);

    UFUNCTION(BlueprintCallable, Category = "GPU Driven Pipeline|Indirect Draw", meta = (DisplayName = "Get Last Frustum Cull Result", ToolTip = "Returns true when the frustum culling readback is ready. This debug path may briefly stall while resolving GPU summary data."))
    static bool GetLastFrustumCullResult(FGPUDrivenFrustumCullResult& OutResult);
};
