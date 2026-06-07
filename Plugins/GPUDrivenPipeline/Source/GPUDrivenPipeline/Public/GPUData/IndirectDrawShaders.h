// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GPUData/GPUDrivenInstanceData.h"
#include "ShaderParameterMacros.h"
#include "ShaderParameterStruct.h"

class GPUDRIVENPIPELINE_API FIndirectDrawArgsShader : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FIndirectDrawArgsShader);
    SHADER_USE_PARAMETER_STRUCT(FIndirectDrawArgsShader, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_UAV(RWBuffer<uint>, OutIndirectArgs)
        SHADER_PARAMETER(uint32, InstanceCount)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
};

class GPUDRIVENPIPELINE_API FIndirectDrawInstanceVS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FIndirectDrawInstanceVS);
    SHADER_USE_PARAMETER_STRUCT(FIndirectDrawInstanceVS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_SRV(StructuredBuffer<FGPUDrivenInstanceData>, InstanceData)
        SHADER_PARAMETER(uint32, InstanceCount)
        SHADER_PARAMETER(FVector2f, RenderTargetSize)
        SHADER_PARAMETER(FVector2f, GridWorldExtent)
        SHADER_PARAMETER(float, QuadPixelSize)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
};

class GPUDRIVENPIPELINE_API FIndirectDrawInstancePS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FIndirectDrawInstancePS);
    SHADER_USE_PARAMETER_STRUCT(FIndirectDrawInstancePS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);
};
