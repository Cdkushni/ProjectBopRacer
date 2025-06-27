#include "PodMovementComponent.h"
#include "ReplicatedPodRacer.h" // Important to include the new Pawn
#include "Components/BoxComponent.h"
#include "EngineComponent.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/Core/PushModel/PushModel.h"

UPodMovementComponent::UPodMovementComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    bWantsInitializeComponent = true;
    bReplicateUsingRegisteredSubObjectList = true;
    ServerState = FPodRacerState();
    bWasOnGroundLastFrame = false;
    LastCreatedMove = FPodRacerMoveStruct();
    //NetUpdateFrequency = 60.0f;
    //MinNetUpdateFrequency = 20.0f;
    MoveSendInterval = 0.2f;
    StartupDelayTimer = 1.0f;
}

void UPodMovementComponent::BeginPlay()
{
    Super::BeginPlay();
    StartupDelayTimer = 1.0f;
    MoveSendTimer = MoveSendInterval;
    if (AReplicatedPodRacer* PodRacer = Cast<AReplicatedPodRacer>(GetOwner()))
    {
        Engines = PodRacer->GetEngines();
    }
    if (bEnableDebugLogging)
    {
        UBoxComponent* PhysicsBody = GetPhysicsBody();
        if (PhysicsBody)
        {
            UE_LOG(LogTemp, Log, TEXT("PodMovement BeginPlay: Pos=%s, Vel=%s, GroundNormal=%s, Role=%d"), *PhysicsBody->GetComponentLocation().ToString(), *PhysicsBody->GetPhysicsLinearVelocity().ToString(), *ServerState.GroundNormal.ToString(), (int32)GetOwner()->GetLocalRole());
        }
    }
}

void UPodMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (StartupDelayTimer > 0.0f)
    {
        StartupDelayTimer -= DeltaTime; return;
    }
    if (!PawnOwner || !UpdatedComponent || ShouldSkipUpdate(DeltaTime)) return;

    UpdateMoveSendInterval(DeltaTime);
    UBoxComponent* PhysicsBody = GetPhysicsBody();
    if (PhysicsBody)
    {
        // Only apply hover for locally controlled or server-authoritative vehicles
        if (PawnOwner->IsLocallyControlled() || PawnOwner->HasAuthority())
        {
            ApplyHover(DeltaTime, PhysicsBody);
        }
    }

    if (PawnOwner->IsLocallyControlled())
    {
        // Smooth rudder input
        SmoothedRudderInput = FMath::FInterpTo(SmoothedRudderInput, RawRudderInput, DeltaTime, 5.0f);
        LastCreatedMove = CreateMove(DeltaTime);
        SimulateMove(LastCreatedMove);
        MoveSendTimer -= DeltaTime;
        if (!bDisableServerReconciliation && MoveSendTimer <= 0.0f)
        {
            UnacknowledgedMoves.Add(LastCreatedMove);
            Server_SendMove(LastCreatedMove);
            if (bEnableDebugLogging)
            {
                UE_LOG(LogTemp, Log, TEXT("Sending Move: MoveNumber=%d, Timestamp=%.3f, Pos=%s, Thruster=%.3f, Rudder=%.3f"),
                LastCreatedMove.MoveNumber, LastCreatedMove.Timestamp, *GetPhysicsBody()->GetComponentLocation().ToString(),
                LastCreatedMove.ThrusterInput, LastCreatedMove.RudderInput);
            }
            MoveSendTimer = MoveSendInterval;
        }
        else if (PawnOwner->HasAuthority())
        {
            // Handle server-controlled vehicles
            SmoothedRudderInput = FMath::FInterpTo(SmoothedRudderInput, RawRudderInput, DeltaTime, 5.0f);
            LastCreatedMove = CreateMove(DeltaTime);
            SimulateMove(LastCreatedMove);
            UpdateServerState(DeltaTime);
            // TODO: Force replication
            GetOwner()->ForceNetUpdate();
            if (bEnableDebugLogging)
            {
                UE_LOG(LogTemp, Log, TEXT("Server Vehicle Move: MoveNumber=%d, Timestamp=%.3f, Pos=%s, Thruster=%.3f, Rudder=%.3f"),
                    LastCreatedMove.MoveNumber, LastCreatedMove.Timestamp, *GetPhysicsBody()->GetComponentLocation().ToString(),
                    LastCreatedMove.ThrusterInput, LastCreatedMove.RudderInput);
            }
        }
    }
}

void UPodMovementComponent::UpdateMoveSendInterval(float DeltaTime)
{
    if (PawnOwner->IsLocallyControlled())
    {
        float LastMoveAckTime = ServerState.LastMove.IsValid() ? ServerState.LastMove.Timestamp : GetWorld()->GetTimeSeconds();
        float CurrentTime = GetWorld()->GetTimeSeconds();
        EstimatedLatency = FMath::Lerp(EstimatedLatency, CurrentTime - LastMoveAckTime, 0.2f);
        MoveSendInterval = FMath::Clamp(0.05f + EstimatedLatency * 2.0f, 0.05f, 0.5f);
        /*
        if (bEnableDebugLogging)
        {
            UE_LOG(LogTemp, Log, TEXT("Updated MoveSendInterval: Latency=%.3f, Interval=%.3f, MovesBuffered=%d"), EstimatedLatency, MoveSendInterval, UnacknowledgedMoves.Num());
        }
        */
    }
}

