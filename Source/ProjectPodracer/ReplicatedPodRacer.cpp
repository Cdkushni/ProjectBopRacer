#include "ReplicatedPodRacer.h"
#include "EnhancedInputComponent.h" // For Enhanced Input
#include "EngineComponent.h" // If you still need to interact with it directly
#include "EnhancedInputSubsystems.h"
#include "PodMovementComponent.h"
#include "PhysicsEngine/BodyInstance.h"

// Constructor
AReplicatedPodRacer::AReplicatedPodRacer()
{
    bReplicates = true;
    SetReplicateMovement(false); // We want the movement component to handle replication, not the actor
    NetPriority = 3.0f; // Higher priority for player-controlled pawns
    SetNetUpdateFrequency(60.0f);
    SetMinNetUpdateFrequency(20.0f);

    BoxCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("BoxCollider"));
    RootComponent = BoxCollider;
    BoxCollider->SetBoxExtent(FVector(100, 52, 12));
    BoxCollider->SetSimulatePhysics(true);
    BoxCollider->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
    BoxCollider->SetMassOverrideInKg(NAME_None, 100.f); // Make sure this matches movement component's Mass property
    BoxCollider->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    BoxCollider->SetLinearDamping(1);
    BoxCollider->SetAngularDamping(3);
    BoxCollider->SetEnableGravity(false);
    BoxCollider->SetGenerateOverlapEvents(false);
    BoxCollider->SetUseCCD(true);
    FBodyInstance* BodyInstance = BoxCollider->GetBodyInstance();
    if (BodyInstance)
    {
        BodyInstance->SetDOFLock(EDOFMode::SixDOF);
        BodyInstance->bLockXRotation = true;
        BodyInstance->bLockYRotation = true;
        BodyInstance->bLockZRotation = false;
    }

    static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> PhysMatFinder(TEXT("/Game/PM_HoverRacer.PM_HoverRacer"));
    if (PhysMatFinder.Succeeded())
    {
        BoxPhysicalMaterial = PhysMatFinder.Object;
        BoxCollider->SetPhysMaterialOverride(BoxPhysicalMaterial);
    }

    PodMovementComponent = CreateDefaultSubobject<UPodMovementComponent>(TEXT("PodMovementComponent"));
    PodMovementComponent->SetUpdatedComponent(BoxCollider);
    
    // Setup remaining components...
    HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
    HullMesh->SetupAttachment(BoxCollider);
    HullMesh->SetSimulatePhysics(false);
    HullMesh->SetEnableGravity(false);
    HullMesh->SetGenerateOverlapEvents(false);
    HullMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

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
}

void AReplicatedPodRacer::BeginPlay()
{
    Super::BeginPlay();
    if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(DefaultMappingContext, 0);
        }
    }

    if (true)
    {
        if (APlayerController* PC = Cast<APlayerController>(GetController()))
        {
            UE_LOG(LogTemp, Log, TEXT("Disconnect Log: Player %s NetworkRole=%d, RemoteRole=%d"),
                *GetName(), (int32)GetNetMode(), (int32)GetRemoteRole());
            UE_LOG(LogTemp, Log, TEXT("Disconnect Log: PodRacer BeginPlay: Pos=%s, Role=%d, RemoteRole=%d, Controller=%s"),
                *GetActorLocation().ToString(), (int32)GetLocalRole(), (int32)GetRemoteRole(), GetController() ? *GetController()->GetName() : TEXT("None"));
        }
    }

    if (PodMovementComponent)
    {
        PodMovementComponent->SetThrusterInput(0.f);
        PodMovementComponent->SetRudderInput(0.f);
        PodMovementComponent->SetIsBraking(false);
        PodMovementComponent->SetIsDrifting(false);
        PodMovementComponent->SetIsBoosting(false);
    }

    // Initialize two engines
    FDataTableRowHandle EngineStatsHandle;
    EngineStatsHandle.DataTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_EngineStats.DT_EngineStats"));
    EngineStatsHandle.RowName = FName("StandardEngine");
    AddEngine(EngineStatsHandle, FVector(100.0f, 50.0f, 25.0f));  // Left engine
    AddEngine(EngineStatsHandle, FVector(100.0f, -50.0f, 25.0f)); // Right engine
}

void AReplicatedPodRacer::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (true && GetNetMode() == NM_Client)
    {
        if (!GetWorld()->GetNetDriver() || !GetWorld()->GetNetDriver()->IsServer())
        {
            UE_LOG(LogTemp, Warning, TEXT("Client detected disconnection! Role=%d, RemoteRole=%d, Pos=%s"),
                (int32)GetLocalRole(), (int32)GetRemoteRole(), *GetActorLocation().ToString());
        }
    }
}

