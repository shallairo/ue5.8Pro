// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeShaderInterface.h"

#include "Engine/TextureRenderTarget2D.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "SimpleComputeShader.h"
#include "TextureResource.h"

namespace
{
    TAtomic<float> GLastExecutionTimeMs(0.0f);
}

void UComputeShaderInterface::ExecuteSimpleComputeShader(UTextureRenderTarget2D* OutputRenderTarget)
{
    if (!OutputRenderTarget)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: OutputRenderTarget is null."));
        return;
    }

    if (OutputRenderTarget->SizeX <= 0 || OutputRenderTarget->SizeY <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: OutputRenderTarget %s has invalid size %dx%d."),
            *OutputRenderTarget->GetName(),
            OutputRenderTarget->SizeX,
            OutputRenderTarget->SizeY);
        return;
    }

    // Compute shader output requires the render target resource to be created with UAV support up front.
    if (!OutputRenderTarget->bSupportsUAV)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: OutputRenderTarget %s does not have UAV support enabled. Enable UAV support on the render target asset and re-save it before running the demo."),
            *OutputRenderTarget->GetName());
        return;
    }

    FTextureRenderTarget2DResource* RenderTargetResource =
        static_cast<FTextureRenderTarget2DResource*>(OutputRenderTarget->GameThread_GetRenderTargetResource());

    if (!RenderTargetResource)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Failed to acquire render target resource for %s."),
            *OutputRenderTarget->GetName());
        return;
    }

    const FIntPoint TextureSize = RenderTargetResource->GetSizeXY();
    if (TextureSize.X <= 0 || TextureSize.Y <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Render target resource for %s is not initialized yet."),
            *OutputRenderTarget->GetName());
        return;
    }

    ENQUEUE_RENDER_COMMAND(GPUDrivenPipeline_ExecuteSimpleComputeShader)(
        [RenderTargetResource, TextureSize, RenderTargetName = OutputRenderTarget->GetFName()](FRHICommandListImmediate& RHICmdList)
        {
            const FTextureRHIRef RenderTargetTexture = RenderTargetResource->GetRenderTargetTexture();
            if (!RenderTargetTexture.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Render target texture is invalid for %s."),
                    *RenderTargetName.ToString());
                return;
            }

            FUnorderedAccessViewRHIRef OutputUAV = RenderTargetResource->GetUnorderedAccessViewRHI();

            if (!OutputUAV.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Render target %s does not expose a valid UAV. Recreate the render target with UAV support enabled."),
                    *RenderTargetName.ToString());
                return;
            }

            FSimpleComputeShader::FParameters PassParameters;
            PassParameters.OutputTexture = OutputUAV;
            PassParameters.TextureSize = FVector2f(TextureSize.X, TextureSize.Y);

            TShaderMapRef<FSimpleComputeShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            const FIntVector GroupCount(
                FMath::DivideAndRoundUp(TextureSize.X, 8),
                FMath::DivideAndRoundUp(TextureSize.Y, 8),
                1);

            RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture, ERHIAccess::SRVMask, ERHIAccess::UAVCompute));

            const double StartTime = FPlatformTime::Seconds();
            FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, PassParameters, GroupCount);
            const double EndTime = FPlatformTime::Seconds();

            RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));

            const float DispatchTimeMs = static_cast<float>((EndTime - StartTime) * 1000.0);
            GLastExecutionTimeMs.Store(DispatchTimeMs);

            UE_LOG(LogTemp, Log, TEXT("GPUDrivenPipeline: Dispatched SimpleComputeShader to %s (%dx%d, groups=%d,%d,%d, cpu-dispatch=%.3f ms)."),
                *RenderTargetName.ToString(),
                TextureSize.X,
                TextureSize.Y,
                GroupCount.X,
                GroupCount.Y,
                GroupCount.Z,
                DispatchTimeMs);
        });
}

float UComputeShaderInterface::GetLastExecutionTime()
{
    return GLastExecutionTimeMs.Load();
}
