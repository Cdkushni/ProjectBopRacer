// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "SimulatedRayCastVehicle.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UBoxComponent;

UCLASS(Blueprintable)
class PROJECTPODRACER_API ASimulatedRayCastVehicle : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	ASimulatedRayCastVehicle();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION()
	void SuspensionCast(USceneComponent* SuspensionAxisRef);

	UFUNCTION()
	void CalculateAcceleration();

	UFUNCTION()
	void AccelerateVehicle(USceneComponent* SuspensionAxisRef, FHitResult HitResult);

	UFUNCTION(BlueprintPure)
	bool IsOnGround();

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBoxComponent* BoxCollider; // This will be the Box collider root component

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* Pivot; // This will be drift pivot

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* FLeftSuspensionRoot;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* FRightSuspensionRoot;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* RLeftSuspensionRoot;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* RRightSuspensionRoot;
	
	// --- Visual Components (no individual physics for core movement) ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* HullMesh; // This will be the root component updated by PodMovementComponent

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USpringArmComponent* SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UCameraComponent* Camera;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ConfigData")
	float TargetHoverHeight = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ConfigData")
	float SuspensionStrengthMultiplier = 90000.f; // TODO: Multiply by mass to make it mass dependent

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ConfigData")
	float SpringStrength = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ConfigData")
	float SpringDamper = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ConfigData")
	float AccelerationForce = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ConfigData")
	float MaxAcceleration = 15000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ConfigData")
	float SpeedModifier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ConfigData")
	float SteeringMultiplier = 2.f;

	// Value multiplied by the acceleration input (-1 to 1) to adjust center of mass during acceleration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ConfigData")
	FVector AccelerationCenterOfMassOffset = FVector(-50.f, 0, 0);

	// Strength of gravity applied when accelerating and in the air (Should adjust to apply even without acceleration)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ConfigData")
	float AccelerationGravityStrength = -10000.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation")
	float AccelerationInput = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation")
	float Acceleration = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation")
	bool bIsDrifting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation")
	FRotator DriftRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ConfigData")
	float TorqueStrength = 1000000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool DrawDebug = true;

private:
	// Input Mapping Context
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputMappingContext* DefaultMappingContext;

	// Input Actions
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* AccelerateAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SteerAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* DriftAction;
	//UPROPERTY(EditAnywhere, Category = "Input")
	//UInputAction* MoveAction;

	//UPROPERTY(EditAnywhere, Category = "Input")
	//UInputAction* RotateAction;

	// Input handling functions
	void Accelerate(const FInputActionValue& Value);
	void Steer(const FInputActionValue& Value);
	void StartDrift(const FInputActionValue& Value);
	void StopDrift(const FInputActionValue& Value);
	//void Move(const FInputActionValue& Value);
	//void Rotate(const FInputActionValue& Value);

	// Movement properties
	//UPROPERTY(EditAnywhere, Category = "Movement")
	//float MoveSpeed = 500.0f;

	//UPROPERTY(EditAnywhere, Category = "Movement")
	//float RotationSpeed = 100.0f;
};
