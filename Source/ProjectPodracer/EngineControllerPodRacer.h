#pragma once

#include "CoreMinimal.h"
#include "PIDController.h"
#include "GameFramework/Pawn.h"
#include "EngineComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "EngineControllerPodRacer.generated.h"

struct FInputActionValue;

UCLASS(Blueprintable)
class PROJECTPODRACER_API AEngineControllerPodRacer : public APawn
{
    GENERATED_BODY()

public:
    AEngineControllerPodRacer();

    UFUNCTION(BlueprintCallable, Category = "Vehicle")
    float GetSpeedPercentage() const;

    UFUNCTION(BlueprintCallable, Category = "Input")
    float GetRudderInput() const { return RudderInput; }

    // Engine management
    UFUNCTION(BlueprintCallable, Category = "Engine")
    void AddEngine(FDataTableRowHandle EngineStatsHandle, const FVector& Offset);

    UFUNCTION(BlueprintCallable, Category = "Engine")
    void RemoveEngine(int32 EngineIndex);

    UFUNCTION(BlueprintCallable, Category = "Engine")
    void DamageEngine(int32 EngineIndex, float DamageAmount);

    UFUNCTION(BlueprintCallable, Category = "Engine")
    void RepairEngine(int32 EngineIndex);

    UFUNCTION(BlueprintCallable, Category = "Engine")
    void BoostEngine(int32 EngineIndex, float Duration);

    UFUNCTION(BlueprintCallable, Category = "Engine")
    void DisableEngine(int32 EngineIndex);

    UFUNCTION(BlueprintCallable, Category = "Engine")
    void EnableEngine(int32 EngineIndex);

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    void CalculateHover(float DeltaTime);
    void CalculatePropulsion(float DeltaTime);

public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UBoxComponent* BoxCollider;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UStaticMeshComponent* HullMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USpringArmComponent* PodSpringArm;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USceneComponent* EngineConnectorRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USpringArmComponent* SpringArm;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UCameraComponent* Camera;

    UPROPERTY(EditAnywhere, Category = "ConfigData|DriveSettings")
    float SlowingVelFactor = 0.99f;

    UPROPERTY(EditAnywhere, Category = "ConfigData|DriveSettings")
    float BrakingVelFactor = 0.98f;

    UPROPERTY(EditAnywhere, Category = "ConfigData|DriveSettings")
    float AngleOfRoll = 30.0f;

    UPROPERTY(EditAnywhere, Category = "ConfigData|HoverSettings")
    float HoverHeight = 100.0f;

    UPROPERTY(EditAnywhere, Category = "ConfigData|HoverSettings")
    float MaxGroundDist = 500.0f;

    UPROPERTY(EditAnywhere, Category = "ConfigData|HoverSettings")
    TEnumAsByte<ECollisionChannel> GroundCollisionChannel = ECC_WorldStatic;

    UPROPERTY(EditAnywhere, Category = "ConfigData|HoverSettings")
    FPIDController HoverPID;

    UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
    float TerminalVelocity = 30000.0f;

    UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
    float HoverGravity = 2000.0f;

    UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
    float FallGravity = 9810.0f;

    UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
    float MaxTurnRate = 2.0f; // Max turn rate in radians/sec at full stick input

    UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
    float SteeringMultiplier = 600.0f; // This was SteeringMultiplier

    UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
    float SidewaysGripFactor = 100.0f;

    UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings", meta=(ClampMin="0.0", ClampMax="1.0"))
    float HighSpeedSteeringDampFactor = 0.4f; // At max speed, steering is 40% as effective

    UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
    float KeyboardSteeringInterpSpeed = 5.0f; // How fast the steering ramps up when a key is pressed

    UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
    float KeyboardSteeringReturnSpeed = 8.0f; // How fast the steering returns to center when released (often slightly faster)

    UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
    float DriftMultiplier = 1.5f;

    UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
    float BoostMultiplier = 3.0f;

    UPROPERTY(EditAnywhere, Category = "ConfigData|PhysicsSettings")
    float Mass = 100.0f;

    UPROPERTY(EditAnywhere, Category = "Physics")
    float LinearDamping = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Physics")
    float AngularDamping = 3.0f; // Increased for imbalance

    UPROPERTY(EditAnywhere, Category = "Debug")
    bool bDrawDebug = true;

    UPROPERTY(EditAnywhere, Category = "Debug")
    float DebugArrowLength = 100.0f;

    UPROPERTY(EditAnywhere, Category = "Debug")
    float DebugArrowSize = 10.0f;

    UPROPERTY(VisibleAnywhere, Category = "Engine")
    TArray<UEngineComponent*> Engines;

    UPROPERTY(EditAnywhere, Category = "ConfigData")
    UPhysicalMaterial* BoxPhysicalMaterial;

private:
    UPROPERTY(EditAnywhere, Category = "Input")
    UInputMappingContext* DefaultMappingContext;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputAction* AccelerateAction;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputAction* SteerAction;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputAction* BreakAction;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputAction* DriftAction;

    UPROPERTY(EditAnywhere, Category = "Input")
    UInputAction* BoostAction;

    void Accelerate(const FInputActionValue& Value);
    void Steer(const FInputActionValue& Value);
    void Break();
    void BreakOff();
    void Drift();
    void DriftOff();
    void Boost();
    void BoostOff();

    UFUNCTION()
    void OnComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

    UFUNCTION()
    void HandleEngineStateChanged(UEngineComponent* Engine);

    UFUNCTION()
    FString MakeNewEngineName();

    float CurrentSpeed;
    float ThrusterInput;
    float RudderInput;
    bool bIsBraking;
    bool bIsOnGround;
    float Drag;
    bool bIsDrifting;
    bool bIsBoosting;
    float AccelerationInput;
    bool bWasOnGroundLastFrame = false;

    // Add this in the private member variables section at the bottom of the .h file
    float SmoothedRudderInput;

    int32 EngineNameIndex = 0;
};