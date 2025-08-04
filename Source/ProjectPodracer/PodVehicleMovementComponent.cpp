// PodVehicleMovementComponent.cpp
// This file implements the core movement and networking logic for the PodVehicle.

#include "PodVehicleMovementComponent.h"

#include "PodVehicle.h"
#include "GameFramework/Pawn.h" // For Acknowledging position on server
#include "Net/UnrealNetwork.h" // Required for replication
#include "Components/CapsuleComponent.h" // For ground detection
#include "DrawDebugHelpers.h" // For visualizing ground trace
#include "Kismet/KismetMathLibrary.h" // For FMath::GetMappedRangeValueClamped

// Constructor: Set default values for movement parameters
UPodVehicleMovementComponent::UPodVehicleMovementComponent()
{
	// Set our component to tick every frame.
	PrimaryComponentTick.bCanEverTick = true;

	// Replication setup: This component's state will be replicated.
	// Crucial for multiplayer prediction and synchronization.
	SetIsReplicatedByDefault(true);

	// Physics Parameters - Tuned for custom velocity-based movement
	MaxSpeed = 15000.0f; // 150 m/s, very fast
	Acceleration = 2000.0f; // cm/s^2, rapid acceleration
	Deceleration = 3000.0f; // cm/s^2, natural slowdown
	LinearDamping = 0.05f; // Small linear damping

	MaxTurnRate = 200.0f; // Max degrees/s target angular velocity for steering
	TurnAcceleration = 1000.0f; // degrees/s^2, how quickly it reaches MaxTurnRate
	AngularDamping = 10.0f; // High angular damping for tight turns (stops rotation quickly when not turning)
	HighSpeedSteeringDampFactor = 0.3f; // At max speed, 30% of normal turn rate
	KeyboardSteeringInterpSpeed = 15.0f; // Very fast interpolation for keyboard input
	KeyboardSteeringReturnSpeed = 30.0f; // Very fast return to zero for keyboard

	BoostAcceleration = 15000.0f; // Strong additional acceleration from boost
	BoostMaxSpeedMultiplier = 1.8f; // 80% increase in max speed when boosting

	BrakeDeceleration = 20000.0f; // Very strong braking deceleration

	DriftTurnSpeedMultiplier = 1.5f; // Double turn speed while drifting
	DriftLinearDampingMultiplier = 0.05f; // Very low linear damping for max slide
	DriftAngularDampingMultiplier = 0.2f; // Very low angular damping, sustains spin much more
	DriftLateralSlideFactor = 0.9f; // Retain 90% of lateral velocity (very slippery)
	DriftMinSlideFactor = 0.3f; // Ensures a minimum amount of slide even at low speed

	// Simplified Mario Kart style drifting parameters
	DriftMomentumDecayRate = 0.05f; // Low decay for sustained slide
	DriftLateralContributionScale = 0.05f; // Contribution of lateral momentum to velocity
	DriftLateralMomentumMax = 5000.0f; // Max lateral momentum magnitude

	AirControlTurnFactor = 0.4f; // Weaker turn in air
	AirControlPitchFactor = 0.6f; // Good pitch responsiveness in air
	AirControlRollFactor = 0.7f; // Decent roll control in air

	GroundTraceDistance = 50.0f; // How far below to check for ground
	GroundDetectionRadius = 60.0f; // Radius for ground trace (slightly less than capsule radius)
	GroundCollisionChannel = ECC_Visibility; // Default, consider custom Trace Channel

	DragCoefficient = 10.0f; // Interpolation speed for air resistance

	CorrectionThreshold = 10.0f; // Correct if discrepancy > 10cm
	GravityScale = 980.0f; // Approx. 1G in cm/s^2

	// Visual config
	AngleOfRoll = 30.0f;

	MoveForwardInput = 0.0f;
	TurnRightInput = 0.0f;
	bIsBoosting = false;
	bIsBraking = false;
	bIsDrifting = false;
	CurrentMoveID = 0; // Initialize move ID counter
	CurrentAngularYawVelocity = 0.0f; // Initialize angular velocity
	SmoothedRudderInput = 0.0f;
}

void UPodVehicleMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	OwningPodVehicle = Cast<APodVehicle>(GetOwner());
}

