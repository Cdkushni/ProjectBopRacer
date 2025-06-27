// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/MovementComponent.h"
#include "RayCastVehicleMovementComponent.generated.h"

class AReplicatedSimRayCastVehicle;
class UBoxComponent;
class USceneComponent;

USTRUCT() struct FVehicleMoveInput
{
	GENERATED_BODY()

	float AccelerationInput;
	float SteeringInput;
	bool bIsDrifting;
	float TimeStamp;
	FVector Position;
	FVector Velocity;
	FRotator Rotation;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTPODRACER_API URayCastVehicleMovementComponent : public UMovementComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	URayCastVehicleMovementComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetAccelerationInput(float InAcceleration);
	void SetSteeringInput(float InSteering);
	void StartDrift();
	void StopDrift();

	UFUNCTION(BlueprintPure)
	bool IsOnGround() const;

private:
	void PerformMovement(float DeltaTime);
	void MaintainHoverHeight(float DeltaTime);
	void CalculateAcceleration(float DeltaTime);
	void ApplyInputs(float DeltaTime);
	void SaveMove(const FVehicleMoveInput& Move);
	void CorrectClientState(const FVehicleMoveInput& ServerState);

	UFUNCTION(Server, Reliable)
	void ServerUpdateInputs(float TimeStamp, float AccelerationUpdate, float Steering, bool Drifting, FVector ClientPosition, FVector ClientVelocity, FRotator ClientRotation);

	UFUNCTION(Client, Reliable)
	void ClientCorrectState(float TimeStamp, FVector ServerPosition, FVector ServerVelocity, FRotator ServerRotation);

	UPROPERTY(ReplicatedUsing=OnRep_AccelerationInput)
	float AccelerationInput;

	UPROPERTY(ReplicatedUsing=OnRep_SteeringInput)
	float SteeringInput;

	UPROPERTY(Replicated)
	bool bIsDrifting;

	UPROPERTY(Replicated)
	FRotator DriftRotation;

	UFUNCTION()
	void OnRep_AccelerationInput();

	UFUNCTION()
	void OnRep_SteeringInput();

	UPROPERTY(EditAnywhere, Category = "ConfigData")
	float TargetHoverHeight = 60.f;

	UPROPERTY(EditAnywhere, Category = "ConfigData")
	float AccelerationForce = 2000.f;

	UPROPERTY(EditAnywhere, Category = "ConfigData")
	float MaxAcceleration = 15000.f;

	UPROPERTY(EditAnywhere, Category = "ConfigData")
	float SpeedModifier = 1.f;

	UPROPERTY(EditAnywhere, Category = "ConfigData")
	float SteeringMultiplier = 2.f;

	UPROPERTY(EditAnywhere, Category = "ConfigData")
	FVector AccelerationCenterOfMassOffset = FVector(-50.f, 0, 0);

	UPROPERTY(EditAnywhere, Category = "ConfigData")
	float TorqueStrength = 1000000.f;

	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDrawDebug = true;

	float Acceleration;

	TArray<FVehicleMoveInput> MoveHistory;
	static constexpr int32 MaxMoveHistory = 100;
	float LastMoveTime;

	UPROPERTY() AReplicatedSimRayCastVehicle* OwningVehicle;
	
	UPROPERTY() UBoxComponent* BoxCollider;
};
