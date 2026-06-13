// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPUData/GPUDrivenIndirectDrawInterface.h"

#include "CommonRenderResources.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GPUData/FrustumCullShader.h"
#include "GPUData/GPUDrivenInstanceData.h"
#include "GPUData/IndirectDrawShaders.h"
#include "PipelineStateCache.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "RHIResourceUtils.h"
#include "RHIStaticStates.h"
#include "TextureResource.h"
#include "Misc/ScopeLock.h"

namespace
{
    constexpr int32 MaxIndirectDrawInstanceCount = 65536;
    constexpr uint32 IndirectArgsUintCount = 4;
    constexpr uint32 FrustumCullSummaryElementCount = 2;
    constexpr uint32 FrustumCullSummarySizeBytes = sizeof(uint32) * FrustumCullSummaryElementCount;
    constexpr float TestInstanceSpacing = 100.0f;
    constexpr float IndirectDrawQuadPixelSize = 8.0f;

    FCriticalSection GFrustumCullResultMutex;
    TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> GFrustumCullReadback;
    FGPUDrivenFrustumCullResult GFrustumCullResult;
    bool bFrustumCullReadbackReady = false;

    struct FGPUDrivenInstanceBounds2D
    {
        FVector2f Min = FVector2f::ZeroVector;
        FVector2f Extent = FVector2f(1.0f, 1.0f);
    };

    struct FGPUDrivenCameraViewData
    {
        FVector Origin = FVector::ZeroVector;
        FVector Forward = FVector::ForwardVector;
        FVector Right = FVector::RightVector;
        FVector Up = FVector::UpVector;
    };

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

    static TArray<FGPUDrivenInstanceData> GenerateCameraAnchoredTestInstances(
        const int32 InstanceCount,
        const FGPUDrivenCameraViewData& CameraView)
    {
        TArray<FGPUDrivenInstanceData> Instances = GenerateTestInstances(InstanceCount);
        if (Instances.IsEmpty())
        {
            return Instances;
        }

        const FVector2f GridWorldExtent = GetGridWorldExtent(InstanceCount);

        FVector GroundForward(CameraView.Forward.X, CameraView.Forward.Y, 0.0);
        if (!GroundForward.Normalize())
        {
            GroundForward = FVector::ForwardVector;
        }

        FVector GroundRight(CameraView.Right.X, CameraView.Right.Y, 0.0);
        if (!GroundRight.Normalize())
        {
            GroundRight = FVector::CrossProduct(FVector::UpVector, GroundForward).GetSafeNormal();
        }

        float DistanceToGround = 1000.0f;
        if (FMath::Abs(CameraView.Forward.Z) > KINDA_SMALL_NUMBER)
        {
            const float GroundIntersectionDistance = static_cast<float>(-CameraView.Origin.Z / CameraView.Forward.Z);
            if (GroundIntersectionDistance > 0.0f)
            {
                DistanceToGround = GroundIntersectionDistance;
            }
        }

        const FVector GridCenter = CameraView.Origin + CameraView.Forward * DistanceToGround;
        const FVector GridOrigin =
            FVector(GridCenter.X, GridCenter.Y, 0.0f)
            - GroundRight * (GridWorldExtent.X * 0.5f)
            - GroundForward * (GridWorldExtent.Y * 0.5f);

        const int32 GridWidth = GetGridWidth(InstanceCount);
        for (int32 Index = 0; Index < Instances.Num(); ++Index)
        {
            const int32 X = Index % GridWidth;
            const int32 Y = Index / GridWidth;
            const FVector WorldPosition =
                GridOrigin
                + GroundRight * (static_cast<float>(X) * TestInstanceSpacing)
                + GroundForward * (static_cast<float>(Y) * TestInstanceSpacing);

            Instances[Index].Position = FVector3f(WorldPosition);
        }

        return Instances;
    }