// Core tick function for movement and network handling
void UPodVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsValid(UpdatedComponent) || ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return;
	}

	// Input smoothing for local client (keyboard friendliness)
	if (OwnerPawn->IsLocallyControlled())
	{
		float InterpSpeed = FMath::IsNearlyZero(TurnRightInput) ? KeyboardSteeringReturnSpeed : KeyboardSteeringInterpSpeed;
		SmoothedRudderInput = FMath::FInterpTo(SmoothedRudderInput, TurnRightInput, DeltaTime, InterpSpeed);
	}

	// Determine movement application based on role
	if (GetOwnerRole() == ROLE_Authority) // Server authoritative
	{
		FRotator NewRotation = UpdatedComponent->GetComponentRotation();
		ApplyMovementLogic(MoveForwardInput, TurnRightInput, bIsBoosting, bIsBraking, bIsDrifting, DeltaTime, Velocity, NewRotation, CurrentAngularYawVelocity);
		Client_AcknowledgeMove(CurrentMoveID, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentRotation(), Velocity, CurrentAngularYawVelocity);
	}
	else if (OwnerPawn->IsLocallyControlled()) // Client prediction
	{
		CurrentMoveID++;
		FClientMoveData CurrentMove(MoveForwardInput, SmoothedRudderInput, bIsBoosting, bIsBraking, bIsDrifting, CurrentMoveID, DeltaTime);
		ClientMoveHistory.Add(CurrentMove);

		FRotator NewRotation = UpdatedComponent->GetComponentRotation();
		ApplyMovementLogic(MoveForwardInput, SmoothedRudderInput, bIsBoosting, bIsBraking, bIsDrifting, DeltaTime, Velocity, NewRotation, CurrentAngularYawVelocity);

		Server_ProcessMove(CurrentMove);
	}

	// Visuals for all roles - use appropriate input
	float VisualTurnInput = OwnerPawn->IsLocallyControlled() ? SmoothedRudderInput : TurnRightInput;
	HandleEngineHoveringVisuals(VisualTurnInput, DeltaTime);
}

