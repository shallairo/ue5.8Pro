// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeShaderInterface.h"
#include "SimpleComputeShader.h"
#include "Engine/TextureRenderTarget2D.h"

static float GLastExecutionTime = 0.0f;

void UComputeShaderInterface::ExecuteSimpleComputeShader(UTextureRenderTarget2D* OutputRenderTarget)
{
    // TODO: Implement compute shader execution
    // This is a placeholder implementation
    if (!OutputRenderTarget)
    {
        UE_LOG(LogTemp, Warning, TEXT("OutputRenderTarget is null"));
        return;
    }

    // For now, just log that the function was called
    UE_LOG(LogTemp, Log, TEXT("ExecuteSimpleComputeShader called with render target: %s"), *OutputRenderTarget->GetName());
}

float UComputeShaderInterface::GetLastExecutionTime()
{
    return GLastExecutionTime;
}