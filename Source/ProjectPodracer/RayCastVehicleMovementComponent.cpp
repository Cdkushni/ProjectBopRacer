// Fill out your copyright notice in the Description page of Project Settings.


#include "RayCastVehicleMovementComponent.h"
#include "ReplicatedSimRayCastVehicle.h"
#include "Components/BoxComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/UnrealNetwork.h"


// Sets default values for this component's properties
URayCastVehicleMovementComponent::URayCastVehicleMovementComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	
	AccelerationInput = 0.f;
	SteeringInput = 0.f;
	bIsDrifting = false;
	Acceleration = 0.f;
	LastMoveTime = 0.f;
}

void URayCastVehicleMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URayCastVehicleMovementComponent, AccelerationInput);
	DOREPLIFETIME(URayCastVehicleMovementComponent, SteeringInput);
	DOREPLIFETIME(URayCastVehicleMovementComponent, bIsDrifting);
	DOREPLIFETIME(URayCastVehicleMovementComponent, DriftRotation);
}

// Called when the game starts
void URayCastVehicleMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (Owner)
	{
		OwningVehicle = Cast<AReplicatedSimRayCastVehicle>(Owner);
		BoxCollider = Cast<UBoxComponent>(Owner->GetRootComponent());
	}
	
}


// Called every frame
void URayCastVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                                     FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!BoxCollider)
		return;

	// Sub-stepping for stability
	const float SubStepTime = DeltaTime / 4.f;
	for (int32 i = 0; i < 4; ++i)
	{
		PerformMovement(SubStepTime);
	}

	// Send inputs to server if locally controlled
	if (OwningVehicle->IsLocallyControlled())
	{
		FVehicleMoveInput MoveInput;
		MoveInput.AccelerationInput = AccelerationInput;
		MoveInput.SteeringInput = SteeringInput;
		MoveInput.bIsDrifting = bIsDrifting;
		MoveInput.TimeStamp = GetWorld()->GetTimeSeconds();
		MoveInput.Position = BoxCollider->GetComponentLocation();
		MoveInput.Velocity = BoxCollider->GetPhysicsLinearVelocity();
		MoveInput.Rotation = BoxCollider->GetComponentRotation();

		SaveMove(MoveInput);
		ServerUpdateInputs(MoveInput.TimeStamp, MoveInput.AccelerationInput, MoveInput.SteeringInput, MoveInput.bIsDrifting,
			MoveInput.Position, MoveInput.Velocity, MoveInput.Rotation);
	}
	else
	{
		// Interpolate for remote players
		FVector CurrentLocation = BoxCollider->GetComponentLocation();
		FVector TargetLocation = BoxCollider->GetComponentLocation(); // Use replicated position
		FVector InterpolatedLocation = FMath::VInterpTo(CurrentLocation, TargetLocation, DeltaTime, 10.f);
		BoxCollider->SetWorldLocation(InterpolatedLocation);

		FRotator CurrentRotation = BoxCollider->GetComponentRotation();
		FRotator TargetRotation = BoxCollider->GetComponentRotation(); // Use replicated rotation
		FRotator InterpolatedRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, 10.f);
		BoxCollider->SetWorldRotation(InterpolatedRotation);
	}
}

void URayCastVehicleMovementComponent::PerformMovement(float DeltaTime)
{
	if (!BoxCollider) return;
	CalculateAcceleration(DeltaTime);
	MaintainHoverHeight(DeltaTime);
	ApplyInputs(DeltaTime);
}
void URayCastVehicleMovementComponent::MaintainHoverHeight(float DeltaTime)
{
	if (!BoxCollider) return;
	FVector Start = BoxCollider->GetComponentLocation();
	FVector End = Start - FVector(0, 0, TargetHoverHeight + 100.f);

	FHitResult HitResult;
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(GetOwner());

	bool bHit = UKismetSystemLibrary::LineTraceSingle(
		GetWorld(), Start, End, UEngineTypes::ConvertToTraceType(ECC_Visibility),
		false, ActorsToIgnore, bDrawDebug ? EDrawDebugTrace::ForOneFrame : EDrawDebugTrace::None, HitResult, true);

	if (bHit && HitResult.bBlockingHit && HitResult.Distance <= TargetHoverHeight + 50.f)
	{
		BoxCollider->SetSimulatePhysics(false);
		FVector TargetLocation = HitResult.Location + FVector(0, 0, TargetHoverHeight);
		BoxCollider->SetWorldLocation(TargetLocation);
		BoxCollider->SetPhysicsLinearVelocity(FVector(BoxCollider->GetPhysicsLinearVelocity().X, BoxCollider->GetPhysicsLinearVelocity().Y, 0.f));
	}
	else
	{
		BoxCollider->SetSimulatePhysics(true);
	}

	Acceleration = FMath::Lerp(0.f, MaxAcceleration, FMath::Abs(AccelerationInput)) * AccelerationInput;
	AccelerationInput = FMath::FInterpTo(AccelerationInput, 0.f, DeltaTime, 0.3f);
}

void URayCastVehicleMovementComponent::CalculateAcceleration(float DeltaTime) { Acceleration = FMath::Lerp(0.f, MaxAcceleration, FMath::Abs(AccelerationInput)) * AccelerationInput; AccelerationInput = FMath::FInterpTo(AccelerationInput, 0.f, DeltaTime, 0.3f); }