// Input setters (called by PodVehicle)
void UPodVehicleMovementComponent::SetMoveForwardInput(float Value)
{
	MoveForwardInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void UPodVehicleMovementComponent::SetTurnRightInput(float Value)
{
	TurnRightInput = FMath::Clamp(Value, -1.0f, 1.0f);
}

void UPodVehicleMovementComponent::SetBoostInput(bool bNewState)
{
	bIsBoosting = bNewState;
}

void UPodVehicleMovementComponent::SetBrakeInput(bool bNewState)
{
	bIsBraking = bNewState;
}

void UPodVehicleMovementComponent::SetDriftInput(bool bNewState)
{
	bIsDrifting = bNewState;
}

// Unified movement logic - split into sub-functions for clarity
void UPodVehicleMovementComponent::ApplyMovementLogic(float InMoveForwardInput, float InTurnRightInput, bool InIsBoosting, bool InIsBraking, bool InIsDrifting, float InDeltaTime, FVector& OutVelocity, FRotator& OutRotation, float& OutAngularYawVelocity)
{
	if (InDeltaTime <= 0.0f) return;

	// Ground detection and normal
	FHitResult GroundHit;
	FVector GroundNormal = GetGroundNormal(GroundHit);

	bool bGrounded = IsGrounded();
	float ControlMultiplier = bGrounded ? 1.0f : AirControlTurnFactor;
	float EffectiveMaxSpeed = MaxSpeed * (InIsBoosting ? BoostMaxSpeedMultiplier : 1.0f);

	FVector ForwardVector = UpdatedComponent->GetForwardVector();
	FVector RightVector = UpdatedComponent->GetRightVector();

	// Handle drift state transitions
	HandleDriftState(InIsDrifting, InDeltaTime, OutVelocity, ForwardVector);

	// Apply damping
	ApplyDamping(InDeltaTime, InIsDrifting, bGrounded, OutVelocity, GroundNormal);

	// Apply lateral friction/drift
	ApplyLateralFriction(InDeltaTime, InIsDrifting, bGrounded, OutVelocity, RightVector);

	// Cap speed
	OutVelocity = CapSpeed(OutVelocity, EffectiveMaxSpeed, bGrounded);

	// Apply acceleration/braking
	ApplyAcceleration(InDeltaTime, InMoveForwardInput, InIsBoosting, InIsBraking, ControlMultiplier, ForwardVector, OutVelocity);

	// Apply steering/angular
	ApplySteering(InDeltaTime, InTurnRightInput, InIsDrifting, bGrounded, EffectiveMaxSpeed, OutRotation, OutAngularYawVelocity);

	// Apply gravity if airborne
	if (!bGrounded)
	{
		OutVelocity.Z -= GravityScale * InDeltaTime;
	}

	// Move the component
	FHitResult HitResult;
	SafeMoveUpdatedComponent(OutVelocity * InDeltaTime, OutRotation, true, HitResult);
	if (HitResult.IsValidBlockingHit())
	{
		SlideAlongSurface(OutVelocity * InDeltaTime, 1.0f - HitResult.Time, HitResult.Normal, HitResult, true);
		OutVelocity = FVector::VectorPlaneProject(OutVelocity, HitResult.Normal);
	}
}

// Sub-function: Handle drift transitions
void UPodVehicleMovementComponent::HandleDriftState(bool InIsDrifting, float DeltaTime, FVector& OutVelocity, const FVector& ForwardVector)
{
	if (InIsDrifting && !bIsDriftingLastFrame)
	{
		DriftOriginalVelocity = OutVelocity;
		DriftDuration = 0.0f;
		bIsDriftingLastFrame = true;
	}
	else if (!InIsDrifting && bIsDriftingLastFrame)
	{
		float CurrentSpeed = OutVelocity.Size();
		OutVelocity = FMath::Lerp(OutVelocity.GetSafeNormal(), ForwardVector, 0.7f) * CurrentSpeed;
		bIsDriftingLastFrame = false;
		DriftOriginalVelocity = FVector::ZeroVector;
		DriftDuration = 0.0f;
	}
	else if (InIsDrifting)
	{
		DriftDuration += DeltaTime;
	}
}

// Sub-function: Apply damping (linear and air resistance)
void UPodVehicleMovementComponent::ApplyDamping(float DeltaTime, bool InIsDrifting, bool bGrounded, FVector& OutVelocity, const FVector& GroundNormal)
{
	float CurrentLinearDamping = LinearDamping;
	if (InIsDrifting && bGrounded)
	{
		CurrentLinearDamping *= DriftLinearDampingMultiplier;
	}
	OutVelocity *= FMath::Clamp(1.0f - CurrentLinearDamping * DeltaTime, 0.0f, 1.0f);

	// Air resistance: preserve forward, damp perpendicular
	FVector Forward = UpdatedComponent->GetForwardVector();
	FVector ForwardVel = FVector::DotProduct(OutVelocity, Forward) * Forward;
	FVector PerpVel = OutVelocity - ForwardVel;
	float DragInterp = DragCoefficient * (bGrounded ? 1.0f : 0.25f);
	PerpVel = FMath::VInterpTo(PerpVel, FVector::ZeroVector, DeltaTime, DragInterp);
	if (bGrounded)
	{
		PerpVel = FVector::VectorPlaneProject(PerpVel, GroundNormal);
	}
	OutVelocity = ForwardVel + PerpVel;
}

// Sub-function: Apply lateral friction for drifting/sliding
void UPodVehicleMovementComponent::ApplyLateralFriction(float DeltaTime, bool InIsDrifting, bool bGrounded, FVector& OutVelocity, const FVector& RightVector)
{
	if (bGrounded)
	{
		FVector LateralVel = FVector::DotProduct(OutVelocity, RightVector) * RightVector;
		float LateralDamping = InIsDrifting ? 0.02f : 20.0f;
		float SlideFactor = InIsDrifting ? DriftLateralSlideFactor : 0.0f;
		OutVelocity -= LateralVel * (1.0f - SlideFactor) * FMath::Clamp(LateralDamping * DeltaTime, 0.0f, 1.0f);

		if (!InIsDrifting && LateralVel.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			OutVelocity -= LateralVel * FMath::Clamp(20.0f * DeltaTime, 0.0f, 1.0f);
		}
	}
}

// Sub-function: Cap speed to max
FVector UPodVehicleMovementComponent::CapSpeed(const FVector& InVelocity, float Max, bool bGrounded) const
{
	FVector Vel = InVelocity;
	if (Vel.SizeSquared() > FMath::Square(Max))
	{
		Vel = Vel.GetSafeNormal() * Max;
	}
	if (!bGrounded)
	{
		Vel.Z = FMath::Clamp(Vel.Z, -GravityScale, 0.0f);
	}
	return Vel;
}

// Sub-function: Apply acceleration or braking
void UPodVehicleMovementComponent::ApplyAcceleration(float DeltaTime, float InForwardInput, bool InBoosting, bool InBraking, float ControlMul, const FVector& Forward, FVector& OutVelocity)
{
	if (InBraking)
	{
		float Speed2D = OutVelocity.Size2D();
		if (Speed2D > 0.0f)
		{
			FVector Dir = OutVelocity.GetSafeNormal2D();
			float NewSpeed = FMath::Max(0.0f, Speed2D - BrakeDeceleration * DeltaTime);
			OutVelocity = Dir * NewSpeed;
		}
	}
	else
	{
		float Accel = Acceleration + (InBoosting ? BoostAcceleration : 0.0f);
		FVector AccelVec = Forward * InForwardInput * Accel * ControlMul;
		OutVelocity += AccelVec * DeltaTime;
	}
}

// Sub-function: Apply steering and angular velocity
void UPodVehicleMovementComponent::ApplySteering(float DeltaTime, float InTurnInput, bool InDrifting, bool bGrounded, float EffectiveMaxSpeed, FRotator& OutRotation, float& OutAngularYawVelocity)
{
	if (bGrounded)
	{
		float SpeedMul = FMath::GetMappedRangeValueClamped(FVector2D(0.0f, MaxSpeed), FVector2D(1.0f, HighSpeedSteeringDampFactor), Velocity.Size());
		float CurrentTurnRate = MaxTurnRate * SpeedMul;
		float CurrentAngularAccel = TurnAcceleration;
		float CurrentAngularDamp = AngularDamping;

		if (InDrifting)
		{
			CurrentTurnRate *= DriftTurnSpeedMultiplier;
			CurrentAngularDamp *= DriftAngularDampingMultiplier;
			CurrentAngularAccel *= 0.9f;
		}

		float TargetYawVel = InTurnInput * CurrentTurnRate;
		float YawChange = TargetYawVel - OutAngularYawVelocity;
		OutAngularYawVelocity += FMath::Sign(YawChange) * FMath::Min(FMath::Abs(YawChange), CurrentAngularAccel * DeltaTime);
		OutAngularYawVelocity *= FMath::Clamp(1.0f - CurrentAngularDamp * DeltaTime, 0.0f, 1.0f);

		OutRotation.Yaw += OutAngularYawVelocity * DeltaTime;
		OutRotation.Normalize();
	}
	else // Airborne
	{
		OutRotation.Yaw += InTurnInput * AirControlTurnFactor * MaxTurnRate * DeltaTime;
		// TODO: Don't want to change pitch and roll while midair unless we have an auto correct once we land to offset it
		//OutRotation.Pitch += InTurnInput * AirControlPitchFactor * MaxTurnRate * DeltaTime; // Improved air pitch control
		//OutRotation.Roll += InTurnInput * AirControlRollFactor * MaxTurnRate * DeltaTime;
		OutAngularYawVelocity = 0.0f;
	}
}

// Improved ground normal detection with multiple traces
FVector UPodVehicleMovementComponent::GetGroundNormal(FHitResult& OutHit) const
{
	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector End = Start - FVector(0, 0, GroundTraceDistance + 50.0f); // Extra distance
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner());
	Params.bTraceComplex = true;

	FHitResult Hit;
	GetWorld()->LineTraceSingleByChannel(Hit, Start, End, GroundCollisionChannel, Params);
	OutHit = Hit;
	return Hit.IsValidBlockingHit() ? Hit.Normal.GetSafeNormal() : FVector::UpVector;
}

