// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPUData/GPUDrivenIndirectDrawInterface.h"

#include "CommonRenderResources.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GPUData/GPUDrivenInstanceData.h"
#include "GPUData/IndirectDrawShaders.h"
#include "PipelineStateCache.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "RHIResourceUtils.h"
#include "RHIStaticStates.h"
#include "TextureResource.h"

namespace
{
    constexpr int32 MaxIndirectDrawInstanceCount = 65536;
    constexpr uint32 IndirectArgsUintCount = 4;
    constexpr float TestInstanceSpacing = 100.0f;
    constexpr float IndirectDrawQuadPixelSize = 8.0f;

    static int32 GetGridWidth(const int32 InstanceCount)
    {
        return FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(InstanceCount))));
    }

    static FVector2f GetGridWorldExtent(const int32 InstanceCount)
    {
        const int32 GridWidth = GetGridWidth(InstanceCount);
        const int32 GridHeight = FMath::Max(1, FMath::DivideAndRoundUp(InstanceCount, GridWidth));
        const float ExtentX = FMath::Max(1, GridWidth - 1) * TestInstanceSpacing;
        const float ExtentY = FMath::Max(1, GridHeight - 1) * TestInstanceSpacing;
        return FVector2f(ExtentX, ExtentY);
    }

    static TArray<FGPUDrivenInstanceData> GenerateTestInstances(const int32 InstanceCount)
    {
        TArray<FGPUDrivenInstanceData> Instances;
        Instances.Reserve(InstanceCount);

        const int32 GridWidth = GetGridWidth(InstanceCount);

        for (int32 Index = 0; Index < InstanceCount; ++Index)
        {
            const int32 X = Index % GridWidth;
            const int32 Y = Index / GridWidth;

            FGPUDrivenInstanceData Instance;
            Instance.Position = FVector3f(
                static_cast<float>(X) * TestInstanceSpacing,
                static_cast<float>(Y) * TestInstanceSpacing,
                0.0f);
            Instance.Radius = 50.0f;
            Instance.Scale = FVector3f(1.0f, 1.0f, 1.0f);
            Instance.Flags = static_cast<uint32>(Index % 4);

            Instances.Add(Instance);
        }

        return Instances;
    }
}

