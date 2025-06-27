// Fill out your copyright notice in the Description page of Project Settings.
#include "PodracerMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h" // For DOREPLIFETIME
#include "Kismet/KismetSystemLibrary.h" // For LineTrace

UPodracerMovementComponent::UPodracerMovementComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    SetIsReplicatedByDefault(true); // Important for component replication

    MaxFlySpeed = 8000.0f;
    Acceleration = 3000.0f;
    TurnSpeed = 90.0f; // Degrees per second
    TargetHoverHeight = 150.0f;
    HoverStiffness = 10.0f;
    HoverDamping = 5.0f;
    MinGroundDistanceForFullHoverEffect = 50.f;

    CurrentThrottleInput = 0.f;
    CurrentSteeringInput = 0.f;
    CurrentVelocity = FVector::ZeroVector;
}

void UPodracerMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UPodracerMovementComponent, CurrentVelocity); // Example of replicating velocity
}

void UPodracerMovementComponent::SetThrottleInput(float InThrottle)
{
    CurrentThrottleInput = FMath::Clamp(InThrottle, -1.0f, 1.0f);
}

void UPodracerMovementComponent::SetSteeringInput(float InSteering)
{
    CurrentSteeringInput = FMath::Clamp(InSteering, -1.0f, 1.0f);
}

bool UPodracerMovementComponent::Server_SendMove_Validate(const FPodracerMove& Move) { return true; /* Add validation here */ }
void UPodracerMovementComponent::Server_SendMove_Implementation(const FPodracerMove& Move)
{
    // Server receives move from client, processes it, and updates its state.
    // This is where server authoritative logic runs.
    CurrentThrottleInput = Move.ForwardInput;
    CurrentSteeringInput = Move.TurnInput;
    
    // For more robust systems, the server would re-simulate this move and correct the client if necessary.
    // Here, we just apply the inputs for the next server tick.
    // A full implementation would involve replaying moves if timestamps differ significantly,
    // using FSavedMove_Character as a reference.

    // Since we are directly setting inputs, the server's TickComponent will use these.
}


void UPodracerMovementComponent::ApplyControlInputToVelocity(float DeltaTime)
{
    if (!PawnOwner) return;

    // Acceleration
    FVector ForwardDir = PawnOwner->GetActorForwardVector();
    CurrentVelocity += ForwardDir * CurrentThrottleInput * Acceleration * DeltaTime;

    // Drag/Friction (simple)
    CurrentVelocity *= FMath::Pow(0.95f, DeltaTime * 10.f); // Dampen velocity over time

    // Clamp speed
    if (CurrentVelocity.SizeSquared() > FMath::Square(MaxFlySpeed))
    {
        CurrentVelocity = CurrentVelocity.GetSafeNormal() * MaxFlySpeed;
    }

    // Turning
    FRotator CurrentRotation = PawnOwner->GetActorRotation();
    float YawChange = CurrentSteeringInput * TurnSpeed * DeltaTime;
    CurrentRotation.Yaw += YawChange;
    UpdatedComponent->SetWorldRotation(CurrentRotation); // UpdatedComponent is the component we are moving (usually root)
}

