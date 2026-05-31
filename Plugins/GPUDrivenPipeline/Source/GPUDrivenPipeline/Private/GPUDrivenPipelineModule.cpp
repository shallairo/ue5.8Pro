// Copyright Epic Games, Inc. All Rights Reserved.

#include "GPUDrivenPipelineModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FGPUDrivenPipelineModule"

void FGPUDrivenPipelineModule::StartupModule()
{
    // Register shader directory
    RegisterShaderDirectory();
}

void FGPUDrivenPipelineModule::ShutdownModule()
{
    // Cleanup if needed
}

void FGPUDrivenPipelineModule::RegisterShaderDirectory()
{
    // Get plugin base directory
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("GPUDrivenPipeline"));
    if (Plugin.IsValid())
    {
        FString PluginDir = Plugin->GetBaseDir();
        FString ShaderDir = FPaths::Combine(PluginDir, TEXT("Shaders"));
        
        // Add shader directory to shader compiler
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/GPUDrivenPipeline"), ShaderDir);
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGPUDrivenPipelineModule, GPUDrivenPipeline)