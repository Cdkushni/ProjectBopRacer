// This file implements the core movement and networking logic for the PodVehicle.

#include "PodVehicleMovementComponent.h"

#include "PodVehicle.h"
#include "GameFramework/Pawn.h" // For Acknowledging position on server
#include "Net/UnrealNetwork.h" // Required for replication
#include "Components/CapsuleComponent.h" // For ground detection
#include "DrawDebugHelpers.h" // For visualizing ground trace

// Constructor: Set default values for movement parameters
UPodVehicleMovementComponent::UPodVehicleMovementComponent()
{
	// Set our component to tick every frame.
	PrimaryComponentTick.bCanEverTick = true;

	// Replication setup: This component's state will be replicated.
	// Crucial for multiplayer prediction and synchronization.
	SetIsReplicatedByDefault(true);

	// Initialize movement parameters
	MaxSpeed = 10000.0f; // Extremely high speed (100 m/s)
	Acceleration = 50000.0f; // 500 m/s^2
	Deceleration = 10000.0f; // 100 m/s^2 (general friction when no input)
	TurnSpeed = 200.0f; // Degrees per second of angular velocity target
	LinearDamping = 0.5f; // Reduce linear velocity over time
	AngularDamping = 10.0f; // Reduce angular velocity over time (tighter turning)

	// Boost parameters
	BoostStrength = 100000.0f; // Additional acceleration from boost
	BoostMaxSpeedMultiplier = 2.0f; // Max speed can be doubled when boosting

	// Brake parameters
	BrakeForce = 200000.0f; // High deceleration force

	// Drift parameters
	DriftTurnSpeedMultiplier = 2.0f; // Double turn speed for quicker rotations
	DriftLinearDampingMultiplier = 0.2f; // Reduce forward damping for more slide
	DriftAngularDampingMultiplier = 0.5f; // Reduce angular damping to maintain spin

	// Air control parameters
	AirControlTurnFactor = 0.5f; // Half the normal turn speed when in air
	AirControlPitchFactor = 0.5f; // Controls how much forward/backward input affects pitch in air

	CorrectionThreshold = 10.0f; // Correct if discrepancy is more than 10cm

	MoveForwardInput = 0.0f;
	TurnRightInput = 0.0f;
	bIsBoosting = false;
	bIsBraking = false;
	bIsDrifting = false;
	CurrentMoveID = 0; // Initialize move ID counter
	CurrentAngularYawVelocity = 0.0f; // Initialize angular velocity
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

	// --- Core Movement Logic Application ---
	// This logic runs differently based on network role:
	// - On server: Runs for all pawns (authoritative).
	// - On owning client: Runs for prediction.
	// - On simulated client: Relies on replicated data, skips this manual application.
	
	if (GetOwnerRole() == ROLE_Authority) // Server-side simulation for all pawns
	{
		// Use the replicated input values (received via RPC from client, or from local player input on listen server)
		// and apply the movement.
		FVector CurrentVel = Velocity;
		FRotator CurrentRot = UpdatedComponent->GetComponentRotation();
		FVector CurrentLoc = UpdatedComponent->GetComponentLocation();
		float CurrentYawVel = CurrentAngularYawVelocity; // Server's current authoritative angular velocity
		
		// Server uses its own DeltaTime for its authoritative simulation
		ApplyMovementLogic(MoveForwardInput, TurnRightInput, bIsBoosting, bIsBraking, bIsDrifting, DeltaTime, CurrentVel, CurrentRot, CurrentLoc, CurrentYawVel);

		Velocity = CurrentVel; // Update component's linear velocity after applying logic
		CurrentAngularYawVelocity = CurrentYawVel; // Update component's angular velocity
		// We use SafeMoveUpdatedComponent which automatically updates location/rotation.
		// For server, this is the authoritative step.
	}
	else if (OwnerPawn->IsLocallyControlled()) // Client-side prediction for owning player
	{
		// Increment MoveID for each client-side predicted move
		CurrentMoveID++;
		
		// Store the current input and move ID in history, along with the DeltaTime used for prediction
		FClientMoveData CurrentMove(MoveForwardInput, TurnRightInput, bIsBoosting, bIsBraking, bIsDrifting, CurrentMoveID, DeltaTime);
		ClientMoveHistory.Add(CurrentMove);

		// Apply prediction locally using the current input and DeltaTime
		FVector CurrentVel = Velocity;
		FRotator CurrentRot = UpdatedComponent->GetComponentRotation();
		FVector CurrentLoc = UpdatedComponent->GetComponentLocation();
		float CurrentYawVel = CurrentAngularYawVelocity; // Client's current predicted angular velocity

		ApplyMovementLogic(MoveForwardInput, TurnRightInput, bIsBoosting, bIsBraking, bIsDrifting, DeltaTime, CurrentVel, CurrentRot, CurrentLoc, CurrentYawVel);

		Velocity = CurrentVel; // Update component's velocity after applying prediction
		CurrentAngularYawVelocity = CurrentYawVel; // Update component's angular velocity
		// SafeMoveUpdatedComponent will move the local component here.

		// Send this input to the server via RPC
		Server_ProcessMove(CurrentMove);
	}
	// For ROLE_SimulatedProxy (other clients on a client machine), this block is skipped.
	// Their movement is purely driven by the replicated Velocity and Transform from the server,
	// with Unreal's built-in interpolation for smoothness via OnRep_ReplicatedMovement.
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
void UPodVehicleMovementComponent::ApplyMovementLogic(float InMoveForwardInput, float InTurnRightInput, bool InIsBoosting, bool InIsBraking, bool InIsDrifting, float InDeltaTime, FVector& OutVelocity, FRotator& OutRotation, FVector& OutLocation, float& OutAngularYawVelocity)
{
	bool bCurrentlyGrounded = IsGrounded();

	// --- Linear Movement (Forward/Backward Acceleration, Braking, Boosting) ---
	float CurrentAcceleration = InMoveForwardInput * Acceleration;
	
	// Apply Boost
	if (InIsBoosting)
	{
		CurrentAcceleration += BoostStrength;
	}

	// Apply Braking
	if (InIsBraking && OutVelocity.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		// Apply brake force opposite to current velocity direction
		FVector BrakeDirection = -OutVelocity.GetSafeNormal();
		OutVelocity += BrakeDirection * BrakeForce * InDeltaTime;

		// Prevent overshooting zero velocity
		if (FVector::DotProduct(OutVelocity, BrakeDirection) < 0) // If velocity has crossed zero due to braking
		{
			OutVelocity = FVector::ZeroVector;
		}
	}
	else // Apply normal acceleration/deceleration if not braking
	{
		OutVelocity += OutRotation.Vector() * CurrentAcceleration * InDeltaTime;
	}

	// --- Linear Damping (Friction/Air Resistance) ---
	// Apply damping, adjusted for drifting and air control
	float CurrentLinearDamping = LinearDamping;
	if (InIsDrifting && bCurrentlyGrounded)
	{
		CurrentLinearDamping *= DriftLinearDampingMultiplier;
	}
	else if (!bCurrentlyGrounded)
	{
		CurrentLinearDamping = 0.0f; // No linear damping in air, relies on gravity
	}
	OutVelocity *= FMath::Clamp(1.0f - CurrentLinearDamping * InDeltaTime, 0.0f, 1.0f);


	// --- Max Speed Limit ---
	float CurrentMaxSpeed = MaxSpeed;
	if (InIsBoosting)
	{
		CurrentMaxSpeed *= BoostMaxSpeedMultiplier;
	}
	if (OutVelocity.SizeSquared() > FMath::Square(CurrentMaxSpeed))
	{
		OutVelocity = OutVelocity.GetSafeNormal() * CurrentMaxSpeed;
	}
	
	// --- Angular Movement (Steering, Drifting, Air Control) ---
	float CurrentTurnSpeed = TurnSpeed;
	float CurrentAngularDamping = AngularDamping;

	if (bCurrentlyGrounded)
	{
		if (InIsDrifting)
		{
			CurrentTurnSpeed *= DriftTurnSpeedMultiplier;
			CurrentAngularDamping *= DriftAngularDampingMultiplier;
		}
	}
	else // Airborne
	{
		CurrentTurnSpeed *= AirControlTurnFactor;
		CurrentAngularDamping = 0.0f; // No angular damping in air, or very little
		
		// Apply pitch control based on forward/backward input
		float PitchChange = InMoveForwardInput * AirControlPitchFactor * InDeltaTime;
		OutRotation.Pitch += PitchChange;
		// Clamp pitch if desired to prevent vehicle from flipping upside down too easily in air
		// OutRotation.Pitch = FMath::Clamp(OutRotation.Pitch, -60.0f, 60.0f);
	}

	/////////
	// Calculate desired yaw change
	//float DeltaYaw = InTurnRightInput * CurrentTurnSpeed * InDeltaTime;
	// Apply angular damping to current rotation. This makes turning smoother.
	// You can model this more accurately with angular velocity, but direct damping on rotation works for arcade.
	// OutRotation = FMath::Lerp(OutRotation.Quaternion(), (OutRotation + FRotator(0, DeltaYaw, 0)).Quaternion(), FMath::Clamp(1.0f - CurrentAngularDamping * InDeltaTime, 0.0f, 1.0f)).Rotator();
	// For simpler arcade feel, let's directly apply yaw and then normalize.
	//OutRotation.Yaw += DeltaYaw;
	//OutRotation.Normalize();
	////////
	// Calculate desired angular acceleration for yaw
	float DesiredAngularAccelerationYaw = InTurnRightInput * CurrentTurnSpeed;
	
	// Apply angular acceleration to OutAngularYawVelocity
	OutAngularYawVelocity += DesiredAngularAccelerationYaw * InDeltaTime;

	// Apply angular damping to OutAngularYawVelocity
	OutAngularYawVelocity *= FMath::Clamp(1.0f - CurrentAngularDamping * InDeltaTime, 0.0f, 1.0f);

	// Apply OutAngularYawVelocity to rotation
	OutRotation.Yaw += OutAngularYawVelocity * InDeltaTime;
	OutRotation.Normalize(); // Keep rotation within -180 to 180 degrees

	// --- Apply Gravity when airborne ---
	if (!bCurrentlyGrounded)
	{
		// Apply a simple downward force to simulate gravity.
		// This should match the Project Settings -> Physics -> Gravity.
		OutVelocity += FVector(0, 0, GetWorld()->GetGravityZ()) * InDeltaTime;
	}

	// --- Final Movement Application ---
	FVector DeltaMove = OutVelocity * InDeltaTime;

	FHitResult Hit;
	SafeMoveUpdatedComponent(DeltaMove, OutRotation, true, Hit); // This will update UpdatedComponent's location/rotation

	if (Hit.bBlockingHit)
	{
		if (Hit.bStartPenetrating)
		{
			StopMovementImmediately();
		}
	}
	OutLocation = UpdatedComponent->GetComponentLocation();
}

// Implementation of the Server RPC for processing client moves.
void UPodVehicleMovementComponent::Server_ProcessMove_Implementation(FClientMoveData ClientMove)
{
	// This function executes on the server.
	// Apply the client's input using our shared movement logic.
	// We're using the server's authoritative CurrentVelocity, CurrentRotation, CurrentLocation.
	FVector ServerCurrentVelocity = Velocity;
	FRotator ServerCurrentRotation = UpdatedComponent->GetComponentRotation();
	FVector ServerCurrentLocation = UpdatedComponent->GetComponentLocation();
	float ServerCurrentAngularYawVelocity = CurrentAngularYawVelocity; // Server's current authoritative angular velocity

	// Apply movement logic using the client's input and its reported DeltaTime
	ApplyMovementLogic(ClientMove.MoveForwardInput, ClientMove.TurnRightInput, ClientMove.bIsBoosting, ClientMove.bIsBraking, ClientMove.bIsDrifting, ClientMove.DeltaTime,
		ServerCurrentVelocity, ServerCurrentRotation, ServerCurrentLocation, ServerCurrentAngularYawVelocity);

	Velocity = ServerCurrentVelocity;
	CurrentAngularYawVelocity = ServerCurrentAngularYawVelocity; // Update server's authoritative angular velocity

	// Now, acknowledge this move back to the client.
	Client_AcknowledgeMove(ClientMove.MoveID, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentRotation(), Velocity);
}

// Implementation of the Client RPC for server acknowledgment.
void UPodVehicleMovementComponent::Client_AcknowledgeMove_Implementation(uint32 LastProcessedMoveID, FVector ServerLocation, FRotator ServerRotation, FVector ServerVelocity)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return; // Only the owning client should process this acknowledgment.
	}

	// 1. Find the acknowledged move in the client's history.
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
		// This can happen if client history is too short or packet reordering/loss.
		// For simplicity, we'll just log. A robust system might re-request or tolerate.
		UE_LOG(LogTemp, Warning, TEXT("Client: Acknowledged move ID %d not found in history!"), LastProcessedMoveID);
		// Consider clearing the history if this happens consistently or is critical.
		// ClientMoveHistory.Empty();
		// CurrentMoveID = LastProcessedMoveID; // Resetting might be needed in severe cases
		return;
	}

	// 2. Remove all moves from history up to and including the acknowledged move.
	ClientMoveHistory.RemoveAt(0, Index + 1);

	// 3. Compare current client position with server's authoritative position.
	FVector ClientLocation = UpdatedComponent->GetComponentLocation();
	FRotator ClientRotation = UpdatedComponent->GetComponentRotation();
	FVector ClientVelocity = Velocity;

	float LocationDifference = FVector::DistSquared(ClientLocation, ServerLocation);
	
	// Correction condition: significant position diff, or noticeable rotation/velocity diff.
	if (LocationDifference > FMath::Square(CorrectionThreshold) || 
		!ClientRotation.Equals(ServerRotation, 0.5f) || // Increased tolerance for rotation
		!ClientVelocity.Equals(ServerVelocity, 5.0f)) // Increased tolerance for velocity
	{
		// Correction needed!
		UE_LOG(LogTemp, Warning, TEXT("Client: Correcting position/rotation/velocity. LocDiff: %f, RotDiff: %f, VelDiff: %f"), 
			FMath::Sqrt(LocationDifference),
			(ClientRotation - ServerRotation).Yaw, // Log yaw diff for debugging
			(ClientVelocity - ServerVelocity).Size());

		// Set the client's pawn to the server's authoritative state.
		// Use TeleportPhysics for a hard correction that handles collision.
		UpdatedComponent->SetWorldLocationAndRotation(ServerLocation, ServerRotation, false, nullptr, ETeleportType::TeleportPhysics);
		Velocity = ServerVelocity;

		// 4. Replay all subsequent moves in history.
		// Start replay from the newly corrected state.
		FVector CurrentVelToReplay = Velocity;
		FRotator CurrentRotToReplay = UpdatedComponent->GetComponentRotation();
		FVector CurrentLocToReplay = UpdatedComponent->GetComponentLocation();
		float CurrentYawVelToReplay = CurrentAngularYawVelocity; // Start replay from corrected angular velocity

		for (const FClientMoveData& Move : ClientMoveHistory)
		{
			// Apply each historical input on top of the corrected state, using its original DeltaTime.
			ApplyMovementLogic(Move.MoveForwardInput, Move.TurnRightInput, Move.bIsBoosting, Move.bIsBraking, Move.bIsDrifting, Move.DeltaTime,
				CurrentVelToReplay, CurrentRotToReplay, CurrentLocToReplay, CurrentYawVelToReplay);

			// After replay, update the component's velocity to match the replayed result.
			Velocity = CurrentVelToReplay;
			// Location/Rotation already updated by SafeMoveUpdatedComponent in ApplyMovementLogic.
		}
		CurrentAngularYawVelocity = CurrentYawVelToReplay; // Apply replayed angular velocity
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
	FVector End = Start - FVector(0, 0, HalfHeight + 5.0f); // 5cm tolerance for being "on ground"

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner()); // Ignore self for trace

	FHitResult Hit;
	bool bHit = GetWorld()->SweepSingleByChannel(
		Hit,
		Start,
		End,
		FQuat::Identity,
		ECC_Visibility, // You might want to use a custom Trace Channel for ground detection, e.g., ECC_WorldStatic
		FCollisionShape::MakeSphere(Radius * 0.9f), // Slightly smaller sphere than capsule radius
		Params
	);

	// Optional: Draw debug sphere trace for visualization
	// DrawDebugSphere(GetWorld(), Hit.TraceEnd, Radius * 0.9f, 16, bHit ? FColor::Green : FColor::Red, false, 0.1f, 0, 1.0f);
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