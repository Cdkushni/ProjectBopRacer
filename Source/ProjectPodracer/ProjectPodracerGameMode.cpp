// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectPodracerGameMode.h"
#include "ProjectPodracerPlayerController.h"

AProjectPodracerGameMode::AProjectPodracerGameMode()
{
	PlayerControllerClass = AProjectPodracerPlayerController::StaticClass();
}
