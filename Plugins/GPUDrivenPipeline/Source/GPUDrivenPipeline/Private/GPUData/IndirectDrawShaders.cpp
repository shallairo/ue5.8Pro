// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPUData/IndirectDrawShaders.h"

#include "ShaderCompilerCore.h"

IMPLEMENT_GLOBAL_SHADER(FIndirectDrawArgsShader,
    "/Plugin/GPUDrivenPipeline/IndirectDrawInstances.usf",
    "BuildIndirectArgsCS",
    SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FIndirectDrawInstanceVS,
    "/Plugin/GPUDrivenPipeline/IndirectDrawInstances.usf",
    "MainVS",
    SF_Vertex);

IMPLEMENT_GLOBAL_SHADER(FIndirectDrawInstancePS,
    "/Plugin/GPUDrivenPipeline/IndirectDrawInstances.usf",
    "MainPS",
    SF_Pixel);

bool FIndirectDrawArgsShader::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

void FIndirectDrawArgsShader::ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters,
    FShaderCompilerEnvironment& OutEnvironment)
{
    FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
}

bool FIndirectDrawInstanceVS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}

void FIndirectDrawInstanceVS::ModifyCompilationEnvironment(
    const FGlobalShaderPermutationParameters& Parameters,
    FShaderCompilerEnvironment& OutEnvironment)
{
    FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
    OutEnvironment.CompilerFlags.Add(CFLAG_IndirectDraw);
}

bool FIndirectDrawInstancePS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}
