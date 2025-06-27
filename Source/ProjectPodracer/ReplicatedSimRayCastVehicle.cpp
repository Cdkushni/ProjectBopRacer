// Fill out your copyright notice in the Description page of Project Settings.


#include "ReplicatedSimRayCastVehicle.h"
#include "RayCastVehicleMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/SpringArmComponent.h"


// Sets default values
AReplicatedSimRayCastVehicle::AReplicatedSimRayCastVehicle()
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false); // Disable default movement replication
	SetNetUpdateFrequency(60.f);
	SetMinNetUpdateFrequency(30.f);

	BoxCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("HullCollider"));
	RootComponent = BoxCollider;
	BoxCollider->SetBoxExtent(FVector(100, 52, 12));
	BoxCollider->SetSimulatePhysics(true);
	BoxCollider->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	BoxCollider->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	BoxCollider->SetLinearDamping(5.0f); // Increased for stability

	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(RootComponent);

	FLeftSuspensionRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FL_SuspensionRoot"));
	FLeftSuspensionRoot->SetupAttachment(Pivot);
	FLeftSuspensionRoot->SetRelativeLocation(FVector(100, -50, 0));

	FRightSuspensionRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FR_SuspensionRoot"));
	FRightSuspensionRoot->SetupAttachment(Pivot);
	FRightSuspensionRoot->SetRelativeLocation(FVector(100, 50, 0));

	RLeftSuspensionRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RL_SuspensionRoot"));
	RLeftSuspensionRoot->SetupAttachment(Pivot);
	RLeftSuspensionRoot->SetRelativeLocation(FVector(-100, -50, 0));

	RRightSuspensionRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RR_SuspensionRoot"));
	RRightSuspensionRoot->SetupAttachment(Pivot);
	RRightSuspensionRoot->SetRelativeLocation(FVector(-100, 50, 0));

	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
	HullMesh->SetupAttachment(Pivot);
	HullMesh->SetSimulatePhysics(false);
	HullMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	HullMesh->SetRelativeScale3D(FVector(2, 1, 0.25f));

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 600.0f;
	SpringArm->SocketOffset = FVector(0, 0, 100.f);
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraLagSpeed = 7.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

	MovementComponent = CreateDefaultSubobject<URayCastVehicleMovementComponent>(TEXT("MovementComponent"));

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

	static ConstructorHelpers::FObjectFinder<UInputAction> SteerActionFinder(TEXT("/Game/Input/IA_SteerAction.IA_SteerAction"));
	if (SteerActionFinder.Succeeded())
	{
		SteerAction = SteerActionFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UInputAction> DriftActionFinder(TEXT("/Game/Input/IA_DriftAction.IA_DriftAction"));
	if (DriftActionFinder.Succeeded())
	{
		DriftAction = DriftActionFinder.Object;
	}
}

// Called when the game starts or when spawned
void AReplicatedSimRayCastVehicle::BeginPlay()
{
	Super::BeginPlay();
	if (DefaultMappingContext)
	{
		if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}
}

// Called every frame
void AReplicatedSimRayCastVehicle::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// Called to bind functionality to input
void AReplicatedSimRayCastVehicle::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(AccelerateAction, ETriggerEvent::Triggered, this, &AReplicatedSimRayCastVehicle::Accelerate);
		EnhancedInputComponent->BindAction(SteerAction, ETriggerEvent::Triggered, this, &AReplicatedSimRayCastVehicle::Steer);
		EnhancedInputComponent->BindAction(DriftAction, ETriggerEvent::Started, this, &AReplicatedSimRayCastVehicle::StartDrift);
		EnhancedInputComponent->BindAction(DriftAction, ETriggerEvent::Canceled, this, &AReplicatedSimRayCastVehicle::StopDrift);
		EnhancedInputComponent->BindAction(DriftAction, ETriggerEvent::Completed, this, &AReplicatedSimRayCastVehicle::StopDrift);
	}
}

void AReplicatedSimRayCastVehicle::Accelerate(const FInputActionValue& Value) {
	if (MovementComponent && Controller)
	{
		float ThrottleValue = Value.Get<float>();
		MovementComponent->SetAccelerationInput(ThrottleValue);
	}
}

void AReplicatedSimRayCastVehicle::Steer(const FInputActionValue& Value) {
	if (MovementComponent && Controller)
	{
		float SteerValue = Value.Get<float>();
		MovementComponent->SetSteeringInput(SteerValue);
	}
}

void AReplicatedSimRayCastVehicle::StartDrift(const FInputActionValue& Value) { if (MovementComponent) { MovementComponent->StartDrift(); } }

void AReplicatedSimRayCastVehicle::StopDrift(const FInputActionValue& Value) { if (MovementComponent) { MovementComponent->StopDrift(); } }