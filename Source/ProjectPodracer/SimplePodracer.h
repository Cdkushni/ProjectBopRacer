// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SimplePodracer.generated.h"

UCLASS()
class PROJECTPODRACER_API ASimplePodracer : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	ASimplePodracer();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaTime) override;

	// Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UStaticMeshComponent* PodMesh;

	// Movement properties
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float MaxSpeed = 1500.0f; // Unreal units/second

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float Acceleration = 5000.0f; // Force for throttle

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float SteeringTorque = 2000.0f; // Torque for turning

	// Input values
	float ThrottleInput;
	float SteeringInput;
	
};
