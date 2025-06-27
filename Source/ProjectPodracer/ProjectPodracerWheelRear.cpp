// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectPodracerWheelRear.h"
#include "UObject/ConstructorHelpers.h"

UProjectPodracerWheelRear::UProjectPodracerWheelRear()
{
	AxleType = EAxleType::Rear;
	bAffectedByHandbrake = true;
	bAffectedByEngine = true;
}