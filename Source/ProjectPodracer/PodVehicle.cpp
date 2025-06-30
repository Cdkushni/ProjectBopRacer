// This file implements the PodVehicle class, including its constructor,
// input binding, and replication setup.

#include "PodVehicle.h"
#include "PodVehicleMovementComponent.h" // Include our custom movement component
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h" // For basic collision
#include "Net/UnrealNetwork.h" // Required for replication


// Sets default values
APodVehicle::APodVehicle()
{
 	// Set this pawn to call Tick() every frame. You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Create a default root component. We'll use a CapsuleComponent for basic collision.
	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("RootCapsule"));
	RootComponent = CapsuleComponent;
	CapsuleComponent->SetCapsuleHalfHeight(100.0f); // Adjust size as needed
	CapsuleComponent->SetCapsuleRadius(50.0f);
	CapsuleComponent->SetCollisionProfileName(TEXT("Pawn")); // Use a common collision profile
	// Physics simulation is NOT enabled on the CapsuleComponent for this custom movement.
	CapsuleComponent->SetEnableGravity(false); // Gravity handled by movement component
	CapsuleComponent->SetLinearDamping(0.0f); // Damping handled by movement component
	CapsuleComponent->SetAngularDamping(0.0f); // Damping handled by movement component
	// Create and attach the custom movement component
	// Note: We specify our custom movement component here.
	PodMovementComponent = CreateDefaultSubobject<UPodVehicleMovementComponent>(TEXT("PodMovementComponent"));
	PodMovementComponent->UpdatedComponent = RootComponent; // Tell the movement component what to move

	VehicleCenterRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VehicleCenterRoot"));
	VehicleCenterRoot->SetupAttachment(RootComponent);

	EngineCenterPoint = CreateDefaultSubobject<USceneComponent>(TEXT("EngineCenterPoint"));
	EngineCenterPoint->SetupAttachment(VehicleCenterRoot);

	LeftEngineRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LeftEngineRoot"));
	LeftEngineRoot->SetupAttachment(EngineCenterPoint);
	LeftEngineRoot->SetRelativeLocation(FVector(200.f, -100.f, 0.f));
	
	RightEngineRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RightEngineRoot"));
	RightEngineRoot->SetupAttachment(EngineCenterPoint);
	RightEngineRoot->SetRelativeLocation(FVector(200.f, 100.f, 0.f));

	PodSpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("PodSpringArm"));
	PodSpringArm->SetupAttachment(EngineCenterPoint);
	PodSpringArm->TargetArmLength = 600.0f;
	PodSpringArm->SocketOffset = FVector(0, 0, 0.f);
	PodSpringArm->bUsePawnControlRotation = false;
	PodSpringArm->bEnableCameraLag = true;
	PodSpringArm->bEnableCameraRotationLag = true;
	PodSpringArm->CameraLagSpeed = 15.f;
	PodSpringArm->CameraRotationLagSpeed = 12.f;

	PodHullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PodHullMesh"));
	PodHullMesh->SetupAttachment(PodSpringArm);
	PodHullMesh->SetSimulatePhysics(false);
	PodHullMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	PodHullMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f)); // Cockpit offset
	PodHullMesh->SetRelativeScale3D(FVector(1.0f, 0.5f, 0.125f)); // Cockpit offset

	// Set the PodVehicle to replicate itself over the network.
	bReplicates = true;
	// Ensures the vehicle's transform is replicated.
	SetReplicateMovement(true);

	// Recommended for Pawns/Characters in multiplayer for better prediction/smoothing
	// Client receives updates from the server and tries to predict movement.
	// Server is always authoritative.
	SetNetUpdateFrequency(30.0f); // How often the server sends updates for this actor (Hz)
	SetMinNetUpdateFrequency(5.0f); // Minimum update frequency
}

// Called when the game starts or when spawned
void APodVehicle::BeginPlay()
{
	Super::BeginPlay();

}

// Called every frame
void APodVehicle::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void APodVehicle::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Bind axis inputs to our movement functions.
	// You'll need to define "MoveForward" and "TurnRight" in your Project Settings -> Input.
	// Example: MoveForward (W=1, S=-1), TurnRight (A=-1, D=1)
	PlayerInputComponent->BindAxis("MoveForward", this, &APodVehicle::MoveForward);
	PlayerInputComponent->BindAxis("TurnRight", this, &APodVehicle::TurnRight);

	// Bind action inputs for Boost, Brake, Drift.
	// You'll need to define "Boost", "Brake", "Drift" in your Project Settings -> Input -> Action Mappings.
	// Example: Boost (Spacebar), Brake (Left Shift), Drift (Left Ctrl)
	PlayerInputComponent->BindAction("Boost", IE_Pressed, this, &APodVehicle::BoostPressed);
	PlayerInputComponent->BindAction("Boost", IE_Released, this, &APodVehicle::BoostReleased);
	PlayerInputComponent->BindAction("Brake", IE_Pressed, this, &APodVehicle::BrakePressed);
	PlayerInputComponent->BindAction("Brake", IE_Released, this, &APodVehicle::BrakeReleased);
	PlayerInputComponent->BindAction("Drift", IE_Pressed, this, &APodVehicle::DriftPressed);
	PlayerInputComponent->BindAction("Drift", IE_Released, this, &APodVehicle::DriftReleased);
}

// Handles input for accelerating the pod forward.
void APodVehicle::MoveForward(float Value)
{
	// Only apply input if our movement component is valid.
	if (PodMovementComponent)
	{
		PodMovementComponent->SetMoveForwardInput(Value);
	}
}

// Handles input for steering the pod left/right.
void APodVehicle::TurnRight(float Value)
{
	// Only apply input if our movement component is valid.
	if (PodMovementComponent)
	{
		PodMovementComponent->SetTurnRightInput(Value);
	}
}

// Handles input for boosting - pressed.
void APodVehicle::BoostPressed()
{
	if (PodMovementComponent)
	{
		PodMovementComponent->SetBoostInput(true);
	}
}

// Handles input for boosting - released.
void APodVehicle::BoostReleased()
{
	if (PodMovementComponent)
	{
		PodMovementComponent->SetBoostInput(false);
	}
}

// Handles input for braking - pressed.
void APodVehicle::BrakePressed()
{
	if (PodMovementComponent)
	{
		PodMovementComponent->SetBrakeInput(true);
	}
}

// Handles input for braking - released.
void APodVehicle::BrakeReleased()
{
	if (PodMovementComponent)
	{
		PodMovementComponent->SetBrakeInput(false);
	}
}

// Handles input for drifting - pressed.
void APodVehicle::DriftPressed()
{
	if (PodMovementComponent)
	{
		PodMovementComponent->SetDriftInput(true);
	}
}

// Handles input for drifting - released.
void APodVehicle::DriftReleased()
{
	if (PodMovementComponent)
	{
		PodMovementComponent->SetDriftInput(false);
	}
}

// This function is crucial for network replication.
// It tells Unreal Engine which properties of this class should be synchronized
// between the server and connected clients.
void APodVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Add properties here that need to be replicated.
	// Currently, our movement component handles internal replication for movement state.
	// If you added other properties to APodVehicle that need to be synced, you'd add them here.
	// DOREPLIFETIME(APodVehicle, MyReplicatedVariable); // Example
}