#include "EngineControllerPodRacer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "DrawDebugHelpers.h"
#include "PhysicsEngine/BodyInstance.h"

AEngineControllerPodRacer::AEngineControllerPodRacer()
{
    PrimaryActorTick.bCanEverTick = true;
    SetActorTickEnabled(true);

    BoxCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("HullCollider"));
    RootComponent = BoxCollider;
    BoxCollider->SetBoxExtent(FVector(100, 52, 12));
    BoxCollider->SetSimulatePhysics(true);
    BoxCollider->SetMassOverrideInKg(NAME_None, Mass);
    BoxCollider->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
    BoxCollider->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    BoxCollider->SetLinearDamping(LinearDamping);
    BoxCollider->SetAngularDamping(AngularDamping);
    BoxCollider->SetEnableGravity(false);
    BoxCollider->SetGenerateOverlapEvents(false);
    BoxCollider->SetUseCCD(true);
    FBodyInstance* BodyInstance = BoxCollider->GetBodyInstance();
    if (BodyInstance)
    {
        BodyInstance->SetDOFLock(EDOFMode::SixDOF);
        BodyInstance->bLockXRotation = true;
        BodyInstance->bLockYRotation = false;
        BodyInstance->bLockZRotation = true;
    }

    static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> PhysMatFinder(TEXT("/Game/PM_HoverRacer.PM_HoverRacer"));
    if (PhysMatFinder.Succeeded())
    {
        BoxPhysicalMaterial = PhysMatFinder.Object;
        BoxCollider->SetPhysMaterialOverride(BoxPhysicalMaterial);
    }

    PodSpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("PodSpringArm"));
    PodSpringArm->SetupAttachment(BoxCollider);
    PodSpringArm->TargetArmLength = 300.0f;
    PodSpringArm->SocketOffset = FVector(0, 0, 0.f);
    PodSpringArm->bUsePawnControlRotation = false;
    PodSpringArm->bEnableCameraLag = true;
    PodSpringArm->bEnableCameraRotationLag = true;
    PodSpringArm->CameraLagSpeed = 15.f;
    PodSpringArm->CameraRotationLagSpeed = 12.f;

    HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
    HullMesh->SetupAttachment(PodSpringArm);
    HullMesh->SetSimulatePhysics(false);
    HullMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    HullMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f)); // Cockpit offset

    EngineConnectorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("EngineConnector"));
    EngineConnectorRoot->SetupAttachment(BoxCollider);

    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(RootComponent);
    SpringArm->TargetArmLength = 600.0f;
    SpringArm->SocketOffset = FVector(0, 0, 100.f);
    SpringArm->bUsePawnControlRotation = false;
    SpringArm->bEnableCameraLag = true;
    SpringArm->bEnableCameraRotationLag = true;
    SpringArm->CameraLagSpeed = 15.f;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

    static ConstructorHelpers::FObjectFinder<UInputMappingContext> MappingContextFinder(TEXT("/Game/Input/IMC_SimVehicle.IMC_SimVehicle"));
    if (MappingContextFinder.Succeeded())
    {
        DefaultMappingContext = MappingContextFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UInputAction> AccelActionFinder(TEXT("/Game/Input/IA_AccelerateAction.IA_AccelerateAction"));
    if (AccelActionFinder.Succeeded())
    {
        AccelerateAction = AccelActionFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UInputAction> SteerActionFinder(TEXT("/Game/Input/IA_RudderAction.IA_RudderAction"));
    if (SteerActionFinder.Succeeded())
    {
        SteerAction = SteerActionFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UInputAction> BreakActionFinder(TEXT("/Game/Input/IA_BrakeAction.IA_BrakeAction"));
    if (BreakActionFinder.Succeeded())
    {
        BreakAction = BreakActionFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UInputAction> DriftActionFinder(TEXT("/Game/Input/IA_DriftAction.IA_DriftAction"));
    if (DriftActionFinder.Succeeded())
    {
        DriftAction = DriftActionFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UInputAction> BoostActionFinder(TEXT("/Game/Input/IA_BoostAction.IA_BoostAction"));
    if (BoostActionFinder.Succeeded())
    {
        BoostAction = BoostActionFinder.Object;
    }

    CurrentSpeed = 0.0f;
    ThrusterInput = 0.0f;
    RudderInput = 0.0f;
    bIsBraking = false;
    bIsOnGround = false;
    bIsDrifting = false;
    bIsBoosting = false;
    Drag = 0.0f;
    AccelerationInput = 0.0f;

    SmoothedRudderInput = 0.0f;

    // PID Config
    //HoverPID.PCoeff = 0.5f; // Increased for responsiveness
    //HoverPID.ICoeff = 0.001f; // Increased for faster steady-state
    //HoverPID.DCoeff = 1.0f;
}

float AEngineControllerPodRacer::GetSpeedPercentage() const
{
    return BoxCollider ? BoxCollider->GetPhysicsLinearVelocity().Size() / TerminalVelocity : 0.0f;
}

void AEngineControllerPodRacer::BeginPlay()
{
    Super::BeginPlay();

    if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(DefaultMappingContext, 0);
        }
    }

    if (BoxCollider)
    {
        BoxCollider->OnComponentHit.AddDynamic(this, &AEngineControllerPodRacer::OnComponentHit);
    }

    // Initialize two engines
    FDataTableRowHandle EngineStatsHandle;
    EngineStatsHandle.DataTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_EngineStats.DT_EngineStats"));
    EngineStatsHandle.RowName = FName("StandardEngine");
    AddEngine(EngineStatsHandle, FVector(100.0f, 50.0f, -25.0f));  // Left engine
    AddEngine(EngineStatsHandle, FVector(100.0f, -50.0f, -25.0f)); // Right engine
}

void AEngineControllerPodRacer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        EnhancedInputComponent->BindAction(AccelerateAction, ETriggerEvent::Triggered, this, &AEngineControllerPodRacer::Accelerate);
        EnhancedInputComponent->BindAction(SteerAction, ETriggerEvent::Triggered, this, &AEngineControllerPodRacer::Steer);
        EnhancedInputComponent->BindAction(BreakAction, ETriggerEvent::Triggered, this, &AEngineControllerPodRacer::Break);
        EnhancedInputComponent->BindAction(BreakAction, ETriggerEvent::Completed, this, &AEngineControllerPodRacer::BreakOff);
        EnhancedInputComponent->BindAction(BreakAction, ETriggerEvent::Canceled, this, &AEngineControllerPodRacer::BreakOff);
        EnhancedInputComponent->BindAction(DriftAction, ETriggerEvent::Triggered, this, &AEngineControllerPodRacer::Drift);
        EnhancedInputComponent->BindAction(DriftAction, ETriggerEvent::Completed, this, &AEngineControllerPodRacer::DriftOff);
        EnhancedInputComponent->BindAction(DriftAction, ETriggerEvent::Canceled, this, &AEngineControllerPodRacer::DriftOff);
        EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Triggered, this, &AEngineControllerPodRacer::Boost);
        EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Completed, this, &AEngineControllerPodRacer::BoostOff);
        EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Canceled, this, &AEngineControllerPodRacer::BoostOff);
    }
}

