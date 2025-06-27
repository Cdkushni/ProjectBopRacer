// PodRacingGame/Source/PodRacingGame/Public/PodVehicle.h
// This header defines the main PodVehicle class, which acts as our player-controlled pawn.
// It sets up input handling and owns the custom movement component.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PodVehicle.generated.h"

class UCapsuleComponent;
// Forward declaration of our custom movement component
class UPodVehicleMovementComponent;

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
	
	// Our custom movement component that handles all pod movement logic.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement", meta = (AllowPrivateAccess = "true"))
	UPodVehicleMovementComponent* PodMovementComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UCapsuleComponent* CapsuleComponent;
};