    static TArray<FGPUDrivenInstanceData> GeneratePlaneBoundedTestInstances(
        const int32 InstanceCount,
        const FVector& BoundsOrigin,
        const FVector& BoundsExtent)
    {
        TArray<FGPUDrivenInstanceData> Instances;
        Instances.Reserve(InstanceCount);

        const int32 GridWidth = GetGridWidth(InstanceCount);
        const int32 GridHeight = FMath::Max(1, FMath::DivideAndRoundUp(InstanceCount, GridWidth));

        const float MinX = BoundsOrigin.X - FMath::Max(1.0f, BoundsExtent.X);
        const float MaxX = BoundsOrigin.X + FMath::Max(1.0f, BoundsExtent.X);
        const float MinY = BoundsOrigin.Y - FMath::Max(1.0f, BoundsExtent.Y);
        const float MaxY = BoundsOrigin.Y + FMath::Max(1.0f, BoundsExtent.Y);
        const float StepX = GridWidth > 1 ? (MaxX - MinX) / static_cast<float>(GridWidth - 1) : 0.0f;
        const float StepY = GridHeight > 1 ? (MaxY - MinY) / static_cast<float>(GridHeight - 1) : 0.0f;
        const float Radius = FMath::Max(1.0f, FMath::Max(StepX, StepY) * 0.5f);

        for (int32 Index = 0; Index < InstanceCount; ++Index)
        {
            const int32 X = Index % GridWidth;
            const int32 Y = Index / GridWidth;

            FGPUDrivenInstanceData Instance;
            Instance.Position = FVector3f(
                MinX + static_cast<float>(X) * StepX,
                MinY + static_cast<float>(Y) * StepY,
                BoundsOrigin.Z);
            Instance.Radius = Radius;
            Instance.Scale = FVector3f(1.0f, 1.0f, 1.0f);
            Instance.Flags = static_cast<uint32>(Index % 4);

            Instances.Add(Instance);
        }

        return Instances;
    }

