// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleComputeShader.h"
#include "ShaderCompilerCore.h"

// Implement the shader
IMPLEMENT_GLOBAL_SHADER(FSimpleComputeShader,
    "/Plugin/GPUDrivenPipeline/SimpleComputeShader.usf",
    "MainCS",
    SF_Compute);

bool FSimpleComputeShader::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

void FSimpleComputeShader::ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters,
    FShaderCompilerEnvironment& OutEnvironment)
{
    // Add any compilation flags if needed
    OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), 8);
    OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), 8);
}