void AReplicatedPodRacer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        // --- ✅ FIXED: Added Completed/Canceled bindings to reset inputs to zero ---
        if (AccelerateAction)
        {
            EIC->BindAction(AccelerateAction, ETriggerEvent::Triggered, this, &AReplicatedPodRacer::Accelerate);
            EIC->BindAction(AccelerateAction, ETriggerEvent::Completed, this, &AReplicatedPodRacer::AccelerateCompleted);
            EIC->BindAction(AccelerateAction, ETriggerEvent::Canceled, this, &AReplicatedPodRacer::AccelerateCompleted);
        }
        if (SteerAction)
        {
            EIC->BindAction(SteerAction, ETriggerEvent::Triggered, this, &AReplicatedPodRacer::Steer);
            EIC->BindAction(SteerAction, ETriggerEvent::Completed, this, &AReplicatedPodRacer::SteerCompleted);
            EIC->BindAction(SteerAction, ETriggerEvent::Canceled, this, &AReplicatedPodRacer::SteerCompleted);
        }
        if (BreakAction)
        {
            EIC->BindAction(BreakAction, ETriggerEvent::Started, this, &AReplicatedPodRacer::Break);
            EIC->BindAction(BreakAction, ETriggerEvent::Completed, this, &AReplicatedPodRacer::BreakOff);
        }
        if (DriftAction)
        {
            EIC->BindAction(DriftAction, ETriggerEvent::Started, this, &AReplicatedPodRacer::Drift);
            EIC->BindAction(DriftAction, ETriggerEvent::Completed, this, &AReplicatedPodRacer::DriftOff);
        }
        if (BoostAction)
        {
            EIC->BindAction(BoostAction, ETriggerEvent::Started, this, &AReplicatedPodRacer::Boost);
            EIC->BindAction(BoostAction, ETriggerEvent::Completed, this, &AReplicatedPodRacer::BoostOff);
        }
    }
}

void AReplicatedPodRacer::TornOff()
{
    if (true)
    {
        UE_LOG(LogTemp, Warning, TEXT("Disconnect Log: Pawn %s TornOff, delaying destruction"), *GetName());
    }
    SetLifeSpan(15.0f); // Delay destruction by 5 seconds
}

void AReplicatedPodRacer::Destroyed()
{
    if (true)
    {
        UE_LOG(LogTemp, Warning, TEXT("Disconnect Log: PodRacer %s Destroyed"), *GetName());
    }
    Super::Destroyed();
}

// Keeping this function on the Pawn as it manages the creation of EngineComponents
void AReplicatedPodRacer::AddEngine(FDataTableRowHandle EngineStatsHandle, const FVector& Offset)
{
    if (!EngineStatsHandle.DataTable) return;
    FEngineStats* Stats = EngineStatsHandle.GetRow<FEngineStats>(TEXT("EngineStats"));
    if (!Stats) return;

    UEngineComponent* NewEngine = NewObject<UEngineComponent>(this, UEngineComponent::StaticClass(), *MakeNewEngineName());
    if (NewEngine)
    {
        NewEngine->RegisterComponent();
        NewEngine->AttachToComponent(BoxCollider, FAttachmentTransformRules::KeepRelativeTransform);
        NewEngine->Initialize(*Stats, Offset);
        Engines.Add(NewEngine);
    }
}

FString AReplicatedPodRacer::MakeNewEngineName() { return "Engine_" + FString::FromInt(EngineNameIndex++); }

// --- Input Callbacks: Pass data to Movement Component ---
void AReplicatedPodRacer::Accelerate(const FInputActionValue& Value) { if (PodMovementComponent) PodMovementComponent->SetThrusterInput(Value.Get<float>()); }
void AReplicatedPodRacer::AccelerateCompleted(const FInputActionValue& Value) { if (PodMovementComponent) PodMovementComponent->SetThrusterInput(0.f); }
void AReplicatedPodRacer::Steer(const FInputActionValue& Value) { if (PodMovementComponent) PodMovementComponent->SetRudderInput(Value.Get<float>()); }
void AReplicatedPodRacer::SteerCompleted(const FInputActionValue& Value) { if (PodMovementComponent) PodMovementComponent->SetRudderInput(0.f); }
void AReplicatedPodRacer::Break(const FInputActionValue& Value) { if (PodMovementComponent) PodMovementComponent->SetIsBraking(true); }
void AReplicatedPodRacer::BreakOff(const FInputActionValue& Value) { if (PodMovementComponent) PodMovementComponent->SetIsBraking(false); }
void AReplicatedPodRacer::Drift(const FInputActionValue& Value) { if (PodMovementComponent) PodMovementComponent->SetIsDrifting(true); }
void AReplicatedPodRacer::DriftOff(const FInputActionValue& Value) { if (PodMovementComponent) PodMovementComponent->SetIsDrifting(false); }
void AReplicatedPodRacer::Boost(const FInputActionValue& Value) { if (PodMovementComponent) PodMovementComponent->SetIsBoosting(true); }
void AReplicatedPodRacer::BoostOff(const FInputActionValue& Value) { if (PodMovementComponent) PodMovementComponent->SetIsBoosting(false); }