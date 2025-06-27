// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "PodracerMovementComponent.generated.h"

USTRUCT()
struct FPodracerMove
{
    GENERATED_BODY()

    UPROPERTY()
    float ForwardInput = 0.f;
    UPROPERTY()
    float TurnInput = 0.f;
    UPROPERTY()
    float DeltaTime = 0.f;
    UPROPERTY()
    float TimeStamp = 0.f; // For server reconciliation

    // You can add more state here if needed, like a "Boost" flag
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTPODRACER_API UPodracerMovementComponent : public UPawnMovementComponent
{
    GENERATED_BODY()

public:
    UPodracerMovementComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // --- Configurable Stats ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Podracer Movement")
    float MaxFlySpeed;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Podracer Movement")
    float Acceleration;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Podracer Movement")
    float TurnSpeed; // Degrees per second

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Podracer Movement")
    float TargetHoverHeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Podracer Movement")
    float HoverStiffness; // How quickly it corrects to hover height

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Podracer Movement")
    float HoverDamping;   // Prevents oscillation

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Podracer Movement")
    float MinGroundDistanceForFullHoverEffect; // Distance below which hover has less/no effect

    // Call this from Pawn to provide input
    void SetThrottleInput(float InThrottle);
    void SetSteeringInput(float InSteering);

protected:
    // Current input state
    float CurrentThrottleInput;
    float CurrentSteeringInput;

    // Internal movement state (could be replicated for smoother client visuals)
    UPROPERTY(Transient, Replicated) // Transient: not saved, Replicated: send to clients
    FVector CurrentVelocity; 
    // Note: For truly robust replication, you'd typically replicate less, like just inputs,
    // and have server authoritative moves. Replicating velocity is a simpler start.

    virtual void ApplyControlInputToVelocity(float DeltaTime);
    virtual void ApplyHover(float DeltaTime);
    virtual void SimulateMovement(float DeltaTime); // Main movement logic

    // --- Replication ---
    // This is a simplified replication setup. Full client-side prediction is more complex.
public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // For server to process client moves. A full implementation involves FSavedMove_Character etc.
    // This is a very simplified version.
    UFUNCTION(Server, Reliable, WithValidation)
    void Server_SendMove(const FPodracerMove& Move);

private:
    TArray<FPodracerMove> UnacknowledgedMoves; // For client-side prediction
    FPodracerMove LastMove;
};