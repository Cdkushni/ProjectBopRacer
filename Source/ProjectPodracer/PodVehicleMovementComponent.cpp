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

	DriftTurnSpeedMultiplier = 1.5f; // 50% more turn speed while drifting
	DriftLinearDampingMultiplier = 0.2f; // Much less forward damping for more slide
	DriftAngularDampingMultiplier = 0.3f; // Less angular damping, sustains spin more
	DriftLateralSlideFactor = 0.8f; // Retain 80% of lateral velocity, less sideways friction

	AirControlTurnFactor = 0.4f; // Weaker turn in air
	AirControlPitchFactor = 0.6f; // Good pitch responsiveness in air
	AirControlRollFactor = 0.7f; // Decent roll control in air

	GroundTraceDistance = 50.0f; // How far below to check for ground
	GroundDetectionRadius = 60.0f; // Radius for ground trace (should be slightly less than capsule radius)
	GroundCollisionChannel = ECC_Visibility; // Default, consider a custom Trace Channel (e.g., "Ground")

	DragCoefficient = 10.0f; // Interpolation speed for air resistance

	CorrectionThreshold = 10.0f; // Correct if discrepancy is more than 10cm
	GravityScale = 980.0f; // Approx. 1G in cm/s^2 (Unreal's default gravity is -980 for Z axis)

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

// This function is called every frame to update the component's state.
// It contains the core movement logic and network prediction/correction.
void UPodVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Ensure we have a valid UpdatedComponent (the component we are supposed to move)
	// and that we are not paused.
	if (!IsValid(UpdatedComponent) || ShouldSkipUpdate(DeltaTime))
	{
		return;
	}

	// Get the owning Pawn.
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return;
	}

	// === Input Smoothing (Client Only) ===
	// Smooth the rudder input for keyboard friendliness on the client side.
	// This smoothed input is then used for local prediction and sent via RPC.
	if (OwnerPawn->IsLocallyControlled())
	{
		float InterpSpeed = (FMath::IsNearlyZero(TurnRightInput)) ? KeyboardSteeringReturnSpeed : KeyboardSteeringInterpSpeed;
		SmoothedRudderInput = FMath::FInterpTo(SmoothedRudderInput, TurnRightInput, DeltaTime, InterpSpeed);
	}
	// On the server, and for simulated proxies, SmoothedRudderInput is not directly used for movement logic;
	// they use the replicated TurnRightInput (which was the smoothed input from the client).

	// --- Core Movement Logic Application ---
	// This logic runs differently based on network role:
	// - On server: Runs for all pawns (authoritative).
	// - On owning client: Runs for prediction.
	// - On simulated client: Relies on replicated data, skips this manual application.
	
	if (GetOwnerRole() == ROLE_Authority) // Server-side authoritative simulation for all pawns
	{
		// Use the replicated input values (received via RPC from client, or from local player input on listen server)
		// and apply the movement.
		// On the server, we use the replicated input values (updated by RPCs from clients)
		// and apply the movement.
		FRotator NewRotation = UpdatedComponent->GetComponentRotation();
		ApplyMovementLogic(MoveForwardInput, TurnRightInput, bIsBoosting, bIsBraking, bIsDrifting, DeltaTime, Velocity, NewRotation, CurrentAngularYawVelocity);

		// After server processes the move, acknowledge back to the client.
		Client_AcknowledgeMove(CurrentMoveID, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentRotation(), Velocity, CurrentAngularYawVelocity);
	}
	else if (OwnerPawn->IsLocallyControlled()) // Client-side prediction for owning player
	{
		// Increment MoveID for each client-side predicted move
		CurrentMoveID++;
		
		// Store the current input (including smoothed rudder input and new flags) in history,
		// along with the DeltaTime used for prediction.
		FClientMoveData CurrentMove(MoveForwardInput, SmoothedRudderInput, bIsBoosting, bIsBraking, bIsDrifting, CurrentMoveID, DeltaTime);
		ClientMoveHistory.Add(CurrentMove);

		// Apply prediction locally using the current input and DeltaTime
		FRotator NewRotation = UpdatedComponent->GetComponentRotation();
		ApplyMovementLogic(MoveForwardInput, SmoothedRudderInput, bIsBoosting, bIsBraking, bIsDrifting, DeltaTime, Velocity, NewRotation, CurrentAngularYawVelocity);

		// Send this input to the server via RPC
		Server_ProcessMove(CurrentMove);
	}
	// For ROLE_SimulatedProxy (other clients on a client machine), this block is skipped.
	// Their movement is purely driven by the replicated Velocity, CurrentAngularYawVelocity, and Transform from the server,
	// with Unreal's built-in interpolation for smoothness via OnRep_ReplicatedMovement.

	// TODO: For some reason its only working on local vehicle, not on remote vehicles
	HandleEngineHoveringVisuals(SmoothedRudderInput, DeltaTime);
}

