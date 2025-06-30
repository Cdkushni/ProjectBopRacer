// PodRacingGame/Source/PodRacingGame/Public/PodVehicle.h
// This header defines the main PodVehicle class, which acts as our player-controlled pawn.
// It sets up input handling and owns the custom movement component.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PodVehicle.generated.h"

class USpringArmComponent;
// Forward declaration of our custom movement component
class UPodVehicleMovementComponent;
class UCapsuleComponent;

UCLASS()
class PROJECTPODRACER_API APodVehicle : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	APodVehicle();
	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// --- Input Handlers ---
	// Handles input for accelerating the pod forward.
	void MoveForward(float Value);
	// Handles input for steering the pod left/right.
	void TurnRight(float Value);
	// Handles input for boosting.
	void BoostPressed();
	void BoostReleased();
	// Handles input for braking.
	void BrakePressed();
	void BrakeReleased();
	// Handles input for drifting.
	void DriftPressed();
	void DriftReleased();

	// --- Replication ---
	// Overrides to specify which properties are replicated.
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Display components
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Components")
	USceneComponent* VehicleCenterRoot;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Components")
	USceneComponent* EngineCenterPoint;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Components")
	USceneComponent* LeftEngineRoot;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Components")
	USceneComponent* RightEngineRoot;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Components")
	USpringArmComponent* PodSpringArm;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Components")
	UStaticMeshComponent* PodHullMesh;

private:
	// Our custom movement component that handles all pod movement logic.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement", meta = (AllowPrivateAccess = "true"))
	UPodVehicleMovementComponent* PodMovementComponent;

	// The main collider for the vehicle, using a CapsuleComponent
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCapsuleComponent* CapsuleComponent;
};