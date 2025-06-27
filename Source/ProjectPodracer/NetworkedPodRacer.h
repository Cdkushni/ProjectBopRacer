// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "NetworkedPodRacer.generated.h"

class UPodracerMovementComponent;
class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UPodRacerMovementComponent; // Forward declare

UCLASS()
class PROJECTPODRACER_API ANetworkedPodRacer : public APawn
{
	GENERATED_BODY()

public:
	ANetworkedPodRacer();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// Expose Movement Component for Blueprint access if needed
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement", meta = (AllowPrivateAccess = "true"))
	UPodracerMovementComponent* PodMovementComponent;

	// --- Visual Components (no individual physics for core movement) ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* CockpitMesh; // This will be the root component updated by PodMovementComponent

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* EngineLeft_Mesh; // Attached to CockpitMesh

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* EngineRight_Mesh; // Attached to CockpitMesh

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USpringArmComponent* SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UCameraComponent* Camera;

private:
	void MoveForward(float Value);
	void TurnRight(float Value);
};