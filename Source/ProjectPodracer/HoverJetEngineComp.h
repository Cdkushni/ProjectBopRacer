// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PIDController.h"
#include "Components/SceneComponent.h"
#include "HoverJetEngineComp.generated.h"

class UBoxComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTPODRACER_API UHoverJetEngineComp : public USceneComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UHoverJetEngineComp();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

public:
	UFUNCTION(BlueprintCallable, Category = "Vehicle")
	float GetSpeedPercentage() const;

	UFUNCTION(BlueprintCallable, Category = "Input")
	float GetRudderInput() const { return RudderInput; }

	// Physics calculations
	void CalculateHover(float DeltaTime);
	void CalculatePropulsion(float DeltaTime);

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBoxComponent* BoxCollider; // This will be the Box collider root component

	// --- Visual Components (no individual physics for core movement) ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* HullMesh; // This will be the root component updated by PodMovementComponent

		// Config Data

	// The force that the engine generates
	UPROPERTY(EditAnywhere, Category = "ConfigData|DriveSettings")
	float DriveForce = 500000.f;
	// The percentage of velocity the ship maintains when not thrusting (e.g., a value of .99 means the ship loses 1% velocity when not thrusting)
	UPROPERTY(EditAnywhere, Category = "ConfigData|DriveSettings")
	float SlowingVelFactor = .99f; // maintain 99% of velocity per frame
	// The percentage of velocity the ship maintains when braking.
	UPROPERTY(EditAnywhere, Category = "ConfigData|DriveSettings")
	float BrakingVelFactor = .98f; // lose 5% of velocity per frame of breaking
	// The angle that the ship "banks" into a turn
	UPROPERTY(EditAnywhere, Category = "ConfigData|DriveSettings")
	float AngleOfRoll = 30.f;
	//The height the ship maintains when hovering
	UPROPERTY(EditAnywhere, Category = "ConfigData|HoverSettings")
	float HoverHeight = 100.f;
	// The distance the ship can be above the ground before it is "falling"
	UPROPERTY(EditAnywhere, Category = "ConfigData|HoverSettings")
	float MaxGroundDist = 500.f;
	// The force of the ship's hovering
	UPROPERTY(EditAnywhere, Category = "ConfigData|HoverSettings")
	float HoverForce = 400000.f;
	// LayerMask to determine what layer the ground is on
	UPROPERTY(EditAnywhere, Category = "ConfigData|HoverSettings")
	TEnumAsByte<ECollisionChannel> GroundCollisionChannel = ECC_WorldStatic; // TODO: Use channel for this for line tracing // TODO: Unless we want to be able to hover above other players?
	// A PID Controller to smooth the ship's hovering
	UPROPERTY(EditAnywhere, Category = "ConfigData|HoverSettings")
	FPIDController HoverPID = FPIDController();
	// The max speed the ship can go
	UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
	float TerminalVelocity = 30000.f;
	// The gravity applied to the ship while it is on the ground
	UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
	float HoverGravity = 2000.f;
	// The gravity applied to the ship while it is falling
	UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
	float FallGravity = 8000.f;
	UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
	float SteeringMultiplier = 600.f;
	// Drift Multiplier: 1 == No Drift, higher equals more drift
	UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
	float DriftMultiplier = 1.5f;
	UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
	float BoostMultiplier = 3.f;
	UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
	float Mass = 100.0f; // Explicit mass in kg
	UPROPERTY(EditAnywhere, Category = "Physics")
	float LinearDamping = 1.0f; // Increased
	UPROPERTY(EditAnywhere, Category = "Physics")
	float AngularDamping = 1.0f; // Increased

	// Debug properties
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDrawDebug = true;

	UPROPERTY(EditAnywhere, Category = "Debug")
	float DebugArrowLength = 100.0f;

	UPROPERTY(EditAnywhere, Category = "Debug")
	float DebugArrowSize = 10.0f;

	// Runtime variables
	float CurrentSpeed = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputValues")
	float ThrusterInput = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputValues")
	float RudderInput = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputValues")
	bool bIsBraking = false;
	//The air resistance the ship recieves in the forward direction
	float Drag = 0.f;
	//A flag determining if the ship is currently on the ground
	bool bIsOnGround = false;
	float AccelerationInput = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputValues")
	bool bIsDrifting = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InputValues")
	bool bIsBoosting = false;
	
	// Physical Material for the box
	UPROPERTY(EditAnywhere, Category = "ConfigData")
	UPhysicalMaterial* BoxPhysicalMaterial;

private:
	// Collision handling
	UFUNCTION()
	void OnComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
};