void UPodMovementComponent::ApplyHover(float DeltaTime, UBoxComponent* PhysicsBody)
{
    if (!PhysicsBody) return;
    FVector Start = PhysicsBody->GetComponentLocation();
    FVector End = Start - FVector::UpVector * MaxGroundDist;
    FHitResult HitResult;
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(PawnOwner);
    Height = MaxGroundDist;

    if (GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, GroundCollisionChannel, QueryParams))
    {
        bIsOnGround = true;
        Height = HitResult.Distance;
        GroundNormal = HitResult.Normal.GetSafeNormal();
        if (!GroundNormal.IsNormalized() || GroundNormal.ContainsNaN())
        {
            GroundNormal = FVector::UpVector;
        }
        // Set position at HoverHeight above ground
        FVector TargetLocation = HitResult.Location + GroundNormal * HoverHeight;
        PhysicsBody->SetWorldLocation(TargetLocation, false, nullptr, ETeleportType::TeleportPhysics);
        // Align rotation to ground normal
        FVector Projection = UKismetMathLibrary::ProjectVectorOnToPlane(PhysicsBody->GetForwardVector(), GroundNormal);
        if (!Projection.IsNearlyZero() && !GroundNormal.IsNearlyZero())
        {
            FRotator TargetRotation = UKismetMathLibrary::MakeRotFromZX(GroundNormal, Projection);
            FRotator NewRotation = FMath::RInterpTo(PhysicsBody->GetComponentRotation(), TargetRotation, DeltaTime, RotationInterpSpeed);
            PhysicsBody->SetWorldRotation(NewRotation);
        }

        // Preserve XY velocity, clear Z
        FVector CurrentVelocity = PhysicsBody->GetPhysicsLinearVelocity();
        CurrentVelocity = FVector::VectorPlaneProject(CurrentVelocity, GroundNormal);
        if (CurrentVelocity.SizeSquared() > FMath::Square(MaxVelocity))
        {
            CurrentVelocity = CurrentVelocity.GetSafeNormal() * MaxVelocity;
        }
        PhysicsBody->SetPhysicsLinearVelocity(CurrentVelocity, false);
        /*
        if (bEnableDebugLogging)
        {
            UE_LOG(LogTemp, Log, TEXT("Hover: Grounded, Height=%.1f, Pos=%s, Vel=%s, Normal=%s"),
                Height, *TargetLocation.ToString(), *CurrentVelocity.ToString(), *GroundNormal.ToString());
        }
        */
    }
    else
    {
        bIsOnGround = false;
        GroundNormal = FVector::UpVector;
        // Apply gravity when airborne
        PhysicsBody->AddForce(FVector(0.0f, 0.0f, -FallGravity * Mass));
        FVector CurrentVelocity = PhysicsBody->GetPhysicsLinearVelocity();
        if (CurrentVelocity.SizeSquared() > FMath::Square(MaxVelocity))
        {
            CurrentVelocity = CurrentVelocity.GetSafeNormal() * MaxVelocity;
            PhysicsBody->SetPhysicsLinearVelocity(CurrentVelocity, false);
        }
        if (bEnableDebugLogging)
        {
            UE_LOG(LogTemp, Log, TEXT("Hover: Airborne, Vel=%s"), *CurrentVelocity.ToString());
        }
    }

    if (PawnOwner->HasAuthority())
    {
        ServerState.GroundNormal = GroundNormal;
    }
    else if (!ServerState.GroundNormal.IsNormalized())
    {
        GroundNormal = FVector::UpVector;
    }

    if (bWasOnGroundLastFrame != bIsOnGround)
    {
        OnGroundStateChanged.Broadcast(bIsOnGround);
        bWasOnGroundLastFrame = bIsOnGround;
    }
}

void UPodMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UPodMovementComponent, ServerState, COND_SkipOwner);
    DOREPLIFETIME(UPodMovementComponent, bWasOnGroundLastFrame);
}

FPodRacerMoveStruct UPodMovementComponent::CreateMove(float DeltaTime)
{
    FPodRacerMoveStruct Move;
    Move.DeltaTime = DeltaTime;
    Move.ThrusterInput = RawThrusterInput;
    Move.RudderInput = SmoothedRudderInput;
    Move.bIsBraking = bIsBrakingInput;
    Move.bIsDrifting = bIsDriftingInput;
    Move.bIsBoosting = bIsBoostingInput;
    /*
    // Apply keyboard input smoothing
    float InterpSpeed = FMath::IsNearlyZero(RawRudderInput) ? KeyboardSteeringReturnSpeed : KeyboardSteeringInterpSpeed;
    SmoothedRudderInput = FMath::FInterpTo(SmoothedRudderInput, RawRudderInput, DeltaTime, InterpSpeed);
    Move.RudderInput = SmoothedRudderInput;
    CurrentMoveNumber++;
    Move.MoveNumber = CurrentMoveNumber;
    */
    Move.MoveNumber = ++CurrentMoveNumber;
    Move.Timestamp = GetWorld()->GetTimeSeconds();

    return Move;

}

