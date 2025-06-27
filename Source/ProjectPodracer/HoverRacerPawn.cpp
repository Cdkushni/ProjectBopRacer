// HoverRacerPawn.cpp

#include "HoverRacerPawn.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "PhysicsEngine/BodyInstance.h" // Required for FBodyInstance

AHoverRacerPawn::AHoverRacerPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
    RootComponent = HullMesh;
    HullMesh->SetSimulatePhysics(true);
    HullMesh->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);

    // Default PID parameters
    HoverPidKp = 600.0f; // Proportional: Adjust for responsiveness
    HoverPidKi = 50.0f;   // Integral: Adjust to eliminate steady-state error (e.g., slight sinking)
    HoverPidKd = 150.0f;  // Derivative: Adjust to dampen oscillations

    TargetHoverHeight = 150.0f;
    HoverTraceLength = TargetHoverHeight * 2.0f; // Trace a bit further than target height

    ForwardAcceleration = 70000.0f;
    BackwardAcceleration = 35000.0f;
    StrafeAcceleration = 40000.0f;
    TurnStrength = 700000.0f;
    BoostMultiplier = 2.0f;
    MaxSpeed = 8000.0f; // cm/s

    DriftReductionFactor = 50.0f;
    TurnAssistFactor = 30.0f;

    LinearDamping = 0.3f; // Lowered for more active control from forces
    AngularDamping = 0.5f;

    CurrentForwardInput = 0.0f;
    CurrentTurnInput = 0.0f;
    CurrentStrafeInput = 0.0f;
    bIsBoosting = false;
    bIsGrounded = false;

    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(RootComponent);
    SpringArm->TargetArmLength = 1000.0f;
    SpringArm->bEnableCameraLag = true;
    SpringArm->CameraLagSpeed = 7.0f;
    SpringArm->bUsePawnControlRotation = false;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
    Camera->bUsePawnControlRotation = false;
}

void AHoverRacerPawn::BeginPlay()
{
    Super::BeginPlay();

    if (HullMesh && HullMesh->IsSimulatingPhysics())
    {
        HullMesh->SetLinearDamping(LinearDamping);
        HullMesh->SetAngularDamping(AngularDamping);
        // HullMesh->SetMassOverrideInKg(NAME_None, 500.f, true); // Example mass

        // Enable Continuous Collision Detection (CCD)
        HullMesh->SetUseCCD(true);

        // Lock X (Roll) and Y (Pitch) rotations from physics simulation
        // Yaw (Z) remains free for player turning torque
        FBodyInstance* BodyInst = HullMesh->GetBodyInstance();
        if (BodyInst)
        {
            BodyInst->bLockXRotation = true;
            BodyInst->bLockYRotation = true;
            BodyInst->bLockZRotation = false; 
        }
    }

    // Initialize PID states for each hover point
    HoverPointPIDStates.SetNum(HoverPoints.Num());
    for (FPIDControllerState& PIDState : HoverPointPIDStates)
    {
        PIDState.Reset();
    }
}

void AHoverRacerPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (HullMesh && HullMesh->IsSimulatingPhysics())
    {
        ApplyHover(DeltaTime);
        ApplyMovement(DeltaTime);

        // Clamp overall speed
        FVector CurrentVelocity = HullMesh->GetPhysicsLinearVelocity();
        if (CurrentVelocity.SizeSquared() > FMath::Square(MaxSpeed))
        {
            FVector ClampedVelocity = CurrentVelocity.GetSafeNormal() * MaxSpeed;
            HullMesh->SetPhysicsLinearVelocity(ClampedVelocity);
        }
    }
}

void AHoverRacerPawn::ApplyHover(float DeltaTime)
{
    if (HoverPoints.Num() == 0 || HoverPoints.Num() != HoverPointPIDStates.Num())
    {
        return;
    }

    bool bAnyPointHitGroundThisFrame = false;

    for (int32 i = 0; i < HoverPoints.Num(); ++i)
    {
        USceneComponent* HoverPoint = HoverPoints[i];
        FPIDControllerState& PIDState = HoverPointPIDStates[i];

        if (!HoverPoint) continue;

        FVector StartLocation = HoverPoint->GetComponentLocation();
        FVector EndLocation = StartLocation - (HoverPoint->GetUpVector() * HoverTraceLength);
        
        FHitResult HitResult;
        FCollisionQueryParams CollisionParams;
        CollisionParams.AddIgnoredActor(this);

        bool bHit = GetWorld()->LineTraceSingleByChannel(
            HitResult, StartLocation, EndLocation, ECC_Visibility, CollisionParams
        );
        // DrawDebugLine(GetWorld(), StartLocation, EndLocation, bHit ? FColor::Green : FColor::Red, false, -1, 0, 1.0f);

        if (bHit)
        {
            bAnyPointHitGroundThisFrame = true;
            float CurrentHeight = HitResult.Distance;
            float Error = TargetHoverHeight - CurrentHeight;

            // Integral term (with anti-windup, though not explicitly shown here, Ki should be tuned)
            PIDState.IntegralTerm += Error * DeltaTime;
            PIDState.IntegralTerm = FMath::Clamp(PIDState.IntegralTerm, -200.0f, 200.0f); // Clamp integral term

            // Derivative term
            float DerivativeTerm = 0.0f;
            if (DeltaTime > KINDA_SMALL_NUMBER) // Avoid division by zero
            {
                DerivativeTerm = (Error - PIDState.PreviousError) / DeltaTime;
            }
            
            // PID Output (Force Magnitude)
            float OutputForce = (HoverPidKp * Error) + (HoverPidKi * PIDState.IntegralTerm) + (HoverPidKd * DerivativeTerm);
            OutputForce = FMath::Max(0.f, OutputForce); // Only apply upward force

            FVector ForceDirection = HoverPoint->GetUpVector();
            HullMesh->AddForceAtLocation(ForceDirection * OutputForce, StartLocation);

            PIDState.PreviousError = Error;
        }
        else
        {
            // Reset PID if not hitting ground to prevent integral windup when in air for too long
            PIDState.Reset(); 
            PIDState.PreviousError = TargetHoverHeight; // Assume max error if in air
        }
    }
    UpdateGroundedState(bAnyPointHitGroundThisFrame);
}