// Setter for MoveForwardInput. This is called by the owning APodVehicle.
void UPodVehicleMovementComponent::SetMoveForwardInput(float Value)
{
	// Clamp the value to ensure it's within expected range (-1.0 to 1.0).
	float ClampedValue = FMath::Clamp(Value, -1.0f, 1.0f);
	if (MoveForwardInput != ClampedValue)
	{
		// Update the input locally.
		MoveForwardInput = ClampedValue;
		//GEngine->AddOnScreenDebugMessage(-1, 10, FColor::Green, *FString::Printf(TEXT("Client Call Forward Input Sending To Server : %f from actor: %s"), MoveForwardInput, *GetOwner()->GetName()), true);
	}
	// Note: We no longer send RPC immediately here. It's handled in TickComponent
	// where moves are batched and sent with a MoveID.
}

// Setter for TurnRightInput. This is called by the owning APodVehicle.
void UPodVehicleMovementComponent::SetTurnRightInput(float Value)
{
	// Clamp the value to ensure it's within expected range (-1.0 to 1.0).
	float ClampedValue = FMath::Clamp(Value, -1.0f, 1.0f);
	if (TurnRightInput != ClampedValue)
	{
		// Update the input locally.
		TurnRightInput = ClampedValue;
	}
	// Note: We no longer send RPC immediately here. It's handled in TickComponent.
}

// Setter for bIsBoosting.
void UPodVehicleMovementComponent::SetBoostInput(bool bNewState)
{
	if (bIsBoosting != bNewState)
	{
		bIsBoosting = bNewState;
	}
}

// Setter for bIsBraking.
void UPodVehicleMovementComponent::SetBrakeInput(bool bNewState)
{
	if (bIsBraking != bNewState)
	{
		bIsBraking = bNewState;
	}
}

// Setter for bIsDrifting.
void UPodVehicleMovementComponent::SetDriftInput(bool bNewState)
{
	if (bIsDrifting != bNewState)
	{
		bIsDrifting = bNewState;
	}
}

