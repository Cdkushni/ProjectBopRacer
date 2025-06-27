// HoverRacerPawn.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "HoverRacerPawn.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UCameraComponent;
class USpringArmComponent;

// Structure to hold PID controller state for each hover point
USTRUCT(BlueprintType)
struct FPIDControllerState
{
    GENERATED_BODY()

    UPROPERTY()
    float IntegralTerm;

    UPROPERTY()
    float PreviousError;

    FPIDControllerState() : IntegralTerm(0.0f), PreviousError(0.0f) {}

    void Reset()
    {
        IntegralTerm = 0.0f;
        PreviousError = 0.0f;
    }
};

UCLASS(Blueprintable)
class PROJECTPODRACER_API AHoverRacerPawn : public APawn
{
    GENERATED_BODY()

public:
    AHoverRacerPawn();

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    // --- Components ---

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UStaticMeshComponent* HullMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Physics|Data", meta = (AllowPrivateAccess = "true"))
    TArray<TObjectPtr<USceneComponent>> HoverPoints;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USpringArmComponent* SpringArm;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UCameraComponent* Camera;


    // --- Hover Parameters ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Physics|PID")
    float HoverPidKp; // Proportional gain

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Physics|PID")
    float HoverPidKi; // Integral gain

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Physics|PID")
    float HoverPidKd; // Derivative gain

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Physics")
    float TargetHoverHeight; // Desired distance from the ground

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Physics")
    float HoverTraceLength; // How far down to trace for ground


    // --- Movement Parameters ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Physics")
    float ForwardAcceleration;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Physics")
    float BackwardAcceleration;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Physics")
    float StrafeAcceleration;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Physics")
    float TurnStrength;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Physics")
    float BoostMultiplier; // Multiplier for forward acceleration when boosting

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Physics")
    float MaxSpeed; // Maximum speed of the vehicle in cm/s

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Physics|Advanced")
    float DriftReductionFactor; // How strongly to counteract sideways velocity

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Physics|Advanced")
    float TurnAssistFactor; // How strongly to push the vehicle into turns

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Physics|Damping")
    float LinearDamping;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement Physics|Damping")
    float AngularDamping;


private:
    // Input values
    float CurrentForwardInput;
    float CurrentTurnInput;
    float CurrentStrafeInput;
    bool bIsBoosting;

    // PID states for each hover point
    TArray<FPIDControllerState> HoverPointPIDStates;
    bool bIsGrounded; // Simple flag, true if any hover point hits ground

    // Input binding functions
    void MoveForwardInput(float Value);
    void TurnInput(float Value);
    void StrafeInput(float Value);
    void StartBoosting();
    void StopBoosting();

    // Helper functions
    void ApplyHover(float DeltaTime);
    void ApplyMovement(float DeltaTime);
    void UpdateGroundedState(bool bAnyPointHitGround);
};