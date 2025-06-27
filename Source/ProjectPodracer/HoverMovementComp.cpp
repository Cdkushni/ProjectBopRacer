// Fill out your copyright notice in the Description page of Project Settings.


#include "HoverMovementComp.h"


// Sets default values for this component's properties
UHoverMovementComp::UHoverMovementComp()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void UHoverMovementComp::BeginPlay()
{
	Super::BeginPlay();

	// ...
	
}


// Called every frame
void UHoverMovementComp::TickComponent(float DeltaTime, ELevelTick TickType,
                                       FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