// Helper function to apply movement logic for a given input.
void UPodVehicleMovementComponent::ApplyMovementLogic(float InMoveForwardInput, float InTurnRightInput, bool InIsBoosting, bool InIsBraking, bool InIsDrifting, float InDeltaTime, FVector& OutVelocity, FRotator& OutRotation, float& OutAngularYawVelocity)
{
	float DeltaTime = InDeltaTime;
	if (DeltaTime <= 0.0f) return;

	FVector SimpleGroundNormal = FVector::UpVector;
	float Height = 100.f;

	FVector Start = OwningPodVehicle->GetRootComponent()->GetComponentLocation();
	FVector End = Start - OwningPodVehicle->GetActorUpVector() * Height;
	FHitResult HoverHitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwningPodVehicle);

	if (GetWorld()->LineTraceSingleByChannel(HoverHitResult, Start, End, GroundCollisionChannel, QueryParams))
	{
		Height = HoverHitResult.Distance;
		SimpleGroundNormal = HoverHitResult.Normal.GetSafeNormal();
	}

	FVector ForwardVector = OwningPodVehicle->GetRootComponent()->GetForwardVector();
	float ControlMultiplier = IsGrounded() ? 1.0f : AirControlTurnFactor;
	float EffectiveMaxSpeed = MaxSpeed * (InIsBoosting ? BoostMaxSpeedMultiplier : 1.0f);

	// Apply rotation (yaw) from RudderInput
	float EffectiveTurnRate = TurnSpeed * HighSpeedSteeringDampFactor * (InIsDrifting ? DriftTurnSpeedMultiplier : 1.0f);
	float YawDelta = InTurnRightInput * EffectiveTurnRate * DeltaTime;
	FRotator CurrentRotation = OwningPodVehicle->GetRootComponent()->GetComponentRotation();
	//FRotator NewRotation = FRotator(CurrentRotation.Pitch, CurrentRotation.Yaw + YawDelta, CurrentRotation.Roll);
	// TODO: Set rotation like this?
	//OwningPodVehicle->GetRootComponent()->SetWorldRotation(NewRotation);
	float CurrentTurnRate = MaxTurnRate;
	float CurrentAngularAcceleration = TurnAcceleration;
	float CurrentAngularDamping = AngularDamping;

	// Update Velocity

	// Apply air resistance (slowdown in non-forward directions)
	// Preserve Velocity in forward direction
	FVector ForwardVelocity = FVector::DotProduct(CurrentVelocity, ForwardVector) * ForwardVector;
	// Isolate perpendicular velocity (to be slowed down)
	FVector PerpendicularVelocity = CurrentVelocity - ForwardVelocity;
	// Interpolate perpendicular velocity toward zero
	if (IsGrounded())
	{
		PerpendicularVelocity = FVector::VectorPlaneProject(PerpendicularVelocity, SimpleGroundNormal);
		PerpendicularVelocity = FMath::VInterpTo(PerpendicularVelocity, FVector::ZeroVector, DeltaTime, DragCoefficient * ControlMultiplier);
	} else
	{
		PerpendicularVelocity = FMath::VInterpTo(PerpendicularVelocity, FVector::ZeroVector, DeltaTime, DragCoefficient * ControlMultiplier * 0.25f);
	}
	if (FMath::Abs(InMoveForwardInput) < 0.1)
	{
		ForwardVelocity = FMath::VInterpTo(ForwardVelocity, FVector::ZeroVector, DeltaTime, 1.f);
	}
	// Recombine velocities
	CurrentVelocity = ForwardVelocity + PerpendicularVelocity;

	if (InIsBraking)
	{
		// Decelerate
		float Speed = CurrentVelocity.Size2D();
		if (Speed > 0.0f)
		{
			FVector VelocityDir = CurrentVelocity.GetSafeNormal2D();
			float NewSpeed = FMath::Max(0.0f, Speed - BrakeDeceleration * DeltaTime);
			CurrentVelocity = VelocityDir * NewSpeed;
			if (!IsGrounded())
			{
				CurrentVelocity.Z = FMath::Max(CurrentVelocity.Z - BrakeDeceleration * DeltaTime, -2000.0f);
			}
		}
	}
	else
	{
		// Accelerate
		FVector AccelerationVector = ForwardVector * InMoveForwardInput * Acceleration * ControlMultiplier;
		UE_LOG(LogTemp, Log, TEXT("Acceleration Value: %f"), AccelerationVector.Length());
		CurrentVelocity += AccelerationVector * DeltaTime;
	}

	// Clamp velocity
	if (IsGrounded())
	{
		CurrentVelocity = FVector::VectorPlaneProject(CurrentVelocity, SimpleGroundNormal);
		// Speed-based agility for steering
		float SpeedMultiplier = FMath::GetMappedRangeValueClamped(
			FVector2D(0.0f, MaxSpeed),
			FVector2D(1.0f, HighSpeedSteeringDampFactor),
			OutVelocity.Size() // Use magnitude of velocity for general speed dampening
		);
		
		CurrentTurnRate *= SpeedMultiplier;
		
		if (InIsDrifting)
		{
			CurrentTurnRate *= DriftTurnSpeedMultiplier;
			CurrentAngularDamping *= DriftAngularDampingMultiplier;
		}

		// Calculate target angular velocity based on input and adjusted turn rate
		float TargetAngularVelocityYaw = InTurnRightInput * CurrentTurnRate;
		
		// Apply angular acceleration towards target
		float AngularVelocityChange = (TargetAngularVelocityYaw - OutAngularYawVelocity);
		OutAngularYawVelocity += FMath::Sign(AngularVelocityChange) * FMath::Min(FMath::Abs(AngularVelocityChange), CurrentAngularAcceleration * InDeltaTime);
		
		// Apply angular damping
		OutAngularYawVelocity *= FMath::Clamp(1.0f - CurrentAngularDamping * InDeltaTime, 0.0f, 1.0f);
	}
	else // Airborne Control
	{
		// Apply air control (pitch/roll/yaw) directly to rotation for arcade feel
		// Yaw control (turning in air)
		OutRotation.Yaw += InTurnRightInput * AirControlTurnFactor * MaxTurnRate * InDeltaTime;

		// Pitch control (forward/backward input tilts pod)
		//OutRotation.Pitch += InMoveForwardInput * AirControlPitchFactor * MaxTurnRate * InDeltaTime;

		// Roll control (turning input applies some roll)
		//OutRotation.Roll += InTurnRightInput * AirControlRollFactor * MaxTurnRate * InDeltaTime;

		OutAngularYawVelocity = 0.0f; // No angular velocity accumulation for direct air control
	}
	// Apply angular velocity to rotation (for grounded movement)
	OutRotation.Yaw += OutAngularYawVelocity * InDeltaTime;
	OutRotation.Normalize(); // Keep rotation within -180 to 180 degrees

	float Speed = CurrentVelocity.Size2D();
	if (Speed > EffectiveMaxSpeed)
	{
		FVector VelocityDir = CurrentVelocity.GetSafeNormal2D();
		CurrentVelocity = VelocityDir * EffectiveMaxSpeed;
		if (!IsGrounded())
		{
			CurrentVelocity.Z = FMath::Clamp(CurrentVelocity.Z, -GravityScale, 0.0f);
		}
	}
	if (!IsGrounded())
	{
		CurrentVelocity.Z += -GravityScale * InDeltaTime;
	}

	// Move with SafeMoveUpdatedComponent
	FVector Delta = CurrentVelocity * DeltaTime;
	FHitResult HitResult;
	SafeMoveUpdatedComponent(Delta, OutRotation, true, HitResult);
	if (HitResult.bBlockingHit)
	{
		// Slide along surface
		SlideAlongSurface(Delta, 1.0f - HitResult.Time, HitResult.Normal, HitResult);
		CurrentVelocity = FVector::VectorPlaneProject(CurrentVelocity, HitResult.Normal);
	}

	if (true)
	{
		UE_LOG(LogTemp, Log, TEXT("SimulateMove: Thruster=%.3f, Rudder=%.3f, Braking=%d, Drifting=%d, Boosting=%d, Vel=%s, Rot=%s, Role=%d"),
			InMoveForwardInput, InTurnRightInput, InIsBraking, InIsDrifting, InIsBoosting,
			*CurrentVelocity.ToString(), *OutRotation.ToString(), (int32)GetOwner()->GetLocalRole());
	}
}

