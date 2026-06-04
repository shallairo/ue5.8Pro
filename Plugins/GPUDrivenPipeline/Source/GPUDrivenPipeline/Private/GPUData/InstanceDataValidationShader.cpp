// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPUData/InstanceDataValidationShader.h"

#include "ShaderCompilerCore.h"

IMPLEMENT_GLOBAL_SHADER(FInstanceDataValidationShader,
    "/Plugin/GPUDrivenPipeline/InstanceDataValidation.usf",
    "MainCS",
    SF_Compute);

bool FInstanceDataValidationShader::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

void FInstanceDataValidationShader::ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters,
    FShaderCompilerEnvironment& OutEnvironment)
{
    OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), 64);
}
