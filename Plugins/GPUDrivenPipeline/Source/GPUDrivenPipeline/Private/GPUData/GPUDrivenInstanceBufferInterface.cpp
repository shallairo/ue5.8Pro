// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPUData/GPUDrivenInstanceBufferInterface.h"

#include "GPUData/GPUDrivenInstanceData.h"
#include "GPUData/InstanceDataValidationShader.h"
#include "Misc/ScopeLock.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIGPUReadback.h"
#include "RHIResourceUtils.h"

namespace
{
    constexpr int32 MaxTestInstanceCount = 65536;
    constexpr int32 ValidationThreadGroupSize = 64;
    constexpr uint32 ValidationSummaryElementCount = 4;
    constexpr uint32 ValidationSummarySizeBytes = sizeof(uint32) * ValidationSummaryElementCount;

    FCriticalSection GValidationMutex;
    TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> GValidationReadback;
    FGPUDrivenInstanceValidationResult GValidationResult;
    bool bValidationReadbackReady = false;

    TArray<FGPUDrivenInstanceData> GenerateTestInstances(int32 InstanceCount)
    {

        //1.生成测试数据（CPU 数组）
        TArray<FGPUDrivenInstanceData> Instances;
        Instances.Reserve(InstanceCount);

        const int32 GridWidth = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(InstanceCount))));
        constexpr float Spacing = 100.0f;

        for (int32 Index = 0; Index < InstanceCount; ++Index)
        {
            const int32 X = Index % GridWidth;
            const int32 Y = Index / GridWidth;

            FGPUDrivenInstanceData Instance;
            Instance.Position = FVector3f(
                static_cast<float>(X) * Spacing,
                static_cast<float>(Y) * Spacing,
                0.0f);
            Instance.Radius = 50.0f;
            Instance.Scale = FVector3f(1.0f, 1.0f, 1.0f);
            Instance.Flags = static_cast<uint32>(Index % 4);

            Instances.Add(Instance);
        }

        return Instances;
    }

    void TryResolveReadback_GameThread()
    {
        TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> ReadbackToResolve;

        {
            FScopeLock Lock(&GValidationMutex);
            if (bValidationReadbackReady || !GValidationReadback.IsValid() || !GValidationReadback->IsReady())
            {
                return;
            }

            ReadbackToResolve = GValidationReadback;
        }

        struct TSTR_GPUDrivenPipeline_ResolveInstanceValidationReadback67 {
            static const char* CStr() {
                return "GPUDrivenPipeline_ResolveInstanceValidationReadback";
            } static const TCHAR* TStr() {
                return L"GPUDrivenPipeline_ResolveInstanceValidationReadback";
            } static constexpr ERenderCommandCategory GetCategory() {
                return  ERenderCommandCategory::Unknown;
            }
        }; 
        using FRenderCommandTag_GPUDrivenPipeline_ResolveInstanceValidationReadback67 = TRenderCommandTag<TSTR_GPUDrivenPipeline_ResolveInstanceValidationReadback67>; 
        FRenderCommandDispatcher::Enqueue<FRenderCommandTag_GPUDrivenPipeline_ResolveInstanceValidationReadback67>(
            [ReadbackToResolve](FRHICommandListImmediate&)
            {
                uint32 Summary[ValidationSummaryElementCount] = {};
                const void* ReadbackData = ReadbackToResolve->Lock(ValidationSummarySizeBytes);

                if (ReadbackData)
                {
                    FMemory::Memcpy(Summary, ReadbackData, ValidationSummarySizeBytes);
                    ReadbackToResolve->Unlock();
                }

                FScopeLock Lock(&GValidationMutex);
                GValidationResult.ProcessedCount = static_cast<int32>(Summary[0]);
                GValidationResult.ValidRadiusCount = static_cast<int32>(Summary[1]);
                GValidationResult.FlagSum = static_cast<int32>(Summary[2]);
                GValidationResult.PositionChecksum = static_cast<int32>(Summary[3]);
                bValidationReadbackReady = true;

                UE_LOG(LogTemp, Log, TEXT("GPUDrivenPipeline: Instance validation readback ready (processed=%d, valid-radius=%d, flag-sum=%d, checksum=%d)."),
                    GValidationResult.ProcessedCount,
                    GValidationResult.ValidRadiusCount,
                    GValidationResult.FlagSum,
                    GValidationResult.PositionChecksum);
            });

        // This validation helper is intentionally synchronous at readback time so Blueprint can get a stable result.
        //强行让 CPU 等待 GPU 把手里所有积压的工作干完，再执行下一步
        FlushRenderingCommands();
    }
}

