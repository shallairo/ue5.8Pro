// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterMacros.h"
#include "ShaderParameterStruct.h"

class GPUDRIVENPIPELINE_API FInstanceDataValidationShader : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FInstanceDataValidationShader);
    SHADER_USE_PARAMETER_STRUCT(FInstanceDataValidationShader, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_SRV(StructuredBuffer<FGPUDrivenInstanceData>, InstanceData)
        SHADER_PARAMETER_UAV(RWStructuredBuffer<uint>, OutSummary)
        SHADER_PARAMETER(uint32, InstanceCount)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
};
