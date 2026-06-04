// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FGPUDrivenInstanceData
{
    FVector3f Position = FVector3f::ZeroVector;
    float Radius = 0.0f;
    FVector3f Scale = FVector3f::OneVector;
    uint32 Flags = 0;
};
