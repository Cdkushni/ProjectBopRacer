// Copyright 2024, Your Name/Company. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "DestructibleBuildingActor.generated.h"

UCLASS()
class PROJECTPODRACER_API ADestructibleBuildingActor : public AActor
{
	GENERATED_BODY()

public:
	ADestructibleBuildingActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// The root component.
	UPROPERTY(VisibleAnywhere)
	USceneComponent* SceneRoot;

	// The visible mesh for the building when it is intact.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* IntactMeshComponent;

	// The Chaos physics component that contains the fractured pieces of the building.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UGeometryCollectionComponent* GeometryCollectionComponent;

	// This function is called when the actor takes damage.
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

	// --- Multiplayer ---
	// This function replicates the destruction effect to all clients.
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_TriggerDestruction();
	void Multicast_TriggerDestruction_Implementation();

public:
	// Call this to initiate the destruction sequence.
	UFUNCTION(BlueprintCallable, Category = "Destruction")
	void TriggerDestruction();

private:
	UPROPERTY(ReplicatedUsing = OnRep_IsDestroyed)
	bool bIsDestroyed = false;

	// This function is called on clients when the bIsDestroyed variable is replicated.
	UFUNCTION()
	void OnRep_IsDestroyed();
    
	// Ensures variables are replicated for multiplayer.
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
