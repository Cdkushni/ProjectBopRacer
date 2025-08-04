// Copyright 2024, Your Name/Company. All Rights Reserved.

#include "DestructibleBuildingActor.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

ADestructibleBuildingActor::ADestructibleBuildingActor()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true; // This actor needs to replicate for multiplayer.
    SetReplicateMovement(true);

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    IntactMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IntactMesh"));
    IntactMeshComponent->SetupAttachment(RootComponent);
    // Important: Enable collision so it can be hit.
    IntactMeshComponent->SetCollisionProfileName(TEXT("BlockAll"));

    GeometryCollectionComponent = CreateDefaultSubobject<UGeometryCollectionComponent>(TEXT("GeometryCollection"));
    GeometryCollectionComponent->SetupAttachment(RootComponent);
    // Start with the fractured mesh hidden and not simulating physics.
    GeometryCollectionComponent->SetVisibility(false);
    GeometryCollectionComponent->SetSimulatePhysics(false);
}

void ADestructibleBuildingActor::BeginPlay()
{
    Super::BeginPlay();
}

void ADestructibleBuildingActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // Replicate the bIsDestroyed variable to all clients.
    DOREPLIFETIME(ADestructibleBuildingActor, bIsDestroyed);
}

float ADestructibleBuildingActor::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
    Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

    // Only trigger destruction if it hasn't been destroyed yet.
    if (!bIsDestroyed)
    {
        TriggerDestruction();
    }

    return DamageAmount;
}

void ADestructibleBuildingActor::TriggerDestruction()
{
    // On the server, update the state and trigger the multicast event.
    if (HasAuthority())
    {
        if (!bIsDestroyed)
        {
            bIsDestroyed = true;
            OnRep_IsDestroyed(); // Call locally on server
        }
    }
}

void ADestructibleBuildingActor::OnRep_IsDestroyed()
{
    // This function runs on the server and all clients when bIsDestroyed changes.
    // It's the guaranteed way to sync the visual state.
    if (bIsDestroyed)
    {
        // Hide the original mesh.
        IntactMeshComponent->SetVisibility(false);
        IntactMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

        // Show the fractured mesh and turn on physics.
        GeometryCollectionComponent->SetVisibility(true);
        GeometryCollectionComponent->SetSimulatePhysics(true);
        
        // Optional: Apply an impulse to make the destruction more dramatic.
        // This would ideally be passed in the multicast function if you want the direction to be synced.
        FVector Impulse = (GetActorLocation() - GetActorUpVector() * 100).GetSafeNormal() * -1;
        GeometryCollectionComponent->AddImpulse(Impulse * 10000.0f, NAME_None, true);
    }
}


// --- DEPRECATED MULTICAST APPROACH ---
// The RepNotify approach above is generally better for state changes.
// A multicast is better for one-off events. I'm leaving this here for educational purposes.
void ADestructibleBuildingActor::Multicast_TriggerDestruction_Implementation()
{
    // This code runs on ALL clients and the server when called.
    
    // if (bIsDestroyed) return; // Prevent it from running twice
    // bIsDestroyed = true;

    // IntactMeshComponent->SetVisibility(false);
    // IntactMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // GeometryCollectionComponent->SetVisibility(true);
    // GeometryCollectionComponent->SetSimulatePhysics(true);
}