void UPodMovementComponent::SimulateMove(const FPodRacerMoveStruct& Move) {
    // Implement your movement simulation logic here
    // // Example: Apply thruster, rudder, braking, drifting, boosting forces
    UBoxComponent* PhysicsBody = GetPhysicsBody();
    if (!PhysicsBody) return;
    float DeltaTime = Move.DeltaTime;
    if (DeltaTime <= 0.0f) return;

    FVector ForwardVector = PhysicsBody->GetForwardVector();
    FVector CurrentVelocity = PhysicsBody->GetPhysicsLinearVelocity();
    float ControlMultiplier = bIsOnGround ? 1.0f : AirControlMultiplier;
    float EffectiveMaxSpeed = MaxSpeed * (Move.bIsBoosting ? BoostSpeedMultiplier : 1.0f);

    // Apply rotation (yaw) from RudderInput
    float EffectiveTurnRate = TurnRate * ControlMultiplier * (Move.bIsDrifting ? DriftTurnRateMultiplier : 1.0f);
    float YawDelta = Move.RudderInput * EffectiveTurnRate * DeltaTime;
    FRotator CurrentRotation = PhysicsBody->GetComponentRotation();
    FRotator NewRotation = FRotator(CurrentRotation.Pitch, CurrentRotation.Yaw + YawDelta, CurrentRotation.Roll);
    PhysicsBody->SetWorldRotation(NewRotation);

    // Apply velocity from ThrusterInput
    if (Move.bIsBraking)
    {
        // Decelerate
        FVector VelocityDir = CurrentVelocity.GetSafeNormal();
        float Speed = CurrentVelocity.Size();
        if (Speed > 0.0f)
        {
            float NewSpeed = FMath::Max(0.0f, Speed - BrakeDeceleration * DeltaTime);
            CurrentVelocity = VelocityDir * NewSpeed;
        }
    }
    else
    {
        // Accelerate
        FVector AccelerationVector = ForwardVector * Move.ThrusterInput * Acceleration * ControlMultiplier;
        CurrentVelocity += AccelerationVector * DeltaTime;
    }

    // Clamp velocity
    if (bIsOnGround)
    {
        CurrentVelocity = FVector::VectorPlaneProject(CurrentVelocity, GroundNormal);
    }
    float Speed = CurrentVelocity.Size();
    if (Speed > EffectiveMaxSpeed)
    {
        CurrentVelocity = CurrentVelocity.GetSafeNormal() * EffectiveMaxSpeed;
    }
    PhysicsBody->SetPhysicsLinearVelocity(CurrentVelocity, false);

    if (bEnableDebugLogging)
    {
        UE_LOG(LogTemp, Log, TEXT("SimulateMove: Thruster=%.3f, Rudder=%.3f, Braking=%d, Drifting=%d, Boosting=%d, Vel=%s, Rot=%s"),
            Move.ThrusterInput, Move.RudderInput, Move.bIsBraking, Move.bIsDrifting, Move.bIsBoosting,
            *CurrentVelocity.ToString(), *NewRotation.ToString());
    }
}

void UPodMovementComponent::Server_SendMove_Implementation(const FPodRacerMoveStruct& Move)
{
    if (!PawnOwner || !PawnOwner->HasAuthority()) return;
    UBoxComponent* PhysicsBody = GetPhysicsBody();
    if (!PhysicsBody) return;

    UnacknowledgedMoves.Add(Move);
    int32 NumMovesToProcess = UnacknowledgedMoves.Num() > 3 ? 2 : 1;
    for (int32 i = 0; i < NumMovesToProcess && UnacknowledgedMoves.Num() > 0; i++)
    {
        const FPodRacerMoveStruct& CurrentMove = UnacknowledgedMoves[0];
        ServerState.LastMove = CurrentMove;
        SimulateMove(CurrentMove);
        UnacknowledgedMoves.RemoveAt(0);
    }
    ServerState.Transform = PhysicsBody->GetComponentTransform();
    ServerState.LinearVelocity = PhysicsBody->GetPhysicsLinearVelocity();
    ServerState.AngularVelocity = PhysicsBody->GetPhysicsAngularVelocityInRadians();
    ServerState.GroundNormal = GroundNormal;
    ServerState.ReplicationCounter = ++ServerStateReplicationCounter;
    // TODO: Force replication
    GetOwner()->ForceNetUpdate();

    if (bEnableDebugLogging)
    {
        UE_LOG(LogTemp, Log, TEXT("Server processed move: MoveNumber=%d, MovesRemaining=%d, Pos=%s"),
            Move.MoveNumber, UnacknowledgedMoves.Num(), *ServerState.Transform.GetLocation().ToString());
    }
}

bool UPodMovementComponent::Server_SendMove_Validate(const FPodRacerMoveStruct& Move)
{
    UBoxComponent* PhysicsBody = GetPhysicsBody();
    bool bValid = Move.IsValid() && FMath::Abs(Move.DeltaTime) < 1.0f && FMath::Abs(Move.ThrusterInput) <= 1.0f && FMath::Abs(Move.RudderInput) <= 1.0f;
    if (PhysicsBody)
    {
        FVector ClientPos = PhysicsBody->GetComponentLocation();
        FVector ServerPos = ServerState.Transform.GetLocation();
        float ZDiff = FMath::Abs(ClientPos.Z - ServerPos.Z);
        float XYDiff = FVector::DistXY(ClientPos, ServerPos);
        bValid = bValid && ZDiff < 1000.0f && XYDiff < 20000.0f; // 10m Z, 200m XY
        if (!bValid && bEnableDebugLogging)
        {
            UE_LOG(LogTemp, Warning, TEXT("Server rejected move: DeltaTime=%.3f, Thruster=%.3f, Rudder=%.3f, ZDiff=%.1f, XYDiff=%.1f"), Move.DeltaTime, Move.ThrusterInput, Move.RudderInput, ZDiff, XYDiff);
        }
    }
    return bValid;
}