void AEngineControllerPodRacer::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (BoxCollider && BoxCollider->IsSimulatingPhysics())
    {
        CurrentSpeed = FVector::DotProduct(BoxCollider->GetPhysicsLinearVelocity(), GetActorForwardVector());
    }

    for (UEngineComponent* Engine : Engines)
    {
        if (Engine->GetState() == EEngineState::Repairing)
        {
            Engine->RepairEngine(DeltaTime);
        }
    }

    CalculateHover(DeltaTime);
    
    // ====================================================================
    // ✅ NEW STEERING AND INPUT SMOOTHING LOGIC ADDED HERE
    // ====================================================================

    // 1. Smooth the rudder input for keyboard friendliness
    float InterpSpeed = (FMath::IsNearlyZero(RudderInput)) ? KeyboardSteeringReturnSpeed : KeyboardSteeringInterpSpeed;
    SmoothedRudderInput = FMath::FInterpTo(SmoothedRudderInput, RudderInput, DeltaTime, InterpSpeed);

    // ====================================================================
    // END OF NEW LOGIC
    // ====================================================================
    CalculatePropulsion(DeltaTime);
}

void AEngineControllerPodRacer::CalculateHover(float DeltaTime)
{
    FVector GroundNormal = FVector::UpVector;
    bIsOnGround = false;
    float Height = MaxGroundDist;

    FVector Start = BoxCollider->GetComponentLocation();
    FVector End = Start - GetActorUpVector() * MaxGroundDist;
    FHitResult HitResult;
    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(this);

    if (GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, GroundCollisionChannel, QueryParams))
    {
        bIsOnGround = true;
        Height = HitResult.Distance;
        GroundNormal = HitResult.Normal.GetSafeNormal();
    }

    // Reset PID on ground transition
    if (bIsOnGround && !bWasOnGroundLastFrame)
    {
        HoverPID.Reset();
    }
    bWasOnGroundLastFrame = bIsOnGround;

    if (bDrawDebug)
    {
        DrawDebugLine(GetWorld(), Start, End, bIsOnGround ? FColor::Green : FColor::Red, false, 0.0f, 0, 1.0f);
        if (bIsOnGround)
        {
            DrawDebugSphere(GetWorld(), Start - GetActorUpVector() * HoverHeight, 10.0f, 12, FColor::Blue, false, 0.0f);
            DrawDebugString(GetWorld(), BoxCollider->GetComponentLocation(), FString::Printf(TEXT("Height: %.1f"), Height), nullptr, FColor::White, 0.0f);
            for (UEngineComponent* Engine : Engines)
            {
                DrawDebugString(GetWorld(), Engine->GetComponentLocation(), FString::Printf(TEXT("Health: %.1f"), Engine->GetHealth()), nullptr, FColor::Yellow, 0.0f);
            }
        }
    }

    if (bIsOnGround)
    {
        float ForcePercent = HoverPID.Seek(HoverHeight, Height, DeltaTime);
        if (Height > HoverHeight)
        {
            ForcePercent *= 0.5f; // Weaken upward force for descent
        }
        for (UEngineComponent* Engine : Engines)
        {
            float EngineHoverForce = Engine->GetHoverForce(ForcePercent);
            FVector Force = GroundNormal * EngineHoverForce / Mass;
            BoxCollider->AddForceAtLocation(Force * Mass, Engine->GetForceApplicationPoint());

            if (bDrawDebug)
            {
                FVector ForceEnd = Engine->GetForceApplicationPoint() + Force.GetSafeNormal() * DebugArrowLength * ForcePercent;
                UKismetSystemLibrary::DrawDebugArrow(GetWorld(), Engine->GetForceApplicationPoint(), ForceEnd, DebugArrowSize, FColor::Cyan, 0, 5.0f);
            }
        }
        FVector Gravity = -GroundNormal * HoverGravity;
        BoxCollider->AddForce(Gravity * Mass);

        if (bDrawDebug)
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
        BoxCollider->AddForce(Gravity * Mass);
        if (bDrawDebug)
        {
            FVector CurrentVelocity = BoxCollider->GetPhysicsLinearVelocity();
            UE_LOG(LogTemp, Log, TEXT("Airborne Velocity: X=%.1f, Y=%.1f, Z=%.1f"), CurrentVelocity.X, CurrentVelocity.Y, CurrentVelocity.Z);
        }
    }

    FVector Projection = UKismetMathLibrary::ProjectVectorOnToPlane(GetActorForwardVector(), GroundNormal);
    FRotator TargetRotation = UKismetMathLibrary::MakeRotFromZX(GroundNormal, Projection);
    FRotator NewRotation = FMath::RInterpTo(GetActorRotation(), TargetRotation, DeltaTime, 5.0f);
    BoxCollider->SetWorldRotation(NewRotation);

    RudderInput = FMath::FInterpTo(RudderInput, 0.0f, DeltaTime, 20.0f);
    float RollAngle = AngleOfRoll * -RudderInput;
    //FQuat BodyRotation = GetActorRotation().Quaternion() * FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(RollAngle));
    FRotator LookAtRotation = UKismetMathLibrary::FindLookAtRotation(HullMesh->GetComponentLocation(), EngineConnectorRoot->GetComponentLocation());
    FQuat CurrentBodyRotation = HullMesh->GetComponentQuat();
    FQuat EngineRotation = GetActorRotation().Quaternion() * FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(RollAngle));
    FQuat CurrentEngineRotation = EngineConnectorRoot->GetComponentQuat();
    //HullMesh->SetWorldRotation(FMath::QInterpTo(CurrentBodyRotation, BodyRotation, DeltaTime, 5.0f));
    HullMesh->SetWorldRotation(FMath::QInterpTo(CurrentBodyRotation, LookAtRotation.Quaternion(), DeltaTime, 5.0f));
    EngineConnectorRoot->SetWorldRotation(FMath::QInterpTo(CurrentEngineRotation, EngineRotation, DeltaTime, 5.0f));
}

