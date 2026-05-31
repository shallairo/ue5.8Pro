// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GPUDrivenPipeline : ModuleRules
{
	public GPUDrivenPipeline(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Renderer",
				"RenderCore",
				"RHI",
				"RHICore"
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"Projects"
			}
			);
	}
}