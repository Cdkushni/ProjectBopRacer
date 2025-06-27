// Fill out your copyright notice in the Description page of Project Settings.


#include "HoverRacer.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"


// Sets default values
AHoverRacer::AHoverRacer()
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	AActor::SetActorTickEnabled(true);

	BoxCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("HullCollider"));
	RootComponent = BoxCollider;
	BoxCollider->SetBoxExtent(FVector(100, 52, 12));
	BoxCollider->SetSimulatePhysics(true);
	BoxCollider->SetMassOverrideInKg(NAME_None, Mass); // Sets mass to 1 kg

	// Load default Physical Material
	static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> PhysMatFinder(TEXT("/Game/PM_HoverRacer.PM_HoverRacer"));
	if (PhysMatFinder.Succeeded())
	{
		BoxPhysicalMaterial = PhysMatFinder.Object;
		BoxCollider->SetPhysMaterialOverride(BoxPhysicalMaterial);
	}
	
	BoxCollider->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	BoxCollider->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	BoxCollider->SetLinearDamping(LinearDamping);
	BoxCollider->SetAngularDamping(AngularDamping);
	BoxCollider->SetEnableGravity(false);
	BoxCollider->SetGenerateOverlapEvents(false);
	BoxCollider->SetUseCCD(true); // Enable Continuous Collision Detection
	FBodyInstance* BodyInstance = BoxCollider->GetBodyInstance();
	if (BodyInstance)
	{
		// Initialize rotational constraints (lock X and Y rotation by default)
		BodyInstance->SetUseCCD(true);
		//BoxCollider->BodyInstance.SetUseMACD(true); // TODO: What is this?
		//BodyInstance->SetUpdateKinematicFromSimulation(true); // TODO: What is this?
		//BoxCollider->SetConstraintMode(EDOFMode::Type::SixDOF); // Full Control over axes
		BodyInstance->SetDOFLock(EDOFMode::Type::SixDOF);
		BodyInstance->bLockXRotation = true;
		BodyInstance->bLockYRotation = false; // Allow pitch
		BodyInstance->bLockZRotation = true;
	}

	// Set up cosmetic ship body
	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
	HullMesh->SetupAttachment(BoxCollider);
	HullMesh->SetSimulatePhysics(false);
	HullMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	// Camera
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
	
	// Initialize runtime variables
	CurrentSpeed = 0.0f;
	ThrusterInput = 0.0f;
	RudderInput = 0.0f;
	bIsBraking = false;
	bIsOnGround = false;
	bIsDrifting = false;
	bIsBoosting = false;
	Drag = DriveForce / TerminalVelocity;
	
}

float AHoverRacer::GetSpeedPercentage() const
{
	return BoxCollider ? BoxCollider->GetPhysicsLinearVelocity().Size() / TerminalVelocity : 0.0f;
}

// Called when the game starts or when spawned
void AHoverRacer::BeginPlay()
{
	Super::BeginPlay();

	// Programmatically add mappings
	if (DefaultMappingContext)
	{
		// Add to subsystem
		if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}
	// Bind collision event
	if (BoxCollider)
	{
		BoxCollider->OnComponentHit.AddDynamic(this, &AHoverRacer::OnComponentHit);
	}
}

// Called every frame
void AHoverRacer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// Calculate speed (forward velocity component)
	if (BoxCollider && BoxCollider->IsSimulatingPhysics())
	{
		CurrentSpeed = FVector::DotProduct(BoxCollider->GetPhysicsLinearVelocity(), GetActorForwardVector());
	}

	// Perform physics calculations
	CalculateHover(DeltaTime);
	CalculatePropulsion(DeltaTime);
}

