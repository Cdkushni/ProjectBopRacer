// Fill out your copyright notice in the Description page of Project Settings.


#include "SimulatedRayCastVehicle.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"


// Sets default values
ASimulatedRayCastVehicle::ASimulatedRayCastVehicle()
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true; // Pawn needs to replicate
	SetReplicateMovement(true); // Standard movement replication (can be fine-tuned by MovementComponent)

	BoxCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("HullCollider"));
	RootComponent = BoxCollider;
	BoxCollider->SetBoxExtent(FVector(100, 52, 12));
	BoxCollider->SetSimulatePhysics(true);
	BoxCollider->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	BoxCollider->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	BoxCollider->SetLinearDamping(3.0f);

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
	HullMesh->SetupAttachment(Pivot); // Cockpit is the root that the movement component will move
	// CockpitMesh should NOT simulate physics if moved by MovementComponent kinematically
	HullMesh->SetSimulatePhysics(false); 
	HullMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	HullMesh->SetRelativeScale3D(FVector(2, 1, 0.25f));
	
	// Camera
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

	// Load Input Actions and Mapping Context
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
void ASimulatedRayCastVehicle::BeginPlay()
{
	Super::BeginPlay();

	// Programmatically add mappings
	if (DefaultMappingContext)
	{
		// Map MoveAction to WASD and Gamepad Left Stick
		/*
		DefaultMappingContext->MapKey(MoveAction, EKeys::W);
		DefaultMappingContext->MapKey(MoveAction, EKeys::S);
		DefaultMappingContext->MapKey(MoveAction, EKeys::A);
		DefaultMappingContext->MapKey(MoveAction, EKeys::D);
		DefaultMappingContext->MapKey(MoveAction, EKeys::Gamepad_LeftX);
		DefaultMappingContext->MapKey(MoveAction, EKeys::Gamepad_LeftY);
		*/

		// Add to subsystem
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
void ASimulatedRayCastVehicle::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	SuspensionCast(FLeftSuspensionRoot);
	SuspensionCast(FRightSuspensionRoot);
	SuspensionCast(RLeftSuspensionRoot);
	SuspensionCast(RRightSuspensionRoot);
	CalculateAcceleration();
}

// Called to bind functionality to input
void ASimulatedRayCastVehicle::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(AccelerateAction, ETriggerEvent::Triggered, this, &ASimulatedRayCastVehicle::Accelerate);
		EnhancedInputComponent->BindAction(SteerAction, ETriggerEvent::Triggered, this, &ASimulatedRayCastVehicle::Steer);
		EnhancedInputComponent->BindAction(DriftAction, ETriggerEvent::Started, this, &ASimulatedRayCastVehicle::StartDrift);
		EnhancedInputComponent->BindAction(DriftAction, ETriggerEvent::Canceled, this, &ASimulatedRayCastVehicle::StopDrift);
		EnhancedInputComponent->BindAction(DriftAction, ETriggerEvent::Completed, this, &ASimulatedRayCastVehicle::StopDrift);
		//EnhancedInputComponent->BindAction(RotateAction, ETriggerEvent::Triggered, this, &AMyPawn::Rotate);
	}
}

void ASimulatedRayCastVehicle::SuspensionCast(USceneComponent* SuspensionAxisRef)
{
	FVector SuspensionAxisLoc = SuspensionAxisRef->GetComponentLocation();
	FVector TraceEnd = SuspensionAxisLoc + SuspensionAxisRef->GetUpVector() * (-TargetHoverHeight);// + 200.0f); // Trace further

	FHitResult HitResult;
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(this);

	bool bHit = UKismetSystemLibrary::LineTraceSingle(
		GetWorld(), SuspensionAxisLoc, TraceEnd, UEngineTypes::ConvertToTraceType(ECC_Visibility),
		false, ActorsToIgnore, EDrawDebugTrace::ForOneFrame, HitResult, true);

	//FVector TargetUp = FVector::UpVector; // Default to world up if no ground
	//float CurrentGroundDistance = TargetHoverHeight + 100.f; // Assume far if no hit

	if (bHit && HitResult.bBlockingHit)
	{
		//CurrentGroundDistance = HitResult.Distance;
		//TargetUp = HitResult.ImpactNormal; // Align to ground
		// World-Space direction of the spring force.
		FVector SpringDir = SuspensionAxisRef->GetUpVector();
		// World-Space velocity of this tire
		FVector SuspensionWorldVel = BoxCollider->GetPhysicsLinearVelocityAtPoint(SuspensionAxisLoc);
		// Calculate offset from the raycast
		float NormalizedHitDist = 1.0f - UKismetMathLibrary::NormalizeToRange(HitResult.Distance, 0, TargetHoverHeight);//SuspensionResetDist - HitResult.Distance;
		// Calculate velocity along the spring direction
		// note that springDir is a unit vector, so this returns the magnitude of tireWorldVel
		// as projected onto springDir
		float SpringVel = FVector::DotProduct(SpringDir, SuspensionWorldVel);
		// Calculate the magnitude of the dampened spring force
		float DampenedSpringForce = (NormalizedHitDist * SpringStrength) - (SpringVel * SpringDamper);
		// apply the force at the location of this tire, in the direction of the suspension
		BoxCollider->AddForceAtLocation(SpringDir * DampenedSpringForce, SuspensionAxisLoc);

		if (DrawDebug)
		{
			// Draw debug arrow for suspension strength direction
			FVector End = SuspensionAxisLoc + (SpringDir * DampenedSpringForce * 0.003);
			UKismetSystemLibrary::DrawDebugArrow(GetWorld(), SuspensionAxisLoc, End, 100.f, FColor::Blue, 0.0f, 5.0f);
		}

		AccelerateVehicle(SuspensionAxisRef, HitResult);

		// Old Version
		FVector SuspensionUnitVector = UKismetMathLibrary::GetDirectionUnitVector(HitResult.TraceEnd, HitResult.TraceStart);
		//FVector SuspensionDistVector = SuspensionUnitVector * NormalizedHitDist * SuspensionStrengthMultiplier;
		//BoxCollider->AddForceAtLocation(SuspensionDistVector, SuspensionAxisLoc);
	} else
	{
		AccelerateVehicle(SuspensionAxisRef, HitResult);
	}
}