void UPodracerMovementComponent::ApplyHover(float DeltaTime)
{
    if (!PawnOwner || !UpdatedComponent) return;

    FVector ActorLocation = UpdatedComponent->GetComponentLocation();
    FVector TraceStart = ActorLocation;
    FVector TraceEnd = ActorLocation - FVector(0, 0, 1) * (TargetHoverHeight + 200.0f); // Trace further

    FHitResult HitResult;
    TArray<AActor*> ActorsToIgnore;
    ActorsToIgnore.Add(PawnOwner);

    bool bHit = UKismetSystemLibrary::LineTraceSingle(
        GetWorld(), TraceStart, TraceEnd, UEngineTypes::ConvertToTraceType(ECC_Visibility),
        false, ActorsToIgnore, EDrawDebugTrace::None, HitResult, true);

    FVector TargetUp = FVector::UpVector; // Default to world up if no ground
    float CurrentGroundDistance = TargetHoverHeight + 100.f; // Assume far if no hit

    if (bHit)
    {
        CurrentGroundDistance = HitResult.Distance;
        TargetUp = HitResult.ImpactNormal; // Align to ground normal
    }
    
    // Calculate desired Z position (simplified, not physics force based)
    float HeightError = TargetHoverHeight - CurrentGroundDistance;
    float VerticalAdjustment = HeightError * HoverStiffness * DeltaTime;

    // Apply damping to vertical movement to prevent bouncing
    float VerticalVelocity = CurrentVelocity.Z; // Assuming Z is up for velocity component
    VerticalAdjustment -= VerticalVelocity * HoverDamping * DeltaTime;
    
    // Limit hover effect close to ground
    if (CurrentGroundDistance < MinGroundDistanceForFullHoverEffect)
    {
        VerticalAdjustment *= FMath::Clamp(CurrentGroundDistance / MinGroundDistanceForFullHoverEffect, 0.1f, 1.0f);
    }


    // Kinematically adjust position
    FVector NewLocation = UpdatedComponent->GetComponentLocation();
    NewLocation.Z += VerticalAdjustment; // Directly adjust Z based on hover calculation
    
    // Adjust velocity based on hover (this makes it react more like a force)
    CurrentVelocity.Z += VerticalAdjustment / DeltaTime; // Crude way to simulate force effect on velocity

    // Align to surface (simple version)
    FRotator TargetRotation = FQuat::FindBetweenNormals(PawnOwner->GetActorUpVector(), TargetUp).Rotator();
    FRotator NewActorRotation = PawnOwner->GetActorRotation() + TargetRotation * 0.1f; // Smoothly interpolate
    UpdatedComponent->SetWorldRotation(NewActorRotation.Quaternion());
}


void UPodracerMovementComponent::SimulateMovement(float DeltaTime)
{
    if (!PawnOwner || !UpdatedComponent || DeltaTime == 0.f)
    {
        return;
    }

    ApplyControlInputToVelocity(DeltaTime);
    ApplyHover(DeltaTime); // Hover will also affect CurrentVelocity.Z

    // Perform Move
    FVector Delta = CurrentVelocity * DeltaTime;
    FHitResult Hit;
    SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

    // If we bumped into something, try to slide along it
    if (Hit.IsValidBlockingHit())
    {
        SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit);
    }
}


void UPodracerMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!PawnOwner || !UpdatedComponent || ShouldSkipUpdate(DeltaTime))
    {
        return;
    }

    // For networked games:
    // Autonomous Proxy (Client controlling this pawn)
    if (PawnOwner->IsLocallyControlled() && GetNetMode() != NM_DedicatedServer)
    {
        // Create a move
        FPodracerMove Move;
        Move.ForwardInput = CurrentThrottleInput; // Already set by pawn's input functions
        Move.TurnInput = CurrentSteeringInput;    // Already set by pawn's input functions
        Move.DeltaTime = DeltaTime;
        Move.TimeStamp = GetWorld()->GetTimeSeconds(); // Simple timestamp

        // Client-side prediction: Simulate the move locally immediately
        SimulateMovement(DeltaTime); // Simulate locally

        // Send move to server
        UnacknowledgedMoves.Add(Move); // Store for reconciliation (basic)
        Server_SendMove(Move); // RPC to server
        LastMove = Move;
    }
    // Simulated Proxy (Other clients) or Server
    else if (PawnOwner->GetLocalRole() == ROLE_SimulatedProxy || PawnOwner->GetLocalRole() == ROLE_Authority)
    {
         // Server directly simulates.
         // Simulated proxies just rely on replicated state (UpdatedComponent's transform, replicated CurrentVelocity)
         // If you replicate CurrentVelocity, you can use it here for smoother interpolation on simulated proxies.
         // Server authoritative movement:
         if(PawnOwner->GetLocalRole() == ROLE_Authority)
         {
            SimulateMovement(DeltaTime);
         }
    }
}