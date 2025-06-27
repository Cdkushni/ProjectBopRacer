// Fill out your copyright notice in the Description page of Project Settings.


#include "SimplePodracer.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"


// Sets default values
ASimplePodracer::ASimplePodracer()
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void ASimplePodracer::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ASimplePodracer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// Called to bind functionality to input
void ASimplePodracer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