void UPodVehicleMovementComponent::HandleEngineHoveringVisuals(float InTurnRightInput, float DeltaTime)
{
	GroundNormal = FVector::UpVector;
    float Height = GroundDetectionRadius;

    FVector Start = OwningPodVehicle->GetRootComponent()->GetComponentLocation();
    FVector End = Start - OwningPodVehicle->GetActorUpVector() * GroundTraceDistance;
    FHitResult HitResult;
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(GetOwner());

    if (GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, GroundCollisionChannel, QueryParams))
    {
        Height = HitResult.Distance;
        GroundNormal = HitResult.Normal.GetSafeNormal();
    }
	// TODO: Do traces for the front, back and center points of the pod to get average normals for pitch rotation

    float RollAngle = AngleOfRoll * -InTurnRightInput;
    //FQuat BodyRotation = GetActorRotation().Quaternion() * FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(RollAngle));
    FRotator LookAtRotation = UKismetMathLibrary::FindLookAtRotation(OwningPodVehicle->PodHullMesh->GetComponentLocation(), OwningPodVehicle->EngineCenterPoint->GetComponentLocation());
    FQuat CurrentBodyRotation = OwningPodVehicle->PodHullMesh->GetComponentQuat();
    FQuat EngineRotation = OwningPodVehicle->GetActorRotation().Quaternion() * FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(RollAngle));
    FQuat CurrentEngineRotation = OwningPodVehicle->EngineCenterPoint->GetComponentQuat();
    //HullMesh->SetWorldRotation(FMath::QInterpTo(CurrentBodyRotation, BodyRotation, DeltaTime, 5.0f));
    OwningPodVehicle->PodHullMesh->SetWorldRotation(FMath::QInterpTo(CurrentBodyRotation, LookAtRotation.Quaternion(), DeltaTime, 5.0f));
    OwningPodVehicle->EngineCenterPoint->SetWorldRotation(FMath::QInterpTo(CurrentEngineRotation, EngineRotation, DeltaTime, 5.0f));
}