void ASimulatedRayCastVehicle::CalculateAcceleration()
{
	Acceleration = FMath::Lerp(0, MaxAcceleration, AccelerationInput) * AccelerationInput;
	AccelerationInput = FMath::FInterpTo(AccelerationInput, 0, GetWorld()->GetDeltaSeconds(), 0.3);
}

void ASimulatedRayCastVehicle::AccelerateVehicle(USceneComponent* SuspensionAxisRef, FHitResult HitResult)
{
	FVector SuspensionAxisLoc = SuspensionAxisRef->GetComponentLocation();
	FVector NewCenterOfMass = AccelerationCenterOfMassOffset * AccelerationInput;
	BoxCollider->SetCenterOfMass(NewCenterOfMass);

	// Acceleration Handling
	FVector AccelDir = BoxCollider->GetForwardVector() * AccelerationForce * AccelerationInput * BoxCollider->GetMass() * SpeedModifier;
	FVector GravityStrength = FVector(0, 0, 0);
	if (!IsOnGround())
	{
		GravityStrength = FVector(0, 0, AccelerationGravityStrength);
	}
	FVector AccelWithGravity = AccelDir + GravityStrength;
	BoxCollider->AddForceAtLocation(AccelWithGravity, SuspensionAxisLoc);
}

bool ASimulatedRayCastVehicle::IsOnGround()
{
	FHitResult HitResult;
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(this);

	FVector TraceHeightOfVehicle = GetActorLocation() - FVector(0, 0, TargetHoverHeight);

	bool bHit = UKismetSystemLibrary::LineTraceSingle(
		GetWorld(), GetActorLocation(), TraceHeightOfVehicle, UEngineTypes::ConvertToTraceType(ECC_Visibility),
		false, ActorsToIgnore, EDrawDebugTrace::ForOneFrame, HitResult, true);
	return bHit;
}

void ASimulatedRayCastVehicle::StartDrift(const FInputActionValue& Value)
{
	bool DriftValue = Value.Get<bool>(); // 1 to -1 since 1d axis
	SteeringMultiplier = 4.0f;
	bIsDrifting = true;
	DriftRotation.Roll = 0.f;
	DriftRotation.Pitch = 0.f;
	DriftRotation.Yaw = 0.f;
}

void ASimulatedRayCastVehicle::StopDrift(const FInputActionValue& Value)
{
	bool DriftValue = Value.Get<bool>(); // 1 to -1 since 1d axis
}

void ASimulatedRayCastVehicle::Accelerate(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	float ThrottleValue = Value.Get<float>(); // 1 to -1 since 1d axis
	if (Controller != nullptr)
	{
		float CanThrottleAdjustment = ThrottleValue * 0.5;
		if (IsOnGround())
		{
			CanThrottleAdjustment = ThrottleValue;
		}
		AccelerationInput = FMath::FInterpTo(AccelerationInput, CanThrottleAdjustment, GetWorld()->GetDeltaSeconds(), 0.5f);
	}
}

void ASimulatedRayCastVehicle::Steer(const FInputActionValue& Value)
{
	float SteerValue = Value.Get<float>(); // 1 to -1 since 1d axis
	if (Controller != nullptr)
	{
		BoxCollider->AddTorqueInRadians(FVector(0, 0, SteerValue * TorqueStrength * (AccelerationInput * SteeringMultiplier)));
	}
}