void AEngineControllerPodRacer::CalculatePropulsion(float DeltaTime)
{
    float TotalThrust = 0.0f;
    for (UEngineComponent* Engine : Engines)
    {
        TotalThrust += Engine->GetThrustForce(1.0f, bIsBoosting, bIsDrifting, DriftMultiplier, BoostMultiplier);
    }
    Drag = TotalThrust / TerminalVelocity;

    float SidewaysSpeed = FVector::DotProduct(BoxCollider->GetPhysicsLinearVelocity(), BoxCollider->GetRightVector());
    // ✅ REVISED SideFriction calculation
    // This applies a stable corrective force to reduce drift, scaled by your new grip factor and the vehicle's mass.
    FVector SideFriction = -BoxCollider->GetRightVector() * SidewaysSpeed * SidewaysGripFactor * Mass;//(SidewaysSpeed / DeltaTime);
    BoxCollider->AddForce(SideFriction);

    if (ThrusterInput <= 0.0f)
    {
        FVector CurrentVelocity = BoxCollider->GetPhysicsLinearVelocity();
        BoxCollider->SetPhysicsLinearVelocity(CurrentVelocity * SlowingVelFactor);
    }

    if (bIsBraking)
    {
        FVector CurrentVelocity = BoxCollider->GetPhysicsLinearVelocity();
        BoxCollider->SetPhysicsLinearVelocity(CurrentVelocity * BrakingVelFactor);
    }

    float AirborneThrustScale = bIsOnGround ? 1.0f : 0.5f; // 50% thrust when airborne
    for (UEngineComponent* Engine : Engines)
    {
        float EngineThrust = Engine->GetThrustForce(ThrusterInput, bIsBoosting, bIsDrifting, DriftMultiplier, BoostMultiplier) * AirborneThrustScale;
        FVector Force = GetActorForwardVector() * EngineThrust;
        BoxCollider->AddForceAtLocation(Force, Engine->GetForceApplicationPoint());

        if (bDrawDebug)
        {
            FVector ForceEnd = Engine->GetForceApplicationPoint() + Force.GetSafeNormal() * DebugArrowLength * ThrusterInput;
            UKismetSystemLibrary::DrawDebugArrow(GetWorld(), Engine->GetForceApplicationPoint(), ForceEnd, DebugArrowSize, FColor::Magenta, 0, 5.0f);
        }
    }
}

