// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPUData/GPUDrivenIndirectDrawInterface.h"

#include "CommonRenderResources.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GPUData/FrustumCullShader.h"
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

    static void BuildTestFrustumPlanes(const FVector2f& GridWorldExtent, FVector4f OutPlanes[6])
    {
        const float MinX = GridWorldExtent.X * 0.25f;
        const float MaxX = GridWorldExtent.X * 0.75f;
        const float MinY = GridWorldExtent.Y * 0.25f;
        const float MaxY = GridWorldExtent.Y * 0.75f;

        OutPlanes[0] = FVector4f(1.0f, 0.0f, 0.0f, -MinX);
        OutPlanes[1] = FVector4f(-1.0f, 0.0f, 0.0f, MaxX);
        OutPlanes[2] = FVector4f(0.0f, 1.0f, 0.0f, -MinY);
        OutPlanes[3] = FVector4f(0.0f, -1.0f, 0.0f, MaxY);
        OutPlanes[4] = FVector4f(0.0f, 0.0f, 1.0f, 100000.0f);
        OutPlanes[5] = FVector4f(0.0f, 0.0f, -1.0f, 100000.0f);
    }

    static bool IsSphereInsidePlane(const FVector3f& Center, const float Radius, const FVector4f& Plane)
    {
        return FVector3f::DotProduct(FVector3f(Plane.X, Plane.Y, Plane.Z), Center) + Plane.W >= -Radius;
    }

    static int32 EstimateVisibleInstanceCount(const TArray<FGPUDrivenInstanceData>& Instances, const FVector4f FrustumPlanes[6])
    {
        int32 VisibleCount = 0;

        for (const FGPUDrivenInstanceData& Instance : Instances)
        {
            bool bVisible = true;
            for (int32 PlaneIndex = 0; PlaneIndex < 6; ++PlaneIndex)
            {
                if (!IsSphereInsidePlane(Instance.Position, Instance.Radius, FrustumPlanes[PlaneIndex]))
                {
                    bVisible = false;
                    break;
                }
            }

            if (bVisible)
            {
                ++VisibleCount;
            }
        }

        return VisibleCount;
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

void UGPUDrivenIndirectDrawInterface::ExecuteTestFrustumCulledIndirectDraw(UTextureRenderTarget2D* OutputRenderTarget, int32 InstanceCount)
{
    if (!GRHIGlobals.SupportsDrawIndirect)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Current RHI does not support draw indirect."));
        return;
    }

    if (!OutputRenderTarget)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: ExecuteTestFrustumCulledIndirectDraw requires a valid render target."));
        return;
    }

    if (InstanceCount <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: ExecuteTestFrustumCulledIndirectDraw requires InstanceCount > 0."));
        return;
    }

    if (InstanceCount > MaxIndirectDrawInstanceCount)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: ExecuteTestFrustumCulledIndirectDraw clamped InstanceCount from %d to %d."),
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
    TArray<FGPUDrivenInstanceData> VisibleInstanceInitialValues;
    VisibleInstanceInitialValues.SetNumZeroed(InstanceCount);

    const int32 BufferSizeBytes = Instances.Num() * sizeof(FGPUDrivenInstanceData);
    const FVector2f GridWorldExtent = GetGridWorldExtent(InstanceCount);

    FVector4f FrustumPlanes[6];
    BuildTestFrustumPlanes(GridWorldExtent, FrustumPlanes);
    const int32 EstimatedVisibleCount = EstimateVisibleInstanceCount(Instances, FrustumPlanes);

    ENQUEUE_RENDER_COMMAND(GPUDrivenPipeline_ExecuteTestFrustumCulledIndirectDraw)(
        [Instances = MoveTemp(Instances),
            VisibleInstanceInitialValues = MoveTemp(VisibleInstanceInitialValues),
            InstanceCount,
            BufferSizeBytes,
            GridWorldExtent,
            TextureSize,
            RenderTargetResource,
            RenderTargetName = OutputRenderTarget->GetFName(),
            EstimatedVisibleCount,
            FrustumPlane0 = FrustumPlanes[0],
            FrustumPlane1 = FrustumPlanes[1],
            FrustumPlane2 = FrustumPlanes[2],
            FrustumPlane3 = FrustumPlanes[3],
            FrustumPlane4 = FrustumPlanes[4],
            FrustumPlane5 = FrustumPlanes[5]](FRHICommandListImmediate& RHICmdList)
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
                TEXT("GPUDrivenPipeline.FrustumCull.InstanceData"),
                EBufferUsageFlags::StructuredBuffer | EBufferUsageFlags::ShaderResource,
                ERHIAccess::SRVCompute,
                TConstArrayView<FGPUDrivenInstanceData>(Instances));

            FBufferRHIRef VisibleInstanceBuffer = UE::RHIResourceUtils::CreateBufferFromArray<FGPUDrivenInstanceData>(
                RHICmdList,
                TEXT("GPUDrivenPipeline.FrustumCull.VisibleInstanceData"),
                EBufferUsageFlags::StructuredBuffer | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess,
                ERHIAccess::UAVCompute,
                TConstArrayView<FGPUDrivenInstanceData>(VisibleInstanceInitialValues));

            const uint32 InitialIndirectArgs[IndirectArgsUintCount] = {};
            FBufferRHIRef IndirectArgsBuffer = UE::RHIResourceUtils::CreateBufferFromArray<uint32>(
                RHICmdList,
                TEXT("GPUDrivenPipeline.FrustumCull.Args"),
                EBufferUsageFlags::DrawIndirect | EBufferUsageFlags::UnorderedAccess,
                ERHIAccess::UAVCompute,
                TConstArrayView<uint32>(InitialIndirectArgs, IndirectArgsUintCount));

            if (!InstanceBuffer.IsValid() || !VisibleInstanceBuffer.IsValid() || !IndirectArgsBuffer.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Failed to create frustum culling buffers."));
                return;
            }

            FShaderResourceViewRHIRef InstanceDataSRV = RHICmdList.CreateShaderResourceView(
                InstanceBuffer,
                FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(InstanceBuffer));

            FShaderResourceViewRHIRef VisibleInstanceDataSRV = RHICmdList.CreateShaderResourceView(
                VisibleInstanceBuffer,
                FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(VisibleInstanceBuffer));

            FUnorderedAccessViewRHIRef VisibleInstanceDataUAV = RHICmdList.CreateUnorderedAccessView(
                VisibleInstanceBuffer,
                FRHIViewDesc::CreateBufferUAV().SetTypeFromBuffer(VisibleInstanceBuffer));

            FUnorderedAccessViewRHIRef IndirectArgsUAV = RHICmdList.CreateUnorderedAccessView(
                IndirectArgsBuffer,
                FRHIViewDesc::CreateBufferUAV()
                    .SetType(FRHIViewDesc::EBufferType::Typed)
                    .SetFormat(PF_R32_UINT));

            if (!InstanceDataSRV.IsValid() || !VisibleInstanceDataSRV.IsValid() || !VisibleInstanceDataUAV.IsValid() || !IndirectArgsUAV.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Failed to create frustum culling SRV/UAV resources."));
                return;
            }

            FFrustumCullInstancesShader::FParameters CullingParameters;
            CullingParameters.InstanceData = InstanceDataSRV;
            CullingParameters.OutVisibleInstanceData = VisibleInstanceDataUAV;
            CullingParameters.OutIndirectArgs = IndirectArgsUAV;
            CullingParameters.InstanceCount = static_cast<uint32>(InstanceCount);
            CullingParameters.FrustumPlane0 = FrustumPlane0;
            CullingParameters.FrustumPlane1 = FrustumPlane1;
            CullingParameters.FrustumPlane2 = FrustumPlane2;
            CullingParameters.FrustumPlane3 = FrustumPlane3;
            CullingParameters.FrustumPlane4 = FrustumPlane4;
            CullingParameters.FrustumPlane5 = FrustumPlane5;

            TShaderMapRef<FFrustumCullInstancesShader> CullingShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

            const double CullingStartTime = FPlatformTime::Seconds();
            FComputeShaderUtils::Dispatch(RHICmdList, CullingShader, CullingParameters, FIntVector(1, 1, 1));
            const double CullingEndTime = FPlatformTime::Seconds();
            const float CullingDispatchTimeMs = static_cast<float>((CullingEndTime - CullingStartTime) * 1000.0);

            RHICmdList.Transition(FRHITransitionInfo(VisibleInstanceBuffer, ERHIAccess::UAVCompute, ERHIAccess::SRVGraphics));
            RHICmdList.Transition(FRHITransitionInfo(IndirectArgsBuffer, ERHIAccess::UAVCompute, ERHIAccess::IndirectArgs));
            RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture, ERHIAccess::SRVMask, ERHIAccess::RTV));

            FRHITexture* ColorRTs[1] = { RenderTargetTexture.GetReference() };
            FRHIRenderPassInfo RenderPassInfo(1, ColorRTs, ERenderTargetActions::Clear_Store);

            RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("GPUDrivenPipeline_FrustumCulledIndirectDraw"));
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
            VertexParameters.InstanceData = VisibleInstanceDataSRV;
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

            UE_LOG(LogTemp, Log, TEXT("GPUDrivenPipeline: Frustum culled indirect draw submitted %d source instances to %s (%dx%d, estimated-visible=%d, instance-bytes=%d, cpu-cull=%.3f ms, cpu-draw=%.3f ms)."),
                InstanceCount,
                *RenderTargetName.ToString(),
                TextureSize.X,
                TextureSize.Y,
                EstimatedVisibleCount,
                BufferSizeBytes,
                CullingDispatchTimeMs,
                DrawDispatchTimeMs);
        });
}