// Implementation of the Server RPC for processing client moves.
void UPodVehicleMovementComponent::Server_ProcessMove_Implementation(FClientMoveData ClientMove)
{
	// This function executes on the server.
	// Apply the client's input using our shared movement logic.
	FRotator NewRotation = UpdatedComponent->GetComponentRotation();
	ApplyMovementLogic(ClientMove.MoveForwardInput, ClientMove.TurnRightInput, ClientMove.bIsBoosting, ClientMove.bIsBraking, ClientMove.bIsDrifting, ClientMove.DeltaTime,
		Velocity, NewRotation, CurrentAngularYawVelocity);

	// After server processes the move, acknowledge back to the client.
	Client_AcknowledgeMove(ClientMove.MoveID, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentRotation(), Velocity, CurrentAngularYawVelocity);
}

// Implementation of the Client RPC for server acknowledgment.
void UPodVehicleMovementComponent::Client_AcknowledgeMove_Implementation(uint32 LastProcessedMoveID, FVector ServerLocation, FRotator ServerRotation, FVector ServerVelocity, float ServerAngularYawVelocity)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	int32 Index = INDEX_NONE;
	for (int32 i = 0; i < ClientMoveHistory.Num(); ++i)
	{
		if (ClientMoveHistory[i].MoveID == LastProcessedMoveID)
		{
			Index = i;
			break;
		}
	}

	if (Index == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("Client: Acknowledged move ID %d not found in history! History Size: %d"), LastProcessedMoveID, ClientMoveHistory.Num());
		// If a crucial move is missing, it might be necessary to clear history and force a hard sync.
		// For now, logging will suffice.
		return;
	}

	ClientMoveHistory.RemoveAt(0, Index + 1);

	FVector ClientLocation = UpdatedComponent->GetComponentLocation();
	FRotator ClientRotation = UpdatedComponent->GetComponentRotation();
	FVector ClientLinearVelocity = Velocity;
	float ClientAngularYawVelocityLocal = CurrentAngularYawVelocity; // Use a local copy for comparison

	float LocationDifference = FVector::DistSquared(ClientLocation, ServerLocation);

	// Correction condition: significant position diff, or noticeable rotation/velocity diff.
	if (LocationDifference > FMath::Square(CorrectionThreshold) || 
		!ClientRotation.Equals(ServerRotation, 1.0f) || // 1 degree tolerance
		!ClientLinearVelocity.Equals(ServerVelocity, 10.0f) || // 10 cm/s tolerance
		!FMath::IsNearlyEqual(ClientAngularYawVelocityLocal, ServerAngularYawVelocity, 5.0f)) // 5 deg/s tolerance
	{
		UE_LOG(LogTemp, Warning, TEXT("Client: Correcting pos/rot/vel/angVel. LocDiff: %f, RotDiff: %f, VelDiff: %f, AngYawDiff: %f"), 
			FMath::Sqrt(LocationDifference),
			(ClientRotation - ServerRotation).Yaw,
			(ClientLinearVelocity - ServerVelocity).Size(),
			ClientAngularYawVelocityLocal - ServerAngularYawVelocity);

		// Set the client's pawn to the server's authoritative state.
		// Use TeleportPhysics for a hard correction that handles collision.
		UpdatedComponent->SetWorldLocationAndRotation(ServerLocation, ServerRotation, false, nullptr, ETeleportType::TeleportPhysics);
		Velocity = ServerVelocity;
		CurrentAngularYawVelocity = ServerAngularYawVelocity; // Correct angular velocity

		// Replay all subsequent moves in history.
		FVector CurrentVelToReplay = Velocity;
		FRotator CurrentRotToReplay = UpdatedComponent->GetComponentRotation();
		float CurrentYawVelToReplay = CurrentAngularYawVelocity;

		for (const FClientMoveData& Move : ClientMoveHistory)
		{
			ApplyMovementLogic(Move.MoveForwardInput, Move.TurnRightInput, Move.bIsBoosting, Move.bIsBraking, Move.bIsDrifting, Move.DeltaTime,
				CurrentVelToReplay, CurrentRotToReplay, CurrentYawVelToReplay);
			
			Velocity = CurrentVelToReplay; // Update linear velocity after each replayed step
			// UpdatedComponent's transform is updated by SafeMoveUpdatedComponent within ApplyMovementLogic
		}
		CurrentAngularYawVelocity = CurrentYawVelToReplay; // Apply final replayed angular velocity
	}
}