void UGPUDrivenIndirectDrawInterface::ExecuteTestIndirectInstanceDraw(UTextureRenderTarget2D* OutputRenderTarget, int32 InstanceCount)
{
    if (!GRHIGlobals.SupportsDrawIndirect)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Current RHI does not support draw indirect."));
        return;
    }

    if (!OutputRenderTarget)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: ExecuteTestIndirectInstanceDraw requires a valid render target."));
        return;
    }

    if (InstanceCount <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: ExecuteTestIndirectInstanceDraw requires InstanceCount > 0."));
        return;
    }

    if (InstanceCount > MaxIndirectDrawInstanceCount)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: ExecuteTestIndirectInstanceDraw clamped InstanceCount from %d to %d."),
            InstanceCount,
            MaxIndirectDrawInstanceCount);
        InstanceCount = MaxIndirectDrawInstanceCount;
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

    TArray<FGPUDrivenInstanceData> Instances = GenerateTestInstances(InstanceCount);
    const int32 BufferSizeBytes = Instances.Num() * sizeof(FGPUDrivenInstanceData);
    const FVector2f GridWorldExtent = GetGridWorldExtent(InstanceCount);

    ENQUEUE_RENDER_COMMAND(GPUDrivenPipeline_ExecuteTestIndirectInstanceDraw)(
        [Instances = MoveTemp(Instances), InstanceCount, BufferSizeBytes, GridWorldExtent, TextureSize, RenderTargetResource, RenderTargetName = OutputRenderTarget->GetFName()](FRHICommandListImmediate& RHICmdList)
        {
            const FTextureRHIRef RenderTargetTexture = RenderTargetResource->GetRenderTargetTexture();
            if (!RenderTargetTexture.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Render target texture is invalid for %s."),
                    *RenderTargetName.ToString());
                return;
            }

            FBufferRHIRef InstanceBuffer = UE::RHIResourceUtils::CreateBufferFromArray<FGPUDrivenInstanceData>(
                RHICmdList,
                TEXT("GPUDrivenPipeline.IndirectDraw.InstanceData"),
                EBufferUsageFlags::StructuredBuffer | EBufferUsageFlags::ShaderResource,
                ERHIAccess::SRVGraphics,
                TConstArrayView<FGPUDrivenInstanceData>(Instances));

            if (!InstanceBuffer.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Failed to create indirect draw instance buffer."));
                return;
            }

            FShaderResourceViewRHIRef InstanceDataSRV = RHICmdList.CreateShaderResourceView(
                InstanceBuffer,
                FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(InstanceBuffer));

            const uint32 InitialIndirectArgs[IndirectArgsUintCount] = {};
            FBufferRHIRef IndirectArgsBuffer = UE::RHIResourceUtils::CreateBufferFromArray<uint32>(
                RHICmdList,
                TEXT("GPUDrivenPipeline.IndirectDraw.Args"),
                EBufferUsageFlags::DrawIndirect | EBufferUsageFlags::UnorderedAccess,
                ERHIAccess::UAVCompute,
                TConstArrayView<uint32>(InitialIndirectArgs, IndirectArgsUintCount));

            if (!IndirectArgsBuffer.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Failed to create indirect draw args buffer."));
                return;
            }

            FUnorderedAccessViewRHIRef IndirectArgsUAV = RHICmdList.CreateUnorderedAccessView(
                IndirectArgsBuffer,
                FRHIViewDesc::CreateBufferUAV()
                    .SetType(FRHIViewDesc::EBufferType::Typed)
                    .SetFormat(PF_R32_UINT));

            if (!InstanceDataSRV.IsValid() || !IndirectArgsUAV.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Failed to create indirect draw SRV/UAV."));
                return;
            }

            FIndirectDrawArgsShader::FParameters ComputeParameters;
            ComputeParameters.OutIndirectArgs = IndirectArgsUAV;
            ComputeParameters.InstanceCount = static_cast<uint32>(InstanceCount);

            TShaderMapRef<FIndirectDrawArgsShader> IndirectArgsShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

            const double ComputeStartTime = FPlatformTime::Seconds();
            FComputeShaderUtils::Dispatch(RHICmdList, IndirectArgsShader, ComputeParameters, FIntVector(1, 1, 1));
            const double ComputeEndTime = FPlatformTime::Seconds();
            const float ComputeDispatchTimeMs = static_cast<float>((ComputeEndTime - ComputeStartTime) * 1000.0);

            RHICmdList.Transition(FRHITransitionInfo(IndirectArgsBuffer, ERHIAccess::UAVCompute, ERHIAccess::IndirectArgs));
            RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture, ERHIAccess::SRVMask, ERHIAccess::RTV));

            FRHITexture* ColorRTs[1] = { RenderTargetTexture.GetReference() };
            FRHIRenderPassInfo RenderPassInfo(1, ColorRTs, ERenderTargetActions::Clear_Store);

            RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("GPUDrivenPipeline_IndirectDraw"));
            RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, static_cast<float>(TextureSize.X), static_cast<float>(TextureSize.Y), 1.0f);

            TShaderMapRef<FIndirectDrawInstanceVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            TShaderMapRef<FIndirectDrawInstancePS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

            FGraphicsPipelineStateInitializer GraphicsPSOInit;
            RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
            GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
            GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
            GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
            GraphicsPSOInit.PrimitiveType = PT_TriangleList;
            GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
            GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
            GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
            SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

            FIndirectDrawInstanceVS::FParameters VertexParameters;
            VertexParameters.InstanceData = InstanceDataSRV;
            VertexParameters.InstanceCount = static_cast<uint32>(InstanceCount);
            VertexParameters.RenderTargetSize = FVector2f(TextureSize.X, TextureSize.Y);
            VertexParameters.GridWorldExtent = GridWorldExtent;
            VertexParameters.QuadPixelSize = IndirectDrawQuadPixelSize;

            FIndirectDrawInstancePS::FParameters PixelParameters;

            SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VertexParameters);
            SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PixelParameters);

            const double DrawStartTime = FPlatformTime::Seconds();
            RHICmdList.DrawPrimitiveIndirect(IndirectArgsBuffer, 0);
            const double DrawEndTime = FPlatformTime::Seconds();
            const float DrawDispatchTimeMs = static_cast<float>((DrawEndTime - DrawStartTime) * 1000.0);

            RHICmdList.EndRenderPass();
            RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture, ERHIAccess::RTV, ERHIAccess::SRVMask));

            UE_LOG(LogTemp, Log, TEXT("GPUDrivenPipeline: IndirectDraw MVP rendered %d instances to %s (%dx%d, instance-bytes=%d, args-groups=1,1,1, cpu-compute=%.3f ms, cpu-draw=%.3f ms)."),
                InstanceCount,
                *RenderTargetName.ToString(),
                TextureSize.X,
                TextureSize.Y,
                BufferSizeBytes,
                ComputeDispatchTimeMs,
                DrawDispatchTimeMs);
        });
}
