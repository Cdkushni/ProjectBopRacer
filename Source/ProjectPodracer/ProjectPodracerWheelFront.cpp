// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectPodracerWheelFront.h"
#include "UObject/ConstructorHelpers.h"

UProjectPodracerWheelFront::UProjectPodracerWheelFront()
{
	AxleType = EAxleType::Front;
	bAffectedBySteering = true;
	MaxSteerAngle = 40.f;
}