// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GPUData/GPUDrivenInstanceData.h"
#include "ShaderParameterMacros.h"
#include "ShaderParameterStruct.h"

class GPUDRIVENPIPELINE_API FFrustumCullInstancesShader : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FFrustumCullInstancesShader);
    SHADER_USE_PARAMETER_STRUCT(FFrustumCullInstancesShader, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_SRV(StructuredBuffer<FGPUDrivenInstanceData>, InstanceData)
        SHADER_PARAMETER_UAV(RWStructuredBuffer<FGPUDrivenInstanceData>, OutVisibleInstanceData)
        SHADER_PARAMETER_UAV(RWBuffer<uint>, OutIndirectArgs)
        SHADER_PARAMETER_UAV(RWBuffer<uint>, OutSummary)
        SHADER_PARAMETER(uint32, InstanceCount)
        SHADER_PARAMETER(FVector4f, FrustumPlane0)
        SHADER_PARAMETER(FVector4f, FrustumPlane1)
        SHADER_PARAMETER(FVector4f, FrustumPlane2)
        SHADER_PARAMETER(FVector4f, FrustumPlane3)
        SHADER_PARAMETER(FVector4f, FrustumPlane4)
        SHADER_PARAMETER(FVector4f, FrustumPlane5)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};