void AHoverRacerPawn::UpdateGroundedState(bool bAnyPointHitGround)
{
    bIsGrounded = bAnyPointHitGround;
    // You can use bIsGrounded here to modify other physics properties if needed
    // For example, increase drag or change turn behavior when not grounded.
}

void AHoverRacerPawn::ApplyMovement(float DeltaTime)
{
    // --- Forward/Backward Thrust ---
    if (!FMath::IsNearlyZero(CurrentForwardInput))
    {
        float CurrentMaxAccel = CurrentForwardInput > 0 ? ForwardAcceleration : BackwardAcceleration;
        if (bIsBoosting && CurrentForwardInput > 0)
        {
            CurrentMaxAccel *= BoostMultiplier;
        }
        FVector ForceToApply = HullMesh->GetForwardVector() * CurrentForwardInput * CurrentMaxAccel;
        HullMesh->AddForce(ForceToApply);
    }

    // --- Strafe Thrust ---
    if (!FMath::IsNearlyZero(CurrentStrafeInput))
    {
        FVector StrafeForce = HullMesh->GetRightVector() * CurrentStrafeInput * StrafeAcceleration;
        HullMesh->AddForce(StrafeForce);
    }

    // --- Turning Torque ---
    if (!FMath::IsNearlyZero(CurrentTurnInput))
    {
        FVector TorqueToApply = HullMesh->GetUpVector() * CurrentTurnInput * TurnStrength;
        HullMesh->AddTorqueInRadians(TorqueToApply);
    }

    // --- Advanced Movement Physics (Inspired by VehicleMovement.cs) ---
    FVector LocalVelocity = HullMesh->GetComponentTransform().InverseTransformVector(HullMesh->GetPhysicsLinearVelocity());

    // Drift Reduction (Counter Sideways Velocity)
    float SidewaysSpeed = LocalVelocity.Y;
    FVector DriftReductionForceVector = HullMesh->GetRightVector() * -SidewaysSpeed * DriftReductionFactor * HullMesh->GetMass(); // Scale by mass
    HullMesh->AddForce(DriftReductionForceVector);

    // Turn Assistance (Push into the turn)
    if (!FMath::IsNearlyZero(CurrentTurnInput) && !FMath::IsNearlyZero(LocalVelocity.X)) // Only if moving forward/backward and turning
    {
        FVector TurnAssistForceVector = HullMesh->GetRightVector() * CurrentTurnInput * FMath::Sign(LocalVelocity.X) * TurnAssistFactor * HullMesh->GetMass();
        HullMesh->AddForce(TurnAssistForceVector);
    }
}

void AHoverRacerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    PlayerInputComponent->BindAxis("MoveForward_Hover", this, &AHoverRacerPawn::MoveForwardInput);
    PlayerInputComponent->BindAxis("Turn_Hover", this, &AHoverRacerPawn::TurnInput);
    PlayerInputComponent->BindAxis("Strafe_Hover", this, &AHoverRacerPawn::StrafeInput);

    PlayerInputComponent->BindAction("Boost_Hover", IE_Pressed, this, &AHoverRacerPawn::StartBoosting);
    PlayerInputComponent->BindAction("Boost_Hover", IE_Released, this, &AHoverRacerPawn::StopBoosting);
}

void AHoverRacerPawn::MoveForwardInput(float Value)
{
    CurrentForwardInput = Value;
}

void AHoverRacerPawn::TurnInput(float Value)
{
    CurrentTurnInput = Value;
}

void AHoverRacerPawn::StrafeInput(float Value)
{
    CurrentStrafeInput = Value;
}

void AHoverRacerPawn::StartBoosting()
{
    bIsBoosting = true;
}

void AHoverRacerPawn::StopBoosting()
{
    bIsBoosting = false;
}