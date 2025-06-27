// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProjectPodracerPawn.h"
#include "ProjectPodracerSportsCar.generated.h"

/**
 *  Sports car wheeled vehicle implementation
 */
UCLASS(abstract)
class PROJECTPODRACER_API AProjectPodracerSportsCar : public AProjectPodracerPawn
{
	GENERATED_BODY()
	
public:

	AProjectPodracerSportsCar();
};