// IsGrounded using sphere sweep for robustness
bool UPodVehicleMovementComponent::IsGrounded() const
{
	if (!UpdatedComponent) return false;

	UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 50.0f;
	float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 50.0f;

	FVector Start = UpdatedComponent->GetComponentLocation();
	FVector End = Start - FVector(0, 0, HalfHeight + GroundTraceDistance);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner());

	FCollisionShape Shape = FCollisionShape::MakeSphere(Radius * 0.9f); // Slightly smaller for edge cases

	FHitResult Hit;
	bool bHit = GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, GroundCollisionChannel, Shape, Params);
	return bHit;
}

// Visual handling - now works on remote vehicles
void UPodVehicleMovementComponent::HandleEngineHoveringVisuals(float InTurnRightInput, float DeltaTime)
{
	float RollAngle = AngleOfRoll * InTurnRightInput;

	FRotator TargetRelRot(0.0f, 0.0f, RollAngle);
	FQuat TargetQuat = TargetRelRot.Quaternion();
	FQuat CurrentQuat = OwningPodVehicle->EngineCenterPoint->GetRelativeRotation().Quaternion();
	FQuat InterpQuat = FMath::QInterpTo(CurrentQuat, TargetQuat, DeltaTime, 5.0f); // Smoother interp
	OwningPodVehicle->EngineCenterPoint->SetRelativeRotation(InterpQuat);

	AdjustVehiclePitch(DeltaTime);
}