    static FGPUDrivenInstanceBounds2D GetInstanceBounds2D(const TArray<FGPUDrivenInstanceData>& Instances)
    {
        FGPUDrivenInstanceBounds2D Bounds;
        if (Instances.IsEmpty())
        {
            return Bounds;
        }

        FVector2f Min(Instances[0].Position.X, Instances[0].Position.Y);
        FVector2f Max = Min;

        for (const FGPUDrivenInstanceData& Instance : Instances)
        {
            Min.X = FMath::Min(Min.X, Instance.Position.X);
            Min.Y = FMath::Min(Min.Y, Instance.Position.Y);
            Max.X = FMath::Max(Max.X, Instance.Position.X);
            Max.Y = FMath::Max(Max.Y, Instance.Position.Y);
        }

        Bounds.Min = Min;
        Bounds.Extent = FVector2f(
            FMath::Max(1.0f, Max.X - Min.X),
            FMath::Max(1.0f, Max.Y - Min.Y));
        return Bounds;
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

    static FVector4f MakePlaneFromPointAndNormal(const FVector& Point, FVector Normal, const FVector& InsidePoint)
    {
        Normal = Normal.GetSafeNormal();
        float W = -FVector::DotProduct(Normal, Point);

        if (FVector::DotProduct(Normal, InsidePoint) + W < 0.0f)
        {
            Normal *= -1.0f;
            W *= -1.0f;
        }

        return FVector4f(
            static_cast<float>(Normal.X),
            static_cast<float>(Normal.Y),
            static_cast<float>(Normal.Z),
            W);
    }

    static bool BuildCameraFrustumPlanes(
        AActor* CameraActor,
        const float FieldOfViewDegrees,
        const float FallbackAspectRatio,
        const float NearPlane,
        const float FarPlane,
        FVector4f OutPlanes[6],
        FGPUDrivenCameraViewData* OutCameraView = nullptr)
    {
        if (!CameraActor)
        {
            return false;
        }

        if (FallbackAspectRatio <= KINDA_SMALL_NUMBER || NearPlane <= KINDA_SMALL_NUMBER || FarPlane <= NearPlane)
        {
            return false;
        }

        float EffectiveFovDegrees = FieldOfViewDegrees;
        float EffectiveAspectRatio = FallbackAspectRatio;
        FVector Origin = CameraActor->GetActorLocation();
        FVector Forward = CameraActor->GetActorForwardVector().GetSafeNormal();
        FVector Right = CameraActor->GetActorRightVector().GetSafeNormal();
        FVector Up = CameraActor->GetActorUpVector().GetSafeNormal();
        const TCHAR* CameraSource = TEXT("Actor");
        const TCHAR* AspectSource = TEXT("RenderTarget");

        if (const UCameraComponent* CameraComponent = CameraActor->FindComponentByClass<UCameraComponent>())
        {
            if (CameraComponent->FieldOfView > KINDA_SMALL_NUMBER)
            {
                EffectiveFovDegrees = CameraComponent->FieldOfView;
            }

            if (CameraComponent->AspectRatio > KINDA_SMALL_NUMBER)
            {
                EffectiveAspectRatio = CameraComponent->AspectRatio;
                AspectSource = TEXT("CameraComponent");
            }

            Origin = CameraComponent->GetComponentLocation();
            Forward = CameraComponent->GetForwardVector().GetSafeNormal();
            Right = CameraComponent->GetRightVector().GetSafeNormal();
            Up = CameraComponent->GetUpVector().GetSafeNormal();
            CameraSource = TEXT("CameraComponent");
        }
        else if (const APawn* Pawn = Cast<APawn>(CameraActor))
        {
            if (const APlayerController* PlayerController = Cast<APlayerController>(Pawn->GetController()))
            {
                if (const APlayerCameraManager* PlayerCameraManager = PlayerController->PlayerCameraManager)
                {
                    Origin = PlayerCameraManager->GetCameraLocation();

                    const FRotator CameraRotation = PlayerCameraManager->GetCameraRotation();
                    const FRotationMatrix CameraRotationMatrix(CameraRotation);
                    Forward = CameraRotationMatrix.GetUnitAxis(EAxis::X).GetSafeNormal();
                    Right = CameraRotationMatrix.GetUnitAxis(EAxis::Y).GetSafeNormal();
                    Up = CameraRotationMatrix.GetUnitAxis(EAxis::Z).GetSafeNormal();

                    if (PlayerCameraManager->GetFOVAngle() > KINDA_SMALL_NUMBER)
                    {
                        EffectiveFovDegrees = PlayerCameraManager->GetFOVAngle();
                    }

                    int32 ViewportSizeX = 0;
                    int32 ViewportSizeY = 0;
                    PlayerController->GetViewportSize(ViewportSizeX, ViewportSizeY);
                    if (ViewportSizeX > 0 && ViewportSizeY > 0)
                    {
                        EffectiveAspectRatio = static_cast<float>(ViewportSizeX) / static_cast<float>(ViewportSizeY);
                        AspectSource = TEXT("Viewport");
                    }

                    CameraSource = TEXT("PlayerCameraManager");
                }
            }
        }

        if (OutCameraView)
        {
            OutCameraView->Origin = Origin;
            OutCameraView->Forward = Forward;
            OutCameraView->Right = Right;
            OutCameraView->Up = Up;
        }

        EffectiveFovDegrees = FMath::Clamp(EffectiveFovDegrees, 1.0f, 179.0f);
        const float HalfHorizontalFovRadians = FMath::DegreesToRadians(EffectiveFovDegrees * 0.5f);
        const float NearHalfWidth = FMath::Tan(HalfHorizontalFovRadians) * NearPlane;
        const float NearHalfHeight = NearHalfWidth / EffectiveAspectRatio;

        const FVector NearCenter = Origin + Forward * NearPlane;
        const FVector FarCenter = Origin + Forward * FarPlane;
        const FVector NearTopLeft = NearCenter + Up * NearHalfHeight - Right * NearHalfWidth;
        const FVector NearTopRight = NearCenter + Up * NearHalfHeight + Right * NearHalfWidth;
        const FVector NearBottomLeft = NearCenter - Up * NearHalfHeight - Right * NearHalfWidth;
        const FVector NearBottomRight = NearCenter - Up * NearHalfHeight + Right * NearHalfWidth;
        const FVector InsidePoint = Origin + Forward * ((NearPlane + FarPlane) * 0.5f);

        OutPlanes[0] = MakePlaneFromPointAndNormal(NearCenter, Forward, InsidePoint);
        OutPlanes[1] = MakePlaneFromPointAndNormal(FarCenter, -Forward, InsidePoint);
        OutPlanes[2] = MakePlaneFromPointAndNormal(Origin, FVector::CrossProduct(NearTopLeft - Origin, NearBottomLeft - Origin), InsidePoint);
        OutPlanes[3] = MakePlaneFromPointAndNormal(Origin, FVector::CrossProduct(NearBottomRight - Origin, NearTopRight - Origin), InsidePoint);
        OutPlanes[4] = MakePlaneFromPointAndNormal(Origin, FVector::CrossProduct(NearTopRight - Origin, NearTopLeft - Origin), InsidePoint);
        OutPlanes[5] = MakePlaneFromPointAndNormal(Origin, FVector::CrossProduct(NearBottomLeft - Origin, NearBottomRight - Origin), InsidePoint);

        UE_LOG(LogTemp, Log, TEXT("GPUDrivenPipeline: Built camera frustum from %s (source=%s, aspect-source=%s, origin=(%.1f, %.1f, %.1f), forward=(%.3f, %.3f, %.3f), fov=%.2f, aspect=%.3f, near=%.1f, far=%.1f)."),
            *CameraActor->GetName(),
            CameraSource,
            AspectSource,
            Origin.X,
            Origin.Y,
            Origin.Z,
            Forward.X,
            Forward.Y,
            Forward.Z,
            EffectiveFovDegrees,
            EffectiveAspectRatio,
            NearPlane,
            FarPlane);
        return true;
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

    static void TryResolveFrustumCullReadback_GameThread()
    {
        TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> ReadbackToResolve;

        {
            FScopeLock Lock(&GFrustumCullResultMutex);
            if (bFrustumCullReadbackReady || !GFrustumCullReadback.IsValid() || !GFrustumCullReadback->IsReady())
            {
                return;
            }

            ReadbackToResolve = GFrustumCullReadback;
        }

        ENQUEUE_RENDER_COMMAND(GPUDrivenPipeline_ResolveFrustumCullReadback)(
            [ReadbackToResolve](FRHICommandListImmediate&)
            {
                uint32 Summary[FrustumCullSummaryElementCount] = {};
                const void* ReadbackData = ReadbackToResolve->Lock(FrustumCullSummarySizeBytes);

                if (ReadbackData)
                {
                    FMemory::Memcpy(Summary, ReadbackData, FrustumCullSummarySizeBytes);
                    ReadbackToResolve->Unlock();
                }

                FScopeLock Lock(&GFrustumCullResultMutex);
                GFrustumCullResult.InstanceCount = static_cast<int32>(Summary[1]);
                GFrustumCullResult.GpuVisibleCount = static_cast<int32>(Summary[0]);
                bFrustumCullReadbackReady = true;

                UE_LOG(LogTemp, Log, TEXT("GPUDrivenPipeline: Frustum cull readback ready (source=%d, estimated-visible=%d, gpu-visible=%d)."),
                    GFrustumCullResult.InstanceCount,
                    GFrustumCullResult.EstimatedVisibleCount,
                    GFrustumCullResult.GpuVisibleCount);
            });

        FlushRenderingCommands();
    }

    static void ExecuteFrustumCulledIndirectDrawInternal(
        UTextureRenderTarget2D* OutputRenderTarget,
        TArray<FGPUDrivenInstanceData>&& Instances,
        const FVector4f FrustumPlanes[6],
        const TCHAR* ValidationContext)
    {
        int32 InstanceCount = Instances.Num();

        if (!GRHIGlobals.SupportsDrawIndirect)
        {
            UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Current RHI does not support draw indirect."));
            return;
        }

        if (!OutputRenderTarget)
        {
            UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: %s requires a valid render target."), ValidationContext);
            return;
        }

        if (InstanceCount <= 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: %s requires InstanceCount > 0."), ValidationContext);
            return;
        }

        if (InstanceCount > MaxIndirectDrawInstanceCount)
        {
            UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: %s clamped InstanceCount from %d to %d."),
                ValidationContext,
                InstanceCount,
                MaxIndirectDrawInstanceCount);
            InstanceCount = MaxIndirectDrawInstanceCount;
            Instances.SetNum(InstanceCount);
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

        TArray<FGPUDrivenInstanceData> VisibleInstanceInitialValues;
        VisibleInstanceInitialValues.SetNumZeroed(InstanceCount);

        const int32 BufferSizeBytes = Instances.Num() * sizeof(FGPUDrivenInstanceData);
        const FGPUDrivenInstanceBounds2D InstanceBounds = GetInstanceBounds2D(Instances);
        const int32 EstimatedVisibleCount = EstimateVisibleInstanceCount(Instances, FrustumPlanes);

        {
            FScopeLock Lock(&GFrustumCullResultMutex);
            GFrustumCullReadback.Reset();
            bFrustumCullReadbackReady = false;
            GFrustumCullResult = FGPUDrivenFrustumCullResult();
            GFrustumCullResult.InstanceCount = InstanceCount;
            GFrustumCullResult.EstimatedVisibleCount = EstimatedVisibleCount;
            GFrustumCullResult.BufferSizeBytes = BufferSizeBytes;
        }

        ENQUEUE_RENDER_COMMAND(GPUDrivenPipeline_ExecuteFrustumCulledIndirectDraw)(
            [Instances = MoveTemp(Instances),
                VisibleInstanceInitialValues = MoveTemp(VisibleInstanceInitialValues),
                InstanceCount,
                BufferSizeBytes,
                GridWorldMin = InstanceBounds.Min,
                GridWorldExtent = InstanceBounds.Extent,
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

                uint32 InitialSummary[FrustumCullSummaryElementCount] = {};
                FBufferRHIRef SummaryBuffer = UE::RHIResourceUtils::CreateBufferFromArray<uint32>(
                    RHICmdList,
                    TEXT("GPUDrivenPipeline.FrustumCull.Summary"),
                    EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::SourceCopy,
                    ERHIAccess::UAVCompute,
                    TConstArrayView<uint32>(InitialSummary, FrustumCullSummaryElementCount));

                if (!InstanceBuffer.IsValid() || !VisibleInstanceBuffer.IsValid() || !IndirectArgsBuffer.IsValid() || !SummaryBuffer.IsValid())
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

                FUnorderedAccessViewRHIRef SummaryUAV = RHICmdList.CreateUnorderedAccessView(
                    SummaryBuffer,
                    FRHIViewDesc::CreateBufferUAV()
                        .SetType(FRHIViewDesc::EBufferType::Typed)
                        .SetFormat(PF_R32_UINT));

                if (!InstanceDataSRV.IsValid() || !VisibleInstanceDataSRV.IsValid() || !VisibleInstanceDataUAV.IsValid() || !IndirectArgsUAV.IsValid() || !SummaryUAV.IsValid())
                {
                    UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Failed to create frustum culling SRV/UAV resources."));
                    return;
                }

                FFrustumCullInstancesShader::FParameters CullingParameters;
                CullingParameters.InstanceData = InstanceDataSRV;
                CullingParameters.OutVisibleInstanceData = VisibleInstanceDataUAV;
                CullingParameters.OutIndirectArgs = IndirectArgsUAV;
                CullingParameters.OutSummary = SummaryUAV;
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

                RHICmdList.Transition(FRHITransitionInfo(SummaryBuffer, ERHIAccess::UAVCompute, ERHIAccess::CopySrc));
                RHICmdList.Transition(FRHITransitionInfo(VisibleInstanceBuffer, ERHIAccess::UAVCompute, ERHIAccess::SRVGraphics));
                RHICmdList.Transition(FRHITransitionInfo(IndirectArgsBuffer, ERHIAccess::UAVCompute, ERHIAccess::IndirectArgs));
                RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture, ERHIAccess::SRVMask, ERHIAccess::RTV));

                TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback =
                    MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("GPUDrivenPipeline.FrustumCullReadback"));
                Readback->EnqueueCopy(RHICmdList, SummaryBuffer, FrustumCullSummarySizeBytes);

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
                VertexParameters.GridWorldMin = GridWorldMin;
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

                {
                    FScopeLock Lock(&GFrustumCullResultMutex);
                    GFrustumCullReadback = Readback;
                    GFrustumCullResult.CpuCullDispatchTimeMs = CullingDispatchTimeMs;
                    GFrustumCullResult.CpuDrawDispatchTimeMs = DrawDispatchTimeMs;
                }

                UE_LOG(LogTemp, Log, TEXT("GPUDrivenPipeline: Frustum culled indirect draw submitted %d source instances to %s (%dx%d, estimated-visible=%d, instance-bounds-min=(%.1f, %.1f), instance-bounds-extent=(%.1f, %.1f), instance-bytes=%d, cpu-cull=%.3f ms, cpu-draw=%.3f ms, readback=pending)."),
                    InstanceCount,
                    *RenderTargetName.ToString(),
                    TextureSize.X,
                    TextureSize.Y,
                    EstimatedVisibleCount,
                    GridWorldMin.X,
                    GridWorldMin.Y,
                    GridWorldExtent.X,
                    GridWorldExtent.Y,
                    BufferSizeBytes,
                    CullingDispatchTimeMs,
                    DrawDispatchTimeMs);
            });
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
            VertexParameters.GridWorldMin = FVector2f::ZeroVector;
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
    FVector4f FrustumPlanes[6];
    BuildTestFrustumPlanes(GetGridWorldExtent(InstanceCount), FrustumPlanes);
    ExecuteFrustumCulledIndirectDrawInternal(
        OutputRenderTarget,
        GenerateTestInstances(InstanceCount),
        FrustumPlanes,
        TEXT("ExecuteTestFrustumCulledIndirectDraw"));
}

