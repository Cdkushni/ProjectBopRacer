// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
// You will need to forward-declare or include headers for your components and input actions
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "ReplicatedPodRacer.generated.h"

class UPodMovementComponent;
class UEngineComponent;
struct FInputActionValue;
/**
 * A new Pawn class designed for replication.
 * It does NOT contain direct physics logic. Instead, it owns and relies on
 * a UPodMovementComponent to handle all movement and networking.
 */
UCLASS()
class PROJECTPODRACER_API AReplicatedPodRacer : public APawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
    AReplicatedPodRacer();

    // Getter for the movement component
    UFUNCTION(BlueprintPure, Category = "Movement")
    UPodMovementComponent* GetPodMovementComponent() const { return PodMovementComponent; }

    // Getter for the physics body
    UFUNCTION(BlueprintPure, Category = "Components")
    UBoxComponent* GetPhysicsBody() const { return BoxCollider; }
    
    // Keeping your engine management functions
    UFUNCTION(BlueprintCallable, Category = "Engine")
    void AddEngine(FDataTableRowHandle EngineStatsHandle, const FVector& Offset);
    
    // We need to access the engines from the movement component
    UFUNCTION(BlueprintCallable, Category = "Engine")
    const TArray<UEngineComponent*>& GetEngines() const { return Engines; }


protected:
    virtual void BeginPlay() override; // Added BeginPlay to set physics constraints
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
    virtual void TornOff() override;
    virtual void Destroyed() override;

protected:
    // The component that will handle our movement logic and replication
    UPROPERTY(Category = Pawn, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
    UPodMovementComponent* PodMovementComponent;
    
    // --- Components ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UBoxComponent* BoxCollider;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UStaticMeshComponent* HullMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USpringArmComponent* SpringArm;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UCameraComponent* Camera;
    
    // You can still manage engines on the pawn
    UPROPERTY(VisibleInstanceOnly, Category = "Engine")
    TArray<UEngineComponent*> Engines;

private:
    UPROPERTY(EditAnywhere, Category = "ConfigData")
    UPhysicalMaterial* BoxPhysicalMaterial;

    // --- Input Actions (assign these in your Blueprint) ---
    UPROPERTY(EditAnywhere, Category = "Input")
    UInputMappingContext* DefaultMappingContext;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* AccelerateAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* SteerAction;
    
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* BreakAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* DriftAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* BoostAction;

    // --- Input Callbacks ---
    void Accelerate(const FInputActionValue& Value);
    void AccelerateCompleted(const FInputActionValue& Value);
    void Steer(const FInputActionValue& Value);
    void SteerCompleted(const FInputActionValue& Value);
    void Break(const FInputActionValue& Value);
    void BreakOff(const FInputActionValue& Value);
    void Drift(const FInputActionValue& Value);
    void DriftOff(const FInputActionValue& Value);
    void Boost(const FInputActionValue& Value);
    void BoostOff(const FInputActionValue& Value);
    
    int32 EngineNameIndex = 0;
    FString MakeNewEngineName();
};
