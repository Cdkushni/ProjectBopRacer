#include "NetworkedPodRacer.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "PodracerMovementComponent.h" // Include the actual header
#include "Components/InputComponent.h"

ANetworkedPodRacer::ANetworkedPodRacer()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true; // Pawn needs to replicate
    SetReplicateMovement(true); // Standard movement replication (can be fine-tuned by MovementComponent)

    CockpitMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CockpitMesh"));
    RootComponent = CockpitMesh; // Cockpit is the root that the movement component will move
    // CockpitMesh should NOT simulate physics if moved by MovementComponent kinematically
    CockpitMesh->SetSimulatePhysics(false); 
    CockpitMesh->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);


    EngineLeft_Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EngineLeft_Mesh"));
    EngineLeft_Mesh->SetupAttachment(CockpitMesh); // Rigidly attached
    EngineLeft_Mesh->SetSimulatePhysics(false);
    EngineLeft_Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // Visual only or simple query

    EngineRight_Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EngineRight_Mesh"));
    EngineRight_Mesh->SetupAttachment(CockpitMesh); // Rigidly attached
    EngineRight_Mesh->SetSimulatePhysics(false);
    EngineRight_Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // Position engines relative to cockpit
    EngineLeft_Mesh->SetRelativeLocation(FVector(250.f, -150.f, 0.f));
    EngineRight_Mesh->SetRelativeLocation(FVector(250.f, 150.f, 0.f));

    // Movement Component
    PodMovementComponent = CreateDefaultSubobject<UPodracerMovementComponent>(TEXT("PodMovementComponent"));
    PodMovementComponent->UpdatedComponent = RootComponent; // Tell movement component what to move

    // Camera
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(RootComponent);
    SpringArm->TargetArmLength = 900.0f;
    SpringArm->bUsePawnControlRotation = false;
    SpringArm->bEnableCameraLag = true;
    SpringArm->CameraLagSpeed = 7.f;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
}

void ANetworkedPodRacer::BeginPlay()
{
    Super::BeginPlay();
}

void ANetworkedPodRacer::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    // Movement is handled by PodMovementComponent's tick
}

void ANetworkedPodRacer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    PlayerInputComponent->BindAxis("MoveForward", this, &ANetworkedPodRacer::MoveForward);
    PlayerInputComponent->BindAxis("TurnRight", this, &ANetworkedPodRacer::TurnRight);
}

void ANetworkedPodRacer::MoveForward(float Value)
{
    if (PodMovementComponent)
    {
        PodMovementComponent->SetThrottleInput(Value);
    }
}

void ANetworkedPodRacer::TurnRight(float Value)
{
    if (PodMovementComponent)
    {
        PodMovementComponent->SetSteeringInput(Value);
    }
}