void AEngineControllerPodRacer::Accelerate(const FInputActionValue& Value)
{
    ThrusterInput = Value.Get<float>();
}

void AEngineControllerPodRacer::Steer(const FInputActionValue& Value)
{
    RudderInput = Value.Get<float>();
    // 2. Implement Speed-based agility (from Suggestion 3)
    float ForwardSpeed = FVector::DotProduct(BoxCollider->GetPhysicsLinearVelocity(), GetActorForwardVector());
    float SpeedMultiplier = FMath::GetMappedRangeValueClamped(
        FVector2D(0.0f, TerminalVelocity),
        FVector2D(1.0f, HighSpeedSteeringDampFactor),
        ForwardSpeed
    );

    // 3. Define the target angular velocity using the SMOOTHED input
    float TargetAngularVelocity = SmoothedRudderInput * MaxTurnRate * SpeedMultiplier;
    if (bIsDrifting)
    {
        TargetAngularVelocity *= DriftMultiplier;
    }

    // 4. Apply torque using the P-Controller logic (from Suggestion 2)
    float CurrentAngularVelocity = BoxCollider->GetPhysicsAngularVelocityInRadians().Z;
    float Error = TargetAngularVelocity - CurrentAngularVelocity;
    // NOTE: Your original code had "SteeringMultiplier" here. We are using the renamed "SteeringStiffness"
    float RotationTorque = Error * SteeringMultiplier; 
    BoxCollider->AddTorqueInDegrees(FVector(0, 0, RotationTorque * 1000.0f), NAME_None, true);
    /*
    float CurrentYawVelocity = BoxCollider->GetPhysicsAngularVelocityInRadians().Z;
    float DriftValue = bIsDrifting ? DriftMultiplier : 1.0f;
    float RotationTorque = (RudderInput * DriftValue) - CurrentYawVelocity;
    BoxCollider->AddTorqueInDegrees(FVector(0, 0, RotationTorque * SteeringStiffness * 1000.0f), NAME_None, true);
    */

    /*
    // ✅ ADDED: Speed-based agility
    // Get forward speed, not just total speed
    float ForwardSpeed = FVector::DotProduct(BoxCollider->GetPhysicsLinearVelocity(), GetActorForwardVector());
    // Map the current forward speed to a steering multiplier.
    // At 0 speed, multiplier is 1.0. At TerminalVelocity, it's HighSpeedSteeringDampFactor.
    float SpeedMultiplier = FMath::GetMappedRangeValueClamped(
        FVector2D(0.0f, TerminalVelocity),         // Input range (Speed)
        FVector2D(1.0f, HighSpeedSteeringDampFactor), // Output range (Multiplier)
        ForwardSpeed
    );

    // ✅ REVISED Steering Logic
    // 1. Define the target angular velocity based on input and max turn rate
    float TargetAngularVelocity = RudderInput * MaxTurnRate * SpeedMultiplier; // Apply the speed multiplier here
    if (bIsDrifting)
    {
        TargetAngularVelocity *= DriftMultiplier;
    }

    // 2. Get the current angular velocity
    float CurrentAngularVelocity = BoxCollider->GetPhysicsAngularVelocityInRadians().Z;

    // 3. Calculate the error (how far we are from the target)
    float Error = TargetAngularVelocity - CurrentAngularVelocity;

    // 4. Apply torque based on the error and stiffness. This is a classic P-controller.
    // The * 1000.0f and `true` for AccelChange are kept from your original code.
    float RotationTorque = Error * SteeringMultiplier; // Using the renamed property
    BoxCollider->AddTorqueInDegrees(FVector(0, 0, RotationTorque * 1000.0f), NAME_None, true);
    */
}

