// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterMacros.h"
#include "ShaderParameterStruct.h"

// Declare the shader class
class GPUDRIVENPIPELINE_API FSimpleComputeShader : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FSimpleComputeShader);
    SHADER_USE_PARAMETER_STRUCT(FSimpleComputeShader, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_UAV(RWTexture2D<float4>, OutputTexture)
        SHADER_PARAMETER(FVector2f, TextureSize)
    END_SHADER_PARAMETER_STRUCT()

    // Override compilation environment
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters);

    // Modify compilation environment
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
};