void UPodMovementComponent::UpdateServerState(float DeltaTime)
{
    UBoxComponent* PhysicsBody = GetPhysicsBody();
    if (!PhysicsBody || !PawnOwner->HasAuthority()) return;
    // Always update ServerState to ensure DOREPLIFETIME detects changes
    ServerState.LastMove = LastCreatedMove;
    ServerState.Transform = PhysicsBody->GetComponentTransform();
    ServerState.LinearVelocity = PhysicsBody->GetPhysicsLinearVelocity();
    ServerState.AngularVelocity = PhysicsBody->GetPhysicsAngularVelocityInRadians();
    ServerState.GroundNormal = GroundNormal;
    ServerState.ReplicationCounter = ++ServerStateReplicationCounter; // Force replication
    

    ServerStateUpdateTimer -= DeltaTime;
    bool bNeedsUpdate = FVector::Dist(PhysicsBody->GetComponentLocation(), LastServerPosition) > ServerStateUpdateThreshold ||
                        ServerState.LinearVelocity != PhysicsBody->GetPhysicsLinearVelocity() ||
                        ServerState.GroundNormal != GroundNormal ||
                        ServerStateUpdateTimer <= 0.0f;

    if (bNeedsUpdate)
    {
        LastServerPosition = PhysicsBody->GetComponentLocation();
        ServerStateUpdateTimer = ServerStateForceUpdateInterval;
        GetOwner()->ForceNetUpdate();

        if (bEnableDebugLogging)
        {
            UE_LOG(LogTemp, Log, TEXT("UpdateServerState: Pos=%s, Vel=%s, Normal=%s, ForcedNetUpdate, Role=%d"),
                *ServerState.Transform.GetLocation().ToString(),
                *ServerState.LinearVelocity.ToString(),
                *ServerState.GroundNormal.ToString(), (int32)GetOwner()->GetLocalRole());
        }
    }
    else if (bEnableDebugLogging)
    {
        UE_LOG(LogTemp, Log, TEXT("UpdateServerState: Skipped, PosDiff=%.1f, Timer=%.3f, Role=%d"),
            FVector::Dist(PhysicsBody->GetComponentLocation(), LastServerPosition),
            ServerStateUpdateTimer, (int32)GetOwner()->GetLocalRole());
    }
}

void UPodMovementComponent::OnRep_ServerState()
{
    // Implement state reconciliation logic here
    // // Example: Correct client position based on ServerState
    UBoxComponent* PhysicsBody = GetPhysicsBody();
    if (!PhysicsBody || !PawnOwner->IsLocallyControlled()) return;
    if (true)
    {
        FVector CurrentPos = PhysicsBody->GetComponentLocation();
        FVector ServerPos = ServerState.Transform.GetLocation();
        float PosDiff = FVector::Dist(CurrentPos, ServerPos);
        GEngine->AddOnScreenDebugMessage(-1, 0, FColor::Green, *FString::Printf(TEXT("Client Pos: (%f, %f, %f) | Server Pos: (%f, %f, %f) | PosDiff: %f"),
            CurrentPos.X, CurrentPos.Y, CurrentPos.Z, ServerPos.X, ServerPos.Y, ServerPos.Z, PosDiff), true);
    }
    if (PawnOwner->IsLocallyControlled())
    {
        // Handle locally controlled vehicles (client player)
        FVector ClientPos = PhysicsBody->GetComponentLocation();
        FVector ServerPos = ServerState.Transform.GetLocation();
        float PosDiff = FVector::Dist(ClientPos, ServerPos);
        bool bNeedsCorrection = PosDiff > CorrectionThreshold;

        if (bNeedsCorrection)
        {
            // Interpolate position
            FVector NewPos = FMath::VInterpTo(ClientPos, ServerPos, GetWorld()->GetDeltaSeconds(), CorrectionInterpSpeed);
            PhysicsBody->SetWorldLocation(NewPos, false, nullptr, ETeleportType::TeleportPhysics);

            // Update velocity
            FVector ClientVel = PhysicsBody->GetPhysicsLinearVelocity();
            FVector ServerVel = ServerState.LinearVelocity;
            if (FVector::DistSquared(ClientVel, ServerVel) > FMath::Square(10.0f))
            {
                PhysicsBody->SetPhysicsLinearVelocity(ServerVel, false);
            }

            // Interpolate rotation
            FRotator ClientRot = PhysicsBody->GetComponentRotation();
            FRotator ServerRot = ServerState.Transform.Rotator();
            FRotator NewRot = FMath::RInterpTo(ClientRot, ServerRot, GetWorld()->GetDeltaSeconds(), CorrectionInterpSpeed);
            PhysicsBody->SetWorldRotation(NewRot);

            // Replay unacknowledged moves
            TArray<FPodRacerMoveStruct> MovesToReplay;
            for (const FPodRacerMoveStruct& UnackedMove : UnacknowledgedMoves)
            {
                if (UnackedMove.MoveNumber > ServerState.LastMove.MoveNumber)
                {
                    MovesToReplay.Add(UnackedMove);
                }
            }
            for (const FPodRacerMoveStruct& Move : MovesToReplay)
            {
                SimulateMove(Move);
            }

            if (bEnableDebugLogging)
            {
                UE_LOG(LogTemp, Log, TEXT("OnRep_ServerState: Corrected, PosDiff=%.1f, ClientPos=%s, ServerPos=%s, ReplayedMoves=%d"),
                    PosDiff, *ClientPos.ToString(), *ServerPos.ToString(), MovesToReplay.Num());
            }
        }
        else if (bEnableDebugLogging)
        {
            UE_LOG(LogTemp, Log, TEXT("OnRep_ServerState: No correction needed for local vehicle, PosDiff=%.1f, Role=%d"),
                PosDiff, (int32)GetOwner()->GetLocalRole());
        }
    }
    else
    {
        // Handle non-locally controlled vehicles (e.g., server player on client)
        FVector CurrentPos = PhysicsBody->GetComponentLocation();
        FVector ServerPos = ServerState.Transform.GetLocation();
        float PosDiff = FVector::Dist(CurrentPos, ServerPos);

        // Interpolate position
        FVector NewPos = FMath::VInterpTo(CurrentPos, ServerPos, GetWorld()->GetDeltaSeconds(), CorrectionInterpSpeed);
        PhysicsBody->SetWorldLocation(NewPos, false, nullptr, ETeleportType::TeleportPhysics);

        // Update velocity
        PhysicsBody->SetPhysicsLinearVelocity(ServerState.LinearVelocity, false);

        // Interpolate rotation
        FRotator CurrentRot = PhysicsBody->GetComponentRotation();
        FRotator ServerRot = ServerState.Transform.Rotator();
        FRotator NewRot = FMath::RInterpTo(CurrentRot, ServerRot, GetWorld()->GetDeltaSeconds(), CorrectionInterpSpeed);
        PhysicsBody->SetWorldRotation(NewRot);

        // Update ground normal for consistency
        GroundNormal = ServerState.GroundNormal;

        if (bEnableDebugLogging)
        {
            UE_LOG(LogTemp, Log, TEXT("OnRep_ServerState: Updated remote vehicle, PosDiff=%.1f, CurrentPos=%s, ServerPos=%s, Role=%d"),
                PosDiff, *CurrentPos.ToString(), *ServerPos.ToString(), (int32)GetOwner()->GetLocalRole());
        }
    }
}