void UGPUDrivenInstanceBufferInterface::UploadTestInstanceData(int32 InstanceCount)
{
    if (InstanceCount <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: UploadTestInstanceData requires InstanceCount > 0."));
        return;
    }

    if (InstanceCount > MaxTestInstanceCount)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: UploadTestInstanceData clamped InstanceCount from %d to %d."),
            InstanceCount,
            MaxTestInstanceCount);
        InstanceCount = MaxTestInstanceCount;
    }

    TArray<FGPUDrivenInstanceData> Instances = GenerateTestInstances(InstanceCount);
    const int32 BufferSizeBytes = Instances.Num() * sizeof(FGPUDrivenInstanceData);

    {
        FScopeLock Lock(&GValidationMutex);

        //2.清空上次的 readback 结果
        GValidationReadback.Reset();
        bValidationReadbackReady = false;
        GValidationResult = FGPUDrivenInstanceValidationResult();
        GValidationResult.InstanceCount = Instances.Num();
        GValidationResult.BufferSizeBytes = BufferSizeBytes;
    }


    //3.ENQUEUE_RENDER_COMMAND 的宏展开这个宏本质上做了两件事。1. 给命令起个名字（用于调试和性能分析）2. 把 lambda 包装成一个渲染命令对象，放进渲染线程的命令队列
    //FRHICommandListImmediate它封装了所有发给 GPU 的命令。你通过它来：
    //-创建资源（Buffer、Texture）
    //  - 设置状态（资源转换）
    //  - 提交工作（Dispatch、Draw）
    struct TSTR_GPUDrivenPipeline_UploadTestInstanceData126 {
        static const char* CStr() {
            return "GPUDrivenPipeline_UploadTestInstanceData";
        } static const TCHAR* TStr() {
            return L"GPUDrivenPipeline_UploadTestInstanceData";
        } static constexpr ERenderCommandCategory GetCategory() {
            return  ERenderCommandCategory::Unknown;
        }
    }; using FRenderCommandTag_GPUDrivenPipeline_UploadTestInstanceData126 = TRenderCommandTag<TSTR_GPUDrivenPipeline_UploadTestInstanceData126>; 
    FRenderCommandDispatcher::Enqueue<FRenderCommandTag_GPUDrivenPipeline_UploadTestInstanceData126>(
        [Instances = MoveTemp(Instances), InstanceCount, BufferSizeBytes](FRHICommandListImmediate& RHICmdList)
        {
            const EBufferUsageFlags InstanceBufferUsage =
                EBufferUsageFlags::StructuredBuffer |
                EBufferUsageFlags::ShaderResource;


            //4. 创建 GPU Buffer（从 CPU 数组上传到显存）
            FBufferRHIRef InstanceBuffer = UE::RHIResourceUtils::CreateBufferFromArray<FGPUDrivenInstanceData>(
                RHICmdList,
                TEXT("GPUDrivenPipeline.InstanceData"),
                InstanceBufferUsage,
                ERHIAccess::SRVCompute,
                TConstArrayView<FGPUDrivenInstanceData>(Instances));

            if (!InstanceBuffer.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Failed to create instance structured buffer."));
                return;
            }

            //5. 创建 SRV（让 shader 能读取）
            FShaderResourceViewRHIRef InstanceDataSRV = RHICmdList.CreateShaderResourceView(
                InstanceBuffer,
                FRHIViewDesc::CreateBufferSRV().SetTypeFromBuffer(InstanceBuffer));

            uint32 SummaryInitialValues[ValidationSummaryElementCount] = {};
            const EBufferUsageFlags SummaryBufferUsage =
                EBufferUsageFlags::StructuredBuffer |
                EBufferUsageFlags::ShaderResource |
                EBufferUsageFlags::UnorderedAccess |
                EBufferUsageFlags::SourceCopy;

            //6. 创建 Summary Buffer + UAV（让 shader 能写入结果）
            FBufferRHIRef SummaryBuffer = UE::RHIResourceUtils::CreateBufferFromArray<uint32>(
                RHICmdList,
                TEXT("GPUDrivenPipeline.InstanceValidationSummary"),
                SummaryBufferUsage,
                ERHIAccess::UAVCompute,
                TConstArrayView<uint32>(SummaryInitialValues, ValidationSummaryElementCount));

            if (!SummaryBuffer.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Failed to create instance validation summary buffer."));
                return;
            }

            FUnorderedAccessViewRHIRef SummaryUAV = RHICmdList.CreateUnorderedAccessView(
                SummaryBuffer,
                FRHIViewDesc::CreateBufferUAV().SetTypeFromBuffer(SummaryBuffer));

            if (!InstanceDataSRV.IsValid() || !SummaryUAV.IsValid())
            {
                UE_LOG(LogTemp, Warning, TEXT("GPUDrivenPipeline: Failed to create instance buffer SRV or summary UAV."));
                return;
            }

            //7. 绑定参数 
            FInstanceDataValidationShader::FParameters PassParameters;
            PassParameters.InstanceData = InstanceDataSRV;
            PassParameters.OutSummary = SummaryUAV;
            PassParameters.InstanceCount = static_cast<uint32>(InstanceCount);

            TShaderMapRef<FInstanceDataValidationShader> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            const FIntVector GroupCount(
                FMath::DivideAndRoundUp(InstanceCount, ValidationThreadGroupSize),
                1,
                1);


            const double StartTime = FPlatformTime::Seconds();
            //8. Dispatch compute shader
            FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, PassParameters, GroupCount);
            const double EndTime = FPlatformTime::Seconds();
            const float DispatchTimeMs = static_cast<float>((EndTime - StartTime) * 1000.0);

            //9. 资源状态转换 + 发起 readback
            RHICmdList.Transition(FRHITransitionInfo(SummaryBuffer, ERHIAccess::UAVCompute, ERHIAccess::CopySrc));

            TSharedPtr<FRHIGPUBufferReadback, ESPMode::ThreadSafe> Readback =
                MakeShared<FRHIGPUBufferReadback, ESPMode::ThreadSafe>(TEXT("GPUDrivenPipeline.InstanceValidationReadback"));
            Readback->EnqueueCopy(RHICmdList, SummaryBuffer, ValidationSummarySizeBytes);

            {
                FScopeLock Lock(&GValidationMutex);
                GValidationReadback = Readback;
                GValidationResult.CpuDispatchTimeMs = DispatchTimeMs;
            }

            UE_LOG(LogTemp, Log, TEXT("GPUDrivenPipeline: Uploaded %d instance records (%d bytes) and dispatched InstanceDataValidation (groups=%d,%d,%d, cpu-dispatch=%.3f ms)."),
                InstanceCount,
                BufferSizeBytes,
                GroupCount.X,
                GroupCount.Y,
                GroupCount.Z,
                DispatchTimeMs);
        });
}

bool UGPUDrivenInstanceBufferInterface::GetLastInstanceDataValidationResult(FGPUDrivenInstanceValidationResult& OutResult)
{
    TryResolveReadback_GameThread();

    FScopeLock Lock(&GValidationMutex);
    OutResult = GValidationResult;
    return bValidationReadbackReady;
}
