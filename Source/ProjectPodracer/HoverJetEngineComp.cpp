// Fill out your copyright notice in the Description page of Project Settings.


#include "HoverJetEngineComp.h"

#include "Components/BoxComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"


// Sets default values for this component's properties
UHoverJetEngineComp::UHoverJetEngineComp()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	UActorComponent::SetComponentTickEnabled(true);

	BoxCollider = CreateDefaultSubobject<UBoxComponent>(TEXT("HullCollider"));
	BoxCollider->SetupAttachment(this);
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
		BodyInstance->SetDOFLock(EDOFMode::Type::SixDOF);
		BodyInstance->bLockXRotation = true;
		BodyInstance->bLockYRotation = true;
		BodyInstance->bLockZRotation = false;  // Allow yaw
	}

	// Set up cosmetic ship body
	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
	HullMesh->SetupAttachment(BoxCollider);
	HullMesh->SetSimulatePhysics(false);
	HullMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	HullMesh->SetRelativeScale3D(FVector(2, 1, 0.25f));
	
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


// Called when the game starts
void UHoverJetEngineComp::BeginPlay()
{
	Super::BeginPlay();

	// ...
	// Bind collision event
	if (BoxCollider)
	{
		BoxCollider->OnComponentHit.AddDynamic(this, &UHoverJetEngineComp::OnComponentHit);
	}
	
}


// Called every frame
void UHoverJetEngineComp::TickComponent(float DeltaTime, ELevelTick TickType,
                                        FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Calculate speed (forward velocity component)
	if (BoxCollider && BoxCollider->IsSimulatingPhysics())
	{
		CurrentSpeed = FVector::DotProduct(BoxCollider->GetPhysicsLinearVelocity(), GetForwardVector());
	}

	// Perform physics calculations
	CalculateHover(DeltaTime);
	CalculatePropulsion(DeltaTime);
}

float UHoverJetEngineComp::GetSpeedPercentage() const
{
	return BoxCollider ? BoxCollider->GetPhysicsLinearVelocity().Size() / TerminalVelocity : 0.0f;
}

void UHoverJetEngineComp::CalculateHover(float DeltaTime)
{
	FVector GroundNormal = FVector::UpVector;
	bIsOnGround = false;
	float Height = MaxGroundDist;

	// Line trace downward to detect ground
	FVector Start = BoxCollider->GetComponentLocation();
	FVector End = Start - GetUpVector() * MaxGroundDist;
	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());

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
			DrawDebugSphere(GetWorld(), Start - GetUpVector() * HoverHeight, 10.0f, 12, FColor::Blue, false, 0.0f);
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
	FVector Projection = UKismetMathLibrary::ProjectVectorOnToPlane(GetForwardVector(), GroundNormal);
	FRotator TargetRotation = UKismetMathLibrary::MakeRotFromZX(GroundNormal, Projection);
	FRotator NewRotation = FMath::RInterpTo(GetComponentRotation(), TargetRotation, DeltaTime, 5);//10.0f);
	BoxCollider->SetWorldRotation(NewRotation);
}

void UHoverJetEngineComp::CalculatePropulsion(float DeltaTime)
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

void UHoverJetEngineComp::OnComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
                                         UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (OtherActor)
	{
		// Check for Wall collision (requires custom channel)
		if (HitComponent->GetCollisionObjectType() == ECC_WorldDynamic) // Placeholder: Replace with Wall channel
		{
			FVector UpwardImpulse = FVector::DotProduct(NormalImpulse, GetUpVector()) * GetUpVector();
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