void AEngineControllerPodRacer::Break()
{
    bIsBraking = true;
}

void AEngineControllerPodRacer::BreakOff()
{
    bIsBraking = false;
}

void AEngineControllerPodRacer::Drift()
{
    bIsDrifting = true;
}

void AEngineControllerPodRacer::DriftOff()
{
    bIsDrifting = false;
}

void AEngineControllerPodRacer::Boost()
{
    bIsBoosting = true;
}

void AEngineControllerPodRacer::BoostOff()
{
    bIsBoosting = false;
}

void AEngineControllerPodRacer::OnComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
    if (OtherActor)
    {
        if (HitComponent->GetCollisionObjectType() == ECC_WorldDynamic)
        {
            FVector UpwardImpulse = FVector::DotProduct(NormalImpulse, GetActorUpVector()) * GetActorUpVector();
            BoxCollider->AddImpulse(-UpwardImpulse);

            if (Engines.Num() > 0)
            {
                UEngineComponent* NearestEngine = nullptr;
                float MinDistance = MAX_FLT;
                FVector HitLocation = Hit.Location;

                for (UEngineComponent* Engine : Engines)
                {
                    float Distance = FVector::Dist(HitLocation, Engine->GetForceApplicationPoint());
                    if (Distance < MinDistance)
                    {
                        MinDistance = Distance;
                        NearestEngine = Engine;
                    }
                }

                if (NearestEngine)
                {
                    NearestEngine->DamageEngine(20.0f);
                }
            }
        }

        if (bDrawDebug)
        {
            FVector Start = Hit.Location;
            FVector End = Start + Hit.Normal * DebugArrowLength;
            UKismetSystemLibrary::DrawDebugArrow(GetWorld(), Start, End, DebugArrowSize, FColor::Yellow, 10, 5.0f);
        }
    }
}