// Called to bind functionality to input
void AHoverRacer::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(AccelerateAction, ETriggerEvent::Triggered, this, &AHoverRacer::Accelerate);
		EnhancedInputComponent->BindAction(SteerAction, ETriggerEvent::Triggered, this, &AHoverRacer::Steer);
		EnhancedInputComponent->BindAction(BreakAction, ETriggerEvent::Triggered, this, &AHoverRacer::Break);
		EnhancedInputComponent->BindAction(BreakAction, ETriggerEvent::Completed, this, &AHoverRacer::BreakOff);
		EnhancedInputComponent->BindAction(BreakAction, ETriggerEvent::Canceled, this, &AHoverRacer::BreakOff);
		EnhancedInputComponent->BindAction(DriftAction, ETriggerEvent::Triggered, this, &AHoverRacer::Drift);
		EnhancedInputComponent->BindAction(DriftAction, ETriggerEvent::Completed, this, &AHoverRacer::DriftOff);
		EnhancedInputComponent->BindAction(DriftAction, ETriggerEvent::Canceled, this, &AHoverRacer::DriftOff);
		EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Triggered, this, &AHoverRacer::Boost);
		EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Completed, this, &AHoverRacer::BoostOff);
		EnhancedInputComponent->BindAction(BoostAction, ETriggerEvent::Canceled, this, &AHoverRacer::BoostOff);
	}
}

void AHoverRacer::CalculateHover(float DeltaTime)
{
	FVector GroundNormal = FVector::UpVector;
	bIsOnGround = false;
	float Height = MaxGroundDist;

	// Line trace downward to detect ground
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

	// Draw debug line
	if (bDrawDebug)
	{
		DrawDebugLine(GetWorld(), Start, End, bIsOnGround ? FColor::Green : FColor::Red, false, 0.0f, 0, 1.0f);
		if (bIsOnGround)
		{
			DrawDebugSphere(GetWorld(), Start - GetActorUpVector() * HoverHeight, 10.0f, 12, FColor::Blue, false, 0.0f);
		}
	}

	if (bIsOnGround)
	{
		float Proportional = HoverHeight - Height;
        float ForcePercent = HoverPID.Seek(HoverHeight, Height, DeltaTime);
        FVector Force = GroundNormal * HoverForce * ForcePercent / Mass; // Normalize by mass
        FVector Gravity = -GroundNormal * HoverGravity;
        BoxCollider->AddForce(Force * Mass); // Re-multiply for Unreal’s force
        BoxCollider->AddForce(Gravity * Mass);

        // Debug logging
        if (bDrawDebug)
        {
            float Integral = HoverPID.Integral; // Access via public setter if needed
            float Derivative = (Proportional - HoverPID.LastProportional) / DeltaTime;
            UE_LOG(LogTemp, Log, TEXT("Height: %f, ForcePercent: %f, P: %f, I: %f, D: %f"), Height, ForcePercent, Proportional * HoverPID.PCoeff, Integral * HoverPID.ICoeff, Derivative * HoverPID.DCoeff);
        }
	}
	else
	{
		FVector Gravity = -GroundNormal * FallGravity;
		BoxCollider->AddForce(Gravity * Mass);
	}

	// Align to ground normal
	FVector Projection = UKismetMathLibrary::ProjectVectorOnToPlane(GetActorForwardVector(), GroundNormal);
	FRotator TargetRotation = UKismetMathLibrary::MakeRotFromZX(GroundNormal, Projection);
	FRotator NewRotation = FMath::RInterpTo(GetActorRotation(), TargetRotation, DeltaTime, 5);//10.0f);
	BoxCollider->SetWorldRotation(NewRotation);

	
	UE_LOG(LogTemp, Display, TEXT("Rotation Input: %f"), RudderInput);
	RudderInput = FMath::FInterpTo(RudderInput, 0.f, DeltaTime, 20);
	// Apply cosmetic roll to ShipBody
	float RollAngle = AngleOfRoll * -RudderInput;
	FQuat BodyRotation = GetActorRotation().Quaternion() * FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(RollAngle));
	FQuat CurrentBodyRotation = HullMesh->GetComponentQuat();
	HullMesh->SetWorldRotation(FMath::QInterpTo(CurrentBodyRotation, BodyRotation, DeltaTime, 5));//0.0f));
}