// Improved pitch adjustment with multiple traces for cooler visuals
void UPodVehicleMovementComponent::AdjustVehiclePitch(float DeltaTime)
{
	if (!OwningPodVehicle->VehicleCenterRoot || !GetWorld()) return;

	const float TraceLength = 1000.0f;
	const float TraceOffset = 100.0f;
	FVector Forward = OwningPodVehicle->VehicleCenterRoot->GetForwardVector();

	FVector FrontLeftStart = OwningPodVehicle->LeftEngineRoot->GetComponentLocation() + FVector(0, 0, 100.0f);
	FVector FrontRightStart = OwningPodVehicle->RightEngineRoot->GetComponentLocation() + FVector(0, 0, 100.0f);
	FVector BackStart = OwningPodVehicle->VehicleCenterRoot->GetComponentLocation() - Forward * TraceOffset;

	FVector Down = -FVector::UpVector;
	FVector FrontLeftEnd = FrontLeftStart + Down * TraceLength;
	FVector FrontRightEnd = FrontRightStart + Down * TraceLength;
	FVector BackEnd = BackStart + Down * TraceLength;

	FHitResult FrontLeftHit, FrontRightHit, BackHit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(OwningPodVehicle);
	ECollisionChannel Channel = ECC_WorldStatic;

	bool bFLHit = GetWorld()->LineTraceSingleByChannel(FrontLeftHit, FrontLeftStart, FrontLeftEnd, Channel, Params);
	bool bFRHit = GetWorld()->LineTraceSingleByChannel(FrontRightHit, FrontRightStart, FrontRightEnd, Channel, Params);
	bool bBHit = GetWorld()->LineTraceSingleByChannel(BackHit, BackStart, BackEnd, Channel, Params);

	float TargetPitch = 0.0f;
	float MaxAirPitch = -45.0f;

	// TODO: These need to average to flat and matching the hit surface better
	if (bFLHit && bFRHit && bBHit)
	{
		FVector AvgFront = (FrontLeftHit.Location + FrontRightHit.Location) / 2.0f;
		FVector Slope = AvgFront - BackHit.Location;
		Slope.Normalize();
		TargetPitch = FMath::RadiansToDegrees(FMath::Asin(Slope.Z));
	}
	else if (!bFLHit && !bFRHit && !bBHit)
	{
		TargetPitch = MaxAirPitch;
		GEngine->AddOnScreenDebugMessage(-1, 0.5f, FColor::Red, TEXT("No Hit For Hover Trace."));
	}
	else
	{
		// Partial hits: blend based on hits
		//int HitCount = (bFLHit ? 1 : 0) + (bFRHit ? 1 : 0) + (bBHit ? 1 : 0);
		//TargetPitch = MaxAirPitch * (3 - HitCount) / 3.0f;
		int FrontHitCount = (bFLHit ? 1 : 0) + (bFRHit ? 1 : 0);
		FVector AvgFront = ((FrontLeftHit.Location * bFLHit) + (FrontRightHit.Location * bFRHit)) / FrontHitCount;
		FVector Slope = AvgFront - (BackHit.Location * bBHit);
		if (bFLHit == false && bFRHit == false)
		{
			Slope = (BackHit.Location * bBHit);
		} else if (bBHit == false)
		{
			Slope = AvgFront;
		}
		Slope.Normalize();
		TargetPitch = FMath::RadiansToDegrees(FMath::Asin(Slope.Z));
		GEngine->AddOnScreenDebugMessage(-1, 0.5f, FColor::Yellow, TEXT("Partial Hit For Hover Trace."));
	}

	TargetPitch = FMath::Clamp(TargetPitch, -45.0f, 45.0f);

	FRotator CurrentRot = OwningPodVehicle->VehicleCenterRoot->GetRelativeRotation();
	FRotator TargetRot(TargetPitch, CurrentRot.Yaw, CurrentRot.Roll);
	FRotator InterpRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaTime, 8.0f); // Faster interp for responsiveness
	OwningPodVehicle->VehicleCenterRoot->SetRelativeRotation(InterpRot);
}