// Helper Getters
AReplicatedPodRacer* UPodMovementComponent::GetPodRacerOwner() const { return Cast<AReplicatedPodRacer>(PawnOwner); }

UBoxComponent* UPodMovementComponent::GetPhysicsBody() const
{
    return GetPodRacerOwner() ? GetPodRacerOwner()->GetPhysicsBody() : nullptr;
}




/*
// Standard boilerplate for getting replicated properties
void UPodMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UPodMovementComponent, ServerState);
}

UPodMovementComponent::UPodMovementComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    SetIsReplicatedByDefault(true);
}

void UPodMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (StartupDelayTimer > 0.0f)
    {
        StartupDelayTimer -= DeltaTime;
        return;
    }

    UpdateLatencyEstimation(DeltaTime);

    if (!PawnOwner || !UpdatedComponent || ShouldSkipUpdate(DeltaTime)) return;

    if (PawnOwner->IsLocallyControlled())
    {
        LastCreatedMove = CreateMove(DeltaTime);
        SimulateMove(LastCreatedMove);

        // Only send moves to the server if reconciliation is enabled
        if (!bDisableServerReconciliation)
        {
            UnacknowledgedMoves.Add(LastCreatedMove);
            Server_SendMove(LastCreatedMove);
        }
    }
    else if (PawnOwner->GetLocalRole() == ROLE_Authority && !PawnOwner->IsLocallyControlled())
    {
        // Server-controlled pawn (AI, etc.). Not covered in this example.
    }
    else // SimulatedProxy
    {
        // Smoothly interpolate to the server's state on other clients
        if (UBoxComponent* PhysicsBody = GetPhysicsBody())
        {
            FVector NewLocation = FMath::VInterpTo(PhysicsBody->GetComponentLocation(), ServerState.Transform.GetLocation(), DeltaTime, 15.f);
            FQuat NewRotation = FMath::QInterpTo(PhysicsBody->GetComponentQuat(), ServerState.Transform.GetRotation(), DeltaTime, 15.f);
            PhysicsBody->SetWorldLocationAndRotation(NewLocation, NewRotation);
        }
    }
}

FPodRacerMoveStruct UPodMovementComponent::CreateMove(float DeltaTime)
{
    FPodRacerMoveStruct Move;
    Move.DeltaTime = DeltaTime;
    Move.ThrusterInput = RawThrusterInput;
    Move.bIsBraking = bIsBrakingInput;
    Move.bIsDrifting = bIsDriftingInput;
    Move.bIsBoosting = bIsBoostingInput;

    // Apply keyboard input smoothing
    float InterpSpeed = FMath::IsNearlyZero(RawRudderInput) ? KeyboardSteeringReturnSpeed : KeyboardSteeringInterpSpeed;
    SmoothedRudderInput = FMath::FInterpTo(SmoothedRudderInput, RawRudderInput, DeltaTime, InterpSpeed);
    Move.RudderInput = SmoothedRudderInput;
    CurrentMoveNumber++;
    Move.MoveNumber = CurrentMoveNumber;

    return Move;
}

void UPodMovementComponent::SimulateMove(const FPodRacerMoveStruct& Move)
{
    UBoxComponent* PhysicsBody = GetPhysicsBody();
    if (!PhysicsBody) return;
    
    // Apply all physics forces from the move
    ApplyHover(Move.DeltaTime, PhysicsBody);
    ApplyPropulsion(Move, PhysicsBody);

    // Clamp speed
    FVector CurrentVelocity = PhysicsBody->GetPhysicsLinearVelocity();
    if (CurrentVelocity.SizeSquared() > FMath::Square(TerminalVelocity))
    {
        PhysicsBody->SetPhysicsLinearVelocity(CurrentVelocity.GetSafeNormal() * TerminalVelocity);
    }
}

void UPodMovementComponent::ApplyHover(float DeltaTime, UBoxComponent* PhysicsBody)
{
    FVector GroundNormal = FVector::UpVector;
    bool bIsOnGround = false;
    float Height = MaxGroundDist;

    FVector Start = PhysicsBody->GetComponentLocation();
    FVector End = Start - PhysicsBody->GetUpVector() * MaxGroundDist;
    FHitResult HitResult;
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(PawnOwner);
    
    if (GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, GroundCollisionChannel, QueryParams))
    {
        bIsOnGround = true;
        Height = HitResult.Distance;
        GroundNormal = HitResult.Normal.GetSafeNormal();
        if (!GroundNormal.IsNormalized() || GroundNormal.ContainsNaN())
        {
            GroundNormal = FVector::UpVector;
        }
    } else
    {
        bIsOnGround = false;
        GroundNormal = FVector::UpVector;
    }

    if (PawnOwner->HasAuthority())
    {
        ServerState.GroundNormal = GroundNormal;
    }
    else if (!ServerState.GroundNormal.IsNormalized())
    {
        GroundNormal = FVector::UpVector;
    }

    if (bIsOnGround)
    {
        if (bUseFixedHeightMode)
        {
            // Fixed-height mode: Set position directly
            FVector TargetLocation = HitResult.Location + FVector::UpVector * HoverHeight;
            PhysicsBody->SetWorldLocation(TargetLocation, false, nullptr, ETeleportType::TeleportPhysics);
            PhysicsBody->SetPhysicsLinearVelocity(FVector::ZeroVector, false); // Clear Z-velocity
            if (bEnableDebugLogging)
            {
                UE_LOG(LogTemp, Log, TEXT("FixedHeightMode: Height=%.1f, TargetPos=%s, Vel=%s"),
                    Height, *TargetLocation.ToString(), *PhysicsBody->GetPhysicsLinearVelocity().ToString());
            }
        }
        else
        {
            // PID mode
            float ForcePercent = HoverPID.Seek(HoverHeight, Height, DeltaTime);
            if (Height > HoverHeight)
            {
                ForcePercent *= 0.15f;
            }
            ForcePercent = FMath::Clamp(ForcePercent, HoverPID.Minimum, HoverPID.Maximum);
            float TotalHoverForce = 0.0f;
            if (AReplicatedPodRacer* Owner = GetPodRacerOwner())
            {
                for (UEngineComponent* Engine : Owner->GetEngines())
                {
                    if (Engine && Engine->GetState() != EEngineState::Destroyed)
                    {
                        TotalHoverForce += Engine->GetHoverForce(ForcePercent);
                    }
                }
            }
            FVector HoverForce = GroundNormal * TotalHoverForce;
            PhysicsBody->AddForceAtLocation(HoverForce, Start);
            if (bEnableDebugLogging)
            {
                float Error = HoverHeight - Height;
                float Derivative = DeltaTime > KINDA_SMALL_NUMBER ? (Error - HoverPID.LastProportional) / DeltaTime : 0.0f;
                FVector CurrentVelocity = PhysicsBody->GetPhysicsLinearVelocity();
                UE_LOG(LogTemp, Log, TEXT("PIDMode: Height=%.1f, ForcePercent=%.3f, P=%.1f, I=%.1f, D=%.1f, Vel=%s"),
                    Height, ForcePercent, Error * HoverPID.PCoeff, HoverPID.Integral * HoverPID.ICoeff, Derivative * HoverPID.DCoeff, *CurrentVelocity.ToString());
            }
        }
    }
    else
    {
        HoverPID.Reset();
        PhysicsBody->AddForce(FVector(0.0f, 0.0f, -FallGravity * Mass));
        if (bEnableDebugLogging)
        {
            UE_LOG(LogTemp, Log, TEXT("Airborne: Vel=%s"), *PhysicsBody->GetPhysicsLinearVelocity().ToString());
        }
    }

    if (bWasOnGroundLastFrame != bIsOnGround)
    {
        OnGroundStateChanged.Broadcast(bIsOnGround);
        bWasOnGroundLastFrame = bIsOnGround;
    }

    return;
    // Reset PID on ground transition
    if (bIsOnGround && !bWasOnGroundLastFrame)
    {
        HoverPID.Reset();
    }
    bWasOnGroundLastFrame = bIsOnGround;
    if (bEnableDebugLogging) // Draw debug
    {
        DrawDebugLine(GetWorld(), Start, End, bIsOnGround ? FColor::Green : FColor::Red, false, 0.0f, 0, 1.0f);
        if (bIsOnGround)
        {
            DrawDebugSphere(GetWorld(), Start - GetOwner()->GetActorUpVector() * HoverHeight, 10.0f, 12, FColor::Blue, false, 0.0f);
            DrawDebugString(GetWorld(), PhysicsBody->GetComponentLocation(), FString::Printf(TEXT("Height: %.1f"), Height), nullptr, FColor::White, 0.0f);
            if (AReplicatedPodRacer* Owner = GetPodRacerOwner())
            {
                for (UEngineComponent* Engine : Owner->GetEngines())
                {
                    DrawDebugString(GetWorld(), Engine->GetComponentLocation(), FString::Printf(TEXT("Health: %.1f"), Engine->GetHealth()), nullptr, FColor::Yellow, 0.0f);
                }
            }
        }
    }

    if (bIsOnGround)
    {
        float ForcePercent = HoverPID.Seek(HoverHeight, HitResult.Distance, DeltaTime);
        if (Height > HoverHeight)
        {
            ForcePercent *= 0.5f; // Weaken upward force for descent
        }
        if (AReplicatedPodRacer* Owner = GetPodRacerOwner())
        {
            for (UEngineComponent* Engine : Owner->GetEngines())
            {
                if (Engine)
                {
                    float EngineHoverForce = Engine->GetHoverForce(ForcePercent);
                    //FVector Force = GroundNormal * EngineHoverForce / Mass;
                    //PhysicsBody->AddForceAtLocation(Force * Mass, Engine->GetForceApplicationPoint());
                    FVector Force = GroundNormal * EngineHoverForce;
                    PhysicsBody->AddForceAtLocation(Force, Engine->GetForceApplicationPoint());

                    if (bEnableDebugLogging) // debug
                    {
                        FVector ForceEnd = Engine->GetForceApplicationPoint() + Force.GetSafeNormal() * 100.f * ForcePercent;
                        UKismetSystemLibrary::DrawDebugArrow(GetWorld(), Engine->GetForceApplicationPoint(), ForceEnd, 10.f, FColor::Cyan, 0, 5.0f);
                    }
                }
            }
        }
        FVector Gravity = -GroundNormal * HoverGravity;
        PhysicsBody->AddForce(Gravity * Mass);

        if (bEnableDebugLogging) // Debug
        {
            float Proportional = HoverHeight - Height;
            float Integral = HoverPID.Integral;
            float Derivative = (Proportional - HoverPID.LastProportional) / DeltaTime;
            UE_LOG(LogTemp, Log, TEXT("Height: %f, ForcePercent: %f, P: %f, I: %f, D: %f"), Height, ForcePercent, Proportional * HoverPID.PCoeff, Integral * HoverPID.ICoeff, Derivative * HoverPID.DCoeff);
        }
    }
    else
    {
        FVector Gravity = -GroundNormal * FallGravity;
        PhysicsBody->AddForce(Gravity * Mass);
        if (bEnableDebugLogging) // Debug
        {
            FVector CurrentVelocity = PhysicsBody->GetPhysicsLinearVelocity();
            UE_LOG(LogTemp, Log, TEXT("Airborne Velocity: X=%.1f, Y=%.1f, Z=%.1f"), CurrentVelocity.X, CurrentVelocity.Y, CurrentVelocity.Z);
        }
    }

    FVector Projection = UKismetMathLibrary::ProjectVectorOnToPlane(PhysicsBody->GetForwardVector(), GroundNormal);
    FRotator TargetRotation = UKismetMathLibrary::MakeRotFromZX(GroundNormal, Projection);
    FRotator NewRotation = FMath::RInterpTo(PhysicsBody->GetComponentRotation(), TargetRotation, DeltaTime, 5.0f);
    PhysicsBody->SetWorldRotation(NewRotation);

    float RollAngle = 30.f * -SmoothedRudderInput;//AngleOfRoll * -RudderInput;
    //FQuat BodyRotation = GetActorRotation().Quaternion() * FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(RollAngle));
    //FRotator LookAtRotation = UKismetMathLibrary::FindLookAtRotation(HullMesh->GetComponentLocation(), EngineConnectorRoot->GetComponentLocation());
    //FQuat CurrentBodyRotation = HullMesh->GetComponentQuat();
    //FQuat EngineRotation = GetActorRotation().Quaternion() * FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(RollAngle));
    //FQuat CurrentEngineRotation = EngineConnectorRoot->GetComponentQuat();

    ////////////// For angling the body visually
    //FVector Projection = UKismetMathLibrary::ProjectVectorOnToPlane(PhysicsBody->GetForwardVector(), GroundNormal);
    //FRotator TargetRotation = UKismetMathLibrary::MakeRotFromZX(GroundNormal, Projection);
    //FRotator NewRotation = FMath::RInterpTo(PhysicsBody->GetComponentRotation(), TargetRotation, DeltaTime, 5.0f);
    //PhysicsBody->SetWorldRotation(NewRotation);
}

void UPodMovementComponent::ApplyPropulsion(const FPodRacerMoveStruct& Move, UBoxComponent* PhysicsBody)
{
    float SidewaysSpeed = FVector::DotProduct(PhysicsBody->GetPhysicsLinearVelocity(), PhysicsBody->GetRightVector());
    FVector SideFriction = -PhysicsBody->GetRightVector() * SidewaysSpeed * SidewaysGripFactor * Mass;
    PhysicsBody->AddForce(SideFriction);

    if (Move.bIsBraking) PhysicsBody->SetPhysicsLinearVelocity(PhysicsBody->GetPhysicsLinearVelocity() * BrakingVelFactor);
    else if(FMath::IsNearlyZero(Move.ThrusterInput)) PhysicsBody->SetPhysicsLinearVelocity(PhysicsBody->GetPhysicsLinearVelocity() * SlowingVelFactor);

    if (AReplicatedPodRacer* Owner = GetPodRacerOwner())
    {
        for (UEngineComponent* Engine : Owner->GetEngines())
        {
            if(Engine)
            {
                float EngineThrust = Engine->GetThrustForce(Move.ThrusterInput, Move.bIsBoosting, Move.bIsDrifting, DriftMultiplier, BoostMultiplier);
                PhysicsBody->AddForceAtLocation(PhysicsBody->GetForwardVector() * EngineThrust, Engine->GetForceApplicationPoint());
            }
        }
    }

    // --- ✅ FIXED: Steering torque calculation ---
    
    if (FMath::Abs(Move.RudderInput) > 0.1)
    {
        float ForwardSpeed = FVector::DotProduct(PhysicsBody->GetPhysicsLinearVelocity(), PhysicsBody->GetForwardVector());
        //float TargetAngularVelocity = Move.RudderInput * MaxTurnRate * SpeedMultiplier * (Move.bIsDrifting ? DriftMultiplier : 1.f);
        float SpeedMultiplier = FMath::GetMappedRangeValueClamped(
            FVector2D(0.0f, TerminalVelocity),
            FVector2D(1.0f, HighSpeedSteeringDampFactor),
            ForwardSpeed
        );

        // 3. Define the target angular velocity using the SMOOTHED input
        float TargetAngularVelocity = Move.RudderInput * MaxTurnRate * SpeedMultiplier;//SmoothedRudderInput * MaxTurnRate * SpeedMultiplier;
        if (Move.bIsDrifting)
        {
            TargetAngularVelocity *= DriftMultiplier;
        }
        // 4. Apply torque using the P-Controller logic (from Suggestion 2)
        float CurrentAngularVelocity = PhysicsBody->GetPhysicsAngularVelocityInRadians().Z;
        float Error = TargetAngularVelocity - CurrentAngularVelocity;
        float RotationTorque = Error * SteeringMultiplier; 
        PhysicsBody->AddTorqueInRadians(FVector(0, 0, RotationTorque), NAME_None, true); // Apply torque in Radians
    }
}

void UPodMovementComponent::UpdateLatencyEstimation(float DeltaTime)
{
    if (PawnOwner->IsLocallyControlled())
    {
        float LastMoveAckTime = ServerState.LastMove.IsValid() ? ServerState.LastMove.Timestamp : GetWorld()->GetTimeSeconds();
        float CurrentTime = GetWorld()->GetTimeSeconds();
        EstimatedLatency = FMath::Lerp(EstimatedLatency, CurrentTime - LastMoveAckTime, 0.1f);
        bUseFixedHeightMode = EstimatedLatency > LatencyThreshold;
        if (bEnableDebugLogging)
        {
            UE_LOG(LogTemp, Log, TEXT("Latency Update: EstimatedLatency=%.3f, FixedHeightMode=%d"), EstimatedLatency, bUseFixedHeightMode);
        }
    }
}

void UPodMovementComponent::OnRep_ServerState()
{
    // If our debug flag is on, we completely ignore server corrections.
    if (bDisableServerReconciliation) return;

    // Only the Autonomous Proxy (the controlling client) needs to do reconciliation.
    if (PawnOwner->IsLocallyControlled()) // Autonomous Proxy
    {
        // We received an update from the server.
        UBoxComponent* PhysicsBody = GetPhysicsBody();
        if (!PhysicsBody) return;

        // Apply the server's state directly.
        // --- ✅ FIXED: This is the new, stable reconciliation logic ---
        // Temporarily disable physics simulation to prevent explosion from teleporting
        PhysicsBody->SetSimulatePhysics(false);
        // Set the transform of the component directly, not the physics body
        UpdatedComponent->SetWorldTransform(ServerState.Transform, false, nullptr, ETeleportType::TeleportPhysics);
        // Re-enable physics
        PhysicsBody->SetSimulatePhysics(true);
        // Now apply the server's velocities to the newly positioned physics body
		PhysicsBody->SetPhysicsLinearVelocity(ServerState.LinearVelocity, false);
		PhysicsBody->SetPhysicsAngularVelocityInRadians(ServerState.AngularVelocity, false);
        
        // Now, re-simulate all the moves we made locally that the server hasn't acknowledged yet.
        // This is the core of Reconciliation.
        ClearAcknowledgedMoves(ServerState.LastMove);
        for(const FPodRacerMoveStruct& Move : UnacknowledgedMoves)
        {
            SimulateMove(Move);
        }
    }
}

void UPodMovementComponent::ClearAcknowledgedMoves(const FPodRacerMoveStruct& LastServerMove)
{
    if (!LastServerMove.IsValid()) return;
    
    int32 MoveIndex = 0;
    while(MoveIndex < UnacknowledgedMoves.Num())
    {
        if (UnacknowledgedMoves[MoveIndex].MoveNumber <= LastServerMove.MoveNumber)
        {
            MoveIndex++;
        }
        else
        {
            break;
        }
    }

    if (MoveIndex > 0)
    {
        UnacknowledgedMoves.RemoveAt(0, MoveIndex, EAllowShrinking::No); // false to not shrink array
    }
}

bool UPodMovementComponent::Server_SendMove_Validate(const FPodRacerMoveStruct& Move)
{
    UBoxComponent* PhysicsBody = GetPhysicsBody();
    bool bValid = Move.IsValid() &&
                  FMath::Abs(Move.DeltaTime) < 1.0f &&
                  FMath::Abs(Move.ThrusterInput) <= 1.0f &&
                  FMath::Abs(Move.RudderInput) <= 1.0f;
    if (PhysicsBody)
    {
        bValid = bValid && FVector::DistSquared(PhysicsBody->GetComponentLocation(), ServerState.Transform.GetLocation()) < FMath::Square(20000.0f); // 200m
    }
    if (!bValid && bEnableDebugLogging)
    {
        UE_LOG(LogTemp, Warning, TEXT("Server rejected move: DeltaTime=%.3f, Thruster=%.3f, Rudder=%.3f, PosDiff=%.1f"),
            Move.DeltaTime, Move.ThrusterInput, Move.RudderInput,
            PhysicsBody ? FVector::Dist(PhysicsBody->GetComponentLocation(), ServerState.Transform.GetLocation()) : -1.0f);
    }
    return bValid;
}

void UPodMovementComponent::Server_SendMove_Implementation(const FPodRacerMoveStruct& Move)
{
    // The server receives a move from a client and simulates it.
    SimulateMove(Move);
    
    // After simulating, update the server state that will be replicated back to clients.
    UBoxComponent* PhysicsBody = GetPhysicsBody();
    if (!PhysicsBody) return;
    
    ServerState.Transform = PhysicsBody->GetComponentTransform();
    ServerState.LinearVelocity = PhysicsBody->GetPhysicsLinearVelocity();
    ServerState.AngularVelocity = PhysicsBody->GetPhysicsAngularVelocityInRadians();
    ServerState.LastMove = Move;
}

// Helper Getters
AReplicatedPodRacer* UPodMovementComponent::GetPodRacerOwner() const { return Cast<AReplicatedPodRacer>(PawnOwner); }
UBoxComponent* UPodMovementComponent::GetPhysicsBody() const { return GetPodRacerOwner() ? GetPodRacerOwner()->GetPhysicsBody() : nullptr; }
*/