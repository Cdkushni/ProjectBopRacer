// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "ReplicatedSimRayCastVehicle.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UBoxComponent;
class URayCastVehicleMovementComponent;

UCLASS()
class PROJECTPODRACER_API AReplicatedSimRayCastVehicle : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	AReplicatedSimRayCastVehicle();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBoxComponent* BoxCollider;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* Pivot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* FLeftSuspensionRoot;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* FRightSuspensionRoot;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* RLeftSuspensionRoot;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* RRightSuspensionRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* HullMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USpringArmComponent* SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UCameraComponent* Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	URayCastVehicleMovementComponent* MovementComponent;

private:
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputMappingContext* DefaultMappingContext;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* AccelerateAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SteerAction;
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* DriftAction;

	void Accelerate(const FInputActionValue& Value);
	void Steer(const FInputActionValue& Value);
	void StartDrift(const FInputActionValue& Value);
	void StopDrift(const FInputActionValue& Value);
	
};