void AHoverRacer::CalculatePropulsion(float DeltaTime)
{

	// Calculate sideways speed
	float SidewaysSpeed = FVector::DotProduct(BoxCollider->GetPhysicsLinearVelocity(), BoxCollider->GetRightVector());
	FVector SideFriction = -BoxCollider->GetRightVector() * (SidewaysSpeed / DeltaTime);
	BoxCollider->AddForce(SideFriction);

	// Apply slowing when not thrusting
	if (ThrusterInput <= 0.0f)
	{
		FVector CurrentVelocity = BoxCollider->GetPhysicsLinearVelocity();
		BoxCollider->SetPhysicsLinearVelocity(CurrentVelocity * SlowingVelFactor);
	}

	// Exit if not on ground
	if (!bIsOnGround)
	{
		return;
	}

	// Apply braking
	if (bIsBraking)
	{
		FVector CurrentVelocity = BoxCollider->GetPhysicsLinearVelocity();
		BoxCollider->SetPhysicsLinearVelocity(CurrentVelocity * BrakingVelFactor);
	}

	float BoostValue = 1.f;
	if (bIsBoosting)
	{
		BoostValue = BoostMultiplier;
	}
	float DriftValue = 1.f;
	if (bIsDrifting)
	{
		DriftValue = 1 / DriftMultiplier;
	}

	// Apply propulsion
	float Propulsion = DriveForce * ThrusterInput * DriftValue * BoostValue - Drag * FMath::Clamp(CurrentSpeed, 0.0f, TerminalVelocity * BoostValue);
	BoxCollider->AddForce(BoxCollider->GetForwardVector() * Propulsion);
}

void AHoverRacer::Accelerate(const FInputActionValue& Value)
{
	ThrusterInput = Value.Get<float>(); // -1 to 1
}

void AHoverRacer::Steer(const FInputActionValue& Value)
{
	RudderInput = Value.Get<float>(); // -1 to 1
	// Apply yaw torque
	float CurrentYawVelocity = BoxCollider->GetPhysicsAngularVelocityInRadians().Z;
	//UE_LOG(LogTemp, Display, TEXT("Rotation Input: %f"), RudderInput);
	float DriftValue = 1.f;
	if (bIsDrifting)
	{
		DriftValue = DriftMultiplier;
	}
	float RotationTorque = (RudderInput * DriftValue) - CurrentYawVelocity;
	BoxCollider->AddTorqueInDegrees(FVector(0, 0, RotationTorque * 1000.0f), NAME_None, true); // TODO: What is the rigidBody.AddRelativeTorque() fourth parameter in unity?
	// TODO: Need to tend turning back to 0 when no steering is occurring.

	//if (RotationTorque > 0)
	//{
		//UE_LOG(LogTemp, Display, TEXT("Rotation Torque: %f"), RotationTorque);
	//}
}

void AHoverRacer::BreakOff()
{
	bIsBraking = false;
}

void AHoverRacer::Break()
{
	bIsBraking = true;
}

void AHoverRacer::Drift()
{
	bIsDrifting = true;
}

void AHoverRacer::DriftOff()
{
	bIsDrifting = false;
}

void AHoverRacer::Boost()
{
	bIsBoosting = true;
}

void AHoverRacer::BoostOff()
{
	bIsBoosting = false;
}

void AHoverRacer::OnComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                                 FVector NormalImpulse, const FHitResult& Hit)
{
	if (OtherActor)
	{
		// Check for Wall collision (requires custom channel)
		if (HitComponent->GetCollisionObjectType() == ECC_WorldDynamic) // Placeholder: Replace with Wall channel
		{
			FVector UpwardImpulse = FVector::DotProduct(NormalImpulse, GetActorUpVector()) * GetActorUpVector();
			BoxCollider->AddImpulse(-UpwardImpulse);
		}

		if (bDrawDebug)
		{
			FVector Start = Hit.Location;
			FVector End = Start + Hit.Normal * DebugArrowLength;
			UKismetSystemLibrary::DrawDebugArrow(GetWorld(), Start, End, DebugArrowSize, FColor::Yellow, 0.5f, 1.0f);
		}
	}
}