void AEngineControllerPodRacer::AddEngine(FDataTableRowHandle EngineStatsHandle, const FVector& Offset)
{
    if (!EngineStatsHandle.DataTable)
        return;

    FEngineStats* Stats = EngineStatsHandle.GetRow<FEngineStats>(TEXT("EngineStats"));
    if (!Stats)
        return;

    UEngineComponent* NewEngine = NewObject<UEngineComponent>(this, UEngineComponent::StaticClass(), *MakeNewEngineName());
    NewEngine->RegisterComponent();
    NewEngine->AttachToComponent(BoxCollider, FAttachmentTransformRules::KeepRelativeTransform);
    NewEngine->Initialize(*Stats, Offset);
    Engines.Add(NewEngine);

    NewEngine->OnEngineStateChanged.AddDynamic(this, &AEngineControllerPodRacer::HandleEngineStateChanged);
}

void AEngineControllerPodRacer::RemoveEngine(int32 EngineIndex)
{
    if (Engines.IsValidIndex(EngineIndex))
    {
        UEngineComponent* Engine = Engines[EngineIndex];
        //Engine->OnEngineStateChanged.RemoveAll(this);
        Engine->OnEngineStateChanged.RemoveDynamic(this, &AEngineControllerPodRacer::HandleEngineStateChanged);
        Engine->DestroyComponent();
        Engines.RemoveAt(EngineIndex);
    }
}

void AEngineControllerPodRacer::DamageEngine(int32 EngineIndex, float DamageAmount)
{
    if (Engines.IsValidIndex(EngineIndex))
    {
        Engines[EngineIndex]->DamageEngine(DamageAmount);
    }
}

void AEngineControllerPodRacer::RepairEngine(int32 EngineIndex)
{
    if (Engines.IsValidIndex(EngineIndex))
    {
        Engines[EngineIndex]->RepairEngine(GetWorld()->GetDeltaSeconds());
    }
}

void AEngineControllerPodRacer::BoostEngine(int32 EngineIndex, float Duration)
{
    if (Engines.IsValidIndex(EngineIndex))
    {
        Engines[EngineIndex]->BoostEngine(Duration);
    }
}

void AEngineControllerPodRacer::DisableEngine(int32 EngineIndex)
{
    if (Engines.IsValidIndex(EngineIndex))
    {
        Engines[EngineIndex]->DisableEngine();
    }
}

void AEngineControllerPodRacer::EnableEngine(int32 EngineIndex)
{
    if (Engines.IsValidIndex(EngineIndex))
    {
        Engines[EngineIndex]->EnableEngine();
    }
}

void AEngineControllerPodRacer::HandleEngineStateChanged(UEngineComponent* Engine)
{
    if (bDrawDebug)
    {
        UE_LOG(LogTemp, Log, TEXT("Engine %s State: %s, Health: %f"), *Engine->GetName(), *UEnum::GetValueAsString(Engine->GetState()), Engine->GetHealth());
    }
}

FString AEngineControllerPodRacer::MakeNewEngineName()
{
    FString NewEngineName = "Engine_" + FString::FromInt(EngineNameIndex++);
    return NewEngineName;
}