// Simple ground detection using a sphere trace.
bool UPodVehicleMovementComponent::IsGrounded() const
{
	if (!UpdatedComponent) return false;

	FVector Start = UpdatedComponent->GetComponentLocation();
	// Adjust trace end to be slightly below the capsule base
	UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 50.0f;
	float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 50.0f;

	// Trace a small distance below the capsule
	FVector End = Start - FVector(0, 0, HalfHeight);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner()); // Ignore self for trace
	Params.bTraceComplex = false; // Simple trace for performance

	// Use the capsule's radius for the sweep shape for better accuracy
	FCollisionShape CapsuleShape = FCollisionShape::MakeSphere(Radius); // Sphere trace for simple ground check

	FHitResult Hit;
	bool bHit = GetWorld()->SweepSingleByChannel(
		Hit,
		Start,
		End,
		FQuat::Identity,
		GroundCollisionChannel,
		CapsuleShape,
		Params
	);

	// Optional: Draw debug sphere trace for visualization
	// DrawDebugSphere(GetWorld(), Hit.TraceEnd, Radius, 16, bHit ? FColor::Green : FColor::Red, false, 0.1f, 0, 1.0f);
	// DrawDebugLine(GetWorld(), Start, End, bHit ? FColor::Green : FColor::Red, false, 0.1f, 0, 1.0f);

	return bHit;
}

// This function defines which properties of this movement component are replicated
// from the server to clients.
void UPodVehicleMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate the current input values from client to server (via RPC, then server to other clients via Replicated property).
	// COND_SkipOwner means the owner of the pawn doesn't need to receive its own input back
	// from the server, as it already knows what input it sent. This saves bandwidth.
	// Replicate the current input values (including new boost/brake/drift states) to all clients (for simulated proxies).
	DOREPLIFETIME_CONDITION(UPodVehicleMovementComponent, MoveForwardInput, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UPodVehicleMovementComponent, TurnRightInput, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UPodVehicleMovementComponent, bIsBoosting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UPodVehicleMovementComponent, bIsBraking, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UPodVehicleMovementComponent, bIsDrifting, COND_SkipOwner);

	// Replicate angular yaw velocity for smoother simulation of remote proxies.
	DOREPLIFETIME_CONDITION(UPodVehicleMovementComponent, CurrentAngularYawVelocity, COND_SimulatedOnly);

	// Note: The 'Velocity' member is handled by the base UMovementComponent and its
	// replication (via DOREPLIFETIME for Velocity and OnRep_Velocity), which is crucial
	// for simulated proxies to smoothly follow the server's state.
	// We do not need to explicitly add Velocity here unless we're overriding its behavior.
}