void UGPUDrivenIndirectDrawInterface::ExecuteCameraFrustumCulledIndirectDraw(
    UTextureRenderTarget2D* OutputRenderTarget,
    AActor* CameraActor,
    int32 InstanceCount,
    float FieldOfViewDegrees,
    float NearPlane,
    float FarPlane)
{
    if (!CameraActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: ExecuteCameraFrustumCulledIndirectDraw requires a valid camera actor."));
        return;
    }

    if (!OutputRenderTarget)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: ExecuteCameraFrustumCulledIndirectDraw requires a valid render target."));
        return;
    }

    const float AspectRatio =
        OutputRenderTarget->SizeY > 0
            ? static_cast<float>(OutputRenderTarget->SizeX) / static_cast<float>(OutputRenderTarget->SizeY)
            : 1.0f;

    FVector4f FrustumPlanes[6];
    FGPUDrivenCameraViewData CameraView;
    if (!BuildCameraFrustumPlanes(CameraActor, FieldOfViewDegrees, AspectRatio, NearPlane, FarPlane, FrustumPlanes, &CameraView))
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Failed to build camera frustum planes from %s."), *CameraActor->GetName());
        return;
    }

    ExecuteFrustumCulledIndirectDrawInternal(
        OutputRenderTarget,
        GenerateCameraAnchoredTestInstances(InstanceCount, CameraView),
        FrustumPlanes,
        TEXT("ExecuteCameraFrustumCulledIndirectDraw"));
}