void URayCastVehicleMovementComponent::ApplyInputs(float DeltaTime)
{
	FVector NewCenterOfMass = AccelerationCenterOfMassOffset * AccelerationInput;
	BoxCollider->SetCenterOfMass(NewCenterOfMass);

	FVector AccelDir = BoxCollider->GetForwardVector() * AccelerationForce * AccelerationInput * BoxCollider->GetMass() * SpeedModifier * DeltaTime;
	BoxCollider->AddForce(AccelDir);

	if (bIsDrifting)
	{
		BoxCollider->AddTorqueInRadians(FVector(0, 0, SteeringInput * TorqueStrength * (AccelerationInput * 4.0f)) * DeltaTime);
	}
	else
	{
		BoxCollider->AddTorqueInRadians(FVector(0, 0, SteeringInput * TorqueStrength * (AccelerationInput * SteeringMultiplier)) * DeltaTime);
	}
}

void URayCastVehicleMovementComponent::SetAccelerationInput(float InAcceleration) { AccelerationInput = FMath::Clamp(InAcceleration, -1.f, 1.f); }

void URayCastVehicleMovementComponent::SetSteeringInput(float InSteering) { SteeringInput = FMath::Clamp(InSteering, -1.f, 1.f); }

void URayCastVehicleMovementComponent::StartDrift() { bIsDrifting = true; SteeringMultiplier = 4.0f; DriftRotation = FRotator::ZeroRotator; }

void URayCastVehicleMovementComponent::StopDrift() { bIsDrifting = false; SteeringMultiplier = 2.0f; }

bool URayCastVehicleMovementComponent::IsOnGround() const
{
	FHitResult HitResult; TArray<AActor*> ActorsToIgnore; ActorsToIgnore.Add(GetOwner());
	FVector Start = GetOwner()->GetActorLocation();
	FVector End = Start - FVector(0, 0, TargetHoverHeight + 50.f);

	bool bHit = UKismetSystemLibrary::LineTraceSingle(
		GetWorld(), Start, End, UEngineTypes::ConvertToTraceType(ECC_Visibility),
		false, ActorsToIgnore, bDrawDebug ? EDrawDebugTrace::ForOneFrame : EDrawDebugTrace::None, HitResult, true);
	return bHit && HitResult.bBlockingHit && HitResult.Distance <= TargetHoverHeight + 50.f;
}

void URayCastVehicleMovementComponent::SaveMove(const FVehicleMoveInput& Move) { MoveHistory.Add(Move); if (MoveHistory.Num() > MaxMoveHistory) { MoveHistory.RemoveAt(0); } }

void URayCastVehicleMovementComponent::CorrectClientState(const FVehicleMoveInput& ServerState)
{
	if (!BoxCollider) return;
	BoxCollider->SetWorldLocation(ServerState.Position);
	BoxCollider->SetWorldRotation(ServerState.Rotation);
	BoxCollider->SetPhysicsLinearVelocity(ServerState.Velocity);

	// Replay moves after the corrected timestamp
	TArray<FVehicleMoveInput> MovesToReplay;
	for (const FVehicleMoveInput& Move : MoveHistory)
	{
		if (Move.TimeStamp > ServerState.TimeStamp)
		{
			MovesToReplay.Add(Move);
		}
	}

	MoveHistory = MovesToReplay;

	for (const FVehicleMoveInput& Move : MovesToReplay)
	{
		AccelerationInput = Move.AccelerationInput;
		SteeringInput = Move.SteeringInput;
		bIsDrifting = Move.bIsDrifting;
		PerformMovement(GetWorld()->GetDeltaSeconds());
	}
}

void URayCastVehicleMovementComponent::ServerUpdateInputs_Implementation(float TimeStamp, float AccelerationUpdate, float Steering, bool Drifting, FVector ClientPosition, FVector ClientVelocity, FRotator ClientRotation)
{
	if (!BoxCollider) return;
	// Validate inputs
	AccelerationInput = FMath::Clamp(Acceleration, -1.f, 1.f);
	SteeringInput = FMath::Clamp(Steering, -1.f, 1.f);
	bIsDrifting = Drifting;

	PerformMovement(GetWorld()->GetDeltaSeconds());

	FVector ServerPosition = BoxCollider->GetComponentLocation();
	FVector ServerVelocity = BoxCollider->GetPhysicsLinearVelocity();
	FRotator ServerRotation = BoxCollider->GetComponentRotation();

	// Check for significant mismatch
	const float PositionThreshold = 100.f; // cm
	const float VelocityThreshold = 200.f; // cm/s
	const float RotationThreshold = 5.f; // degrees

	if (FVector::DistSquared(ClientPosition, ServerPosition) > FMath::Square(PositionThreshold) ||
		FVector::DistSquared(ClientVelocity, ServerVelocity) > FMath::Square(VelocityThreshold) ||
		FMath::Abs(ClientRotation.Yaw - ServerRotation.Yaw) > RotationThreshold)
	{
		ClientCorrectState(TimeStamp, ServerPosition, ServerVelocity, ServerRotation);
	}
}

void URayCastVehicleMovementComponent::ClientCorrectState_Implementation(float TimeStamp, FVector ServerPosition, FVector ServerVelocity, FRotator ServerRotation)
{
	FVehicleMoveInput ServerState;
	ServerState.TimeStamp = TimeStamp;
	ServerState.Position = ServerPosition;
	ServerState.Velocity = ServerVelocity;
	ServerState.Rotation = ServerRotation;
	CorrectClientState(ServerState);
}

void URayCastVehicleMovementComponent::OnRep_AccelerationInput() { if (!OwningVehicle->IsLocallyControlled()) { PerformMovement(GetWorld()->GetDeltaSeconds()); } }

void URayCastVehicleMovementComponent::OnRep_SteeringInput() { if (!OwningVehicle->IsLocallyControlled()) { PerformMovement(GetWorld()->GetDeltaSeconds()); } }