// Server RPC implementation
void UPodVehicleMovementComponent::Server_ProcessMove_Implementation(FClientMoveData ClientMove)
{
	FRotator NewRotation = UpdatedComponent->GetComponentRotation();
	ApplyMovementLogic(ClientMove.MoveForwardInput, ClientMove.TurnRightInput, ClientMove.bIsBoosting, ClientMove.bIsBraking, ClientMove.bIsDrifting, ClientMove.DeltaTime, Velocity, NewRotation, CurrentAngularYawVelocity);
	Client_AcknowledgeMove(ClientMove.MoveID, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentRotation(), Velocity, CurrentAngularYawVelocity);
}

// Client acknowledgment with smoother correction
void UPodVehicleMovementComponent::Client_AcknowledgeMove_Implementation(uint32 LastProcessedMoveID, FVector ServerLocation, FRotator ServerRotation, FVector ServerVelocity, float ServerAngularYawVelocity)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	int32 Index = ClientMoveHistory.FindLastByPredicate([LastProcessedMoveID](const FClientMoveData& Move) { return Move.MoveID == LastProcessedMoveID; });
	if (Index == INDEX_NONE)
	{
		return;
	}

	ClientMoveHistory.RemoveAt(0, Index + 1);

	FVector ClientLoc = UpdatedComponent->GetComponentLocation();
	FRotator ClientRot = UpdatedComponent->GetComponentRotation();

	float LocDiff = FVector::DistSquared(ClientLoc, ServerLocation);
	if (LocDiff > FMath::Square(CorrectionThreshold) || !ClientRot.Equals(ServerRotation, 1.0f) || !Velocity.Equals(ServerVelocity, 10.0f) || !FMath::IsNearlyEqual(CurrentAngularYawVelocity, ServerAngularYawVelocity, 5.0f))
	{
		// Smoother correction: Interp to server state over a short time instead of hard set
		// But for now, keep hard set + replay, but increase threshold for minor diffs
		UpdatedComponent->SetWorldLocationAndRotation(ServerLocation, ServerRotation, false, nullptr, ETeleportType::ResetPhysics);
		Velocity = ServerVelocity;
		CurrentAngularYawVelocity = ServerAngularYawVelocity;

		// Replay history
		FVector ReplayVel = Velocity;
		FRotator ReplayRot = ServerRotation;
		float ReplayYawVel = ServerAngularYawVelocity;
		for (const FClientMoveData& Move : ClientMoveHistory)
		{
			ApplyMovementLogic(Move.MoveForwardInput, Move.TurnRightInput, Move.bIsBoosting, Move.bIsBraking, Move.bIsDrifting, Move.DeltaTime, ReplayVel, ReplayRot, ReplayYawVel);
		}
		Velocity = ReplayVel;
		CurrentAngularYawVelocity = ReplayYawVel;
	}
}

// Replication props
void UPodVehicleMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UPodVehicleMovementComponent, MoveForwardInput, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UPodVehicleMovementComponent, TurnRightInput, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UPodVehicleMovementComponent, bIsBoosting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UPodVehicleMovementComponent, bIsBraking, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UPodVehicleMovementComponent, bIsDrifting, COND_SkipOwner);

	DOREPLIFETIME_CONDITION(UPodVehicleMovementComponent, CurrentAngularYawVelocity, COND_SimulatedOnly);
}