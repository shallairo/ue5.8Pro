// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPUData/FrustumCullShader.h"

IMPLEMENT_GLOBAL_SHADER(FFrustumCullInstancesShader,
    "/Plugin/GPUDrivenPipeline/FrustumCullInstances.usf",
    "CullInstancesCS",
    SF_Compute);

bool FFrustumCullInstancesShader::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
    return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
}