void UGPUDrivenIndirectDrawInterface::ExecutePlaneCameraFrustumCulledIndirectDraw(
    UTextureRenderTarget2D* OutputRenderTarget,
    AActor* CameraActor,
    AActor* PlaneActor,
    int32 InstanceCount,
    float FieldOfViewDegrees,
    float NearPlane,
    float FarPlane)
{
    if (!CameraActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: ExecutePlaneCameraFrustumCulledIndirectDraw requires a valid camera actor."));
        return;
    }

    if (!PlaneActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: ExecutePlaneCameraFrustumCulledIndirectDraw requires a valid plane actor."));
        return;
    }

    if (!OutputRenderTarget)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: ExecutePlaneCameraFrustumCulledIndirectDraw requires a valid render target."));
        return;
    }

    const float AspectRatio =
        OutputRenderTarget->SizeY > 0
            ? static_cast<float>(OutputRenderTarget->SizeX) / static_cast<float>(OutputRenderTarget->SizeY)
            : 1.0f;

    FVector4f FrustumPlanes[6];
    if (!BuildCameraFrustumPlanes(CameraActor, FieldOfViewDegrees, AspectRatio, NearPlane, FarPlane, FrustumPlanes))
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Failed to build camera frustum planes from %s."), *CameraActor->GetName());
        return;
    }

    FVector PlaneBoundsOrigin = FVector::ZeroVector;
    FVector PlaneBoundsExtent = FVector::ZeroVector;
    PlaneActor->GetActorBounds(false, PlaneBoundsOrigin, PlaneBoundsExtent);

    if (PlaneBoundsExtent.X <= KINDA_SMALL_NUMBER || PlaneBoundsExtent.Y <= KINDA_SMALL_NUMBER)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Plane actor %s has invalid XY bounds (origin=(%.1f, %.1f, %.1f), extent=(%.1f, %.1f, %.1f))."),
            *PlaneActor->GetName(),
            PlaneBoundsOrigin.X,
            PlaneBoundsOrigin.Y,
            PlaneBoundsOrigin.Z,
            PlaneBoundsExtent.X,
            PlaneBoundsExtent.Y,
            PlaneBoundsExtent.Z);
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("GPUDrivenPipeline: Using plane actor %s for camera frustum culling target (origin=(%.1f, %.1f, %.1f), extent=(%.1f, %.1f, %.1f))."),
        *PlaneActor->GetName(),
        PlaneBoundsOrigin.X,
        PlaneBoundsOrigin.Y,
        PlaneBoundsOrigin.Z,
        PlaneBoundsExtent.X,
        PlaneBoundsExtent.Y,
        PlaneBoundsExtent.Z);

    ExecuteFrustumCulledIndirectDrawInternal(
        OutputRenderTarget,
        GeneratePlaneBoundedTestInstances(InstanceCount, PlaneBoundsOrigin, PlaneBoundsExtent),
        FrustumPlanes,
        TEXT("ExecutePlaneCameraFrustumCulledIndirectDraw"));
}

bool UGPUDrivenIndirectDrawInterface::GetLastFrustumCullResult(FGPUDrivenFrustumCullResult& OutResult)
{
    TryResolveFrustumCullReadback_GameThread();

    FScopeLock Lock(&GFrustumCullResultMutex);
    OutResult = GFrustumCullResult;
    return bFrustumCullReadbackReady;
}
