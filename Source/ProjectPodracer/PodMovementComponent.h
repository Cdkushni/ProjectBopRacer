// =============================================================
// PodRacerMovementComponent.h
// =============================================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "EngineComponent.h"
#include "PodMovementComponent.generated.h"

class UBoxComponent;
// Unified the struct name to FPodRacerMoveStruct
USTRUCT()
struct FPodRacerMoveStruct
{
    GENERATED_BODY()

    UPROPERTY()
    float DeltaTime;
    UPROPERTY()
    float ThrusterInput;
    UPROPERTY()
    float RudderInput;
    UPROPERTY()
    bool bIsBraking;
    UPROPERTY()
    bool bIsDrifting;
    UPROPERTY()
    bool bIsBoosting;
    UPROPERTY()
    int32 MoveNumber;
    UPROPERTY()
    float Timestamp;

    bool IsValid() const { return DeltaTime > 0; }
    void Reset() { *this = FPodRacerMoveStruct(); }
};

USTRUCT()
struct FPodRacerState
{
    GENERATED_BODY()

    UPROPERTY() FTransform Transform;
    UPROPERTY() FVector_NetQuantize100 LinearVelocity;
    UPROPERTY() FVector_NetQuantize100 AngularVelocity;
    UPROPERTY() FVector_NetQuantizeNormal GroundNormal = FVector::UpVector;
    UPROPERTY() FPodRacerMoveStruct LastMove;
    UPROPERTY() int32 ReplicationCounter = 0; // Debug counter to force replication
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGroundStateChanged, bool, bIsOnGround);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTPODRACER_API UPodMovementComponent : public UPawnMovementComponent
{
    GENERATED_BODY()

public:
    UPodMovementComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    void SetThrusterInput(float Input) { RawThrusterInput = Input; }
    void SetRudderInput(float Input) { RawRudderInput = Input; }
    void SetIsBraking(bool bBraking) { bIsBrakingInput = bBraking; }
    void SetIsDrifting(bool bDrifting) { bIsDriftingInput = bDrifting; }
    void SetIsBoosting(bool bBoosting) { bIsBoostingInput = bBoosting; }

    FPodRacerMoveStruct CreateMove(float DeltaTime);

    void SimulateMove(const FPodRacerMoveStruct& Move);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_SendMove(const FPodRacerMoveStruct& Move);
    void Server_SendMove_Implementation(const FPodRacerMoveStruct& Move);
    bool Server_SendMove_Validate(const FPodRacerMoveStruct& Move);

protected:
    UPROPERTY(ReplicatedUsing=OnRep_ServerState)
    FPodRacerState ServerState;
    UFUNCTION()
    void OnRep_ServerState();
    UPROPERTY(Replicated, EditAnywhere, Category = "PodRacer")
    bool bWasOnGroundLastFrame;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Hover")
    float HoverHeight = 100.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Hover")
    float MaxGroundDist = 500.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Hover")
    float RotationInterpSpeed = 10.0f; // Degrees per second
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float Mass = 1000.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float FallGravity = 4905.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float MaxVelocity = 2000.0f; // 20m/s
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float MaxSpeed = 1500.0f; // 15m/s
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float Acceleration = 2000.0f; // cm/s^2
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float TurnRate = 90.0f; // Degrees/s
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float BrakeDeceleration = 3000.0f; // cm/s^2
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float DriftTurnRateMultiplier = 1.5f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float BoostSpeedMultiplier = 1.5f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float AirControlMultiplier = 0.3f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float CorrectionThreshold = 10.0f; // cm
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float CorrectionInterpSpeed = 10.0f; // Units/s
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float ServerStateUpdateThreshold = 5.0f; // cm, for triggering ForceNetUpdate
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    float ServerStateForceUpdateInterval = 0.1f; // Seconds, for periodic updates
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics")
    TEnumAsByte<ECollisionChannel> GroundCollisionChannel = ECC_WorldStatic;
    UPROPERTY(EditAnywhere, Category = "Debug")
    bool bEnableDebugLogging = true;
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGroundStateChanged, bool, bIsOnGround);
    UPROPERTY(BlueprintAssignable, Category = "PodRacer")
    FOnGroundStateChanged OnGroundStateChanged;

private:
    UPROPERTY()
    TArray<UEngineComponent*> Engines;
    TArray<FPodRacerMoveStruct> UnacknowledgedMoves;

    // --- State & Input ---
    FPodRacerMoveStruct LastCreatedMove;
    float SmoothedRudderInput = 0.f;
    float RawThrusterInput = 0.f;
    float RawRudderInput = 0.f;
    bool bIsBrakingInput = false;
    bool bIsDriftingInput = false;
    bool bIsBoostingInput = false;
    int32 CurrentMoveNumber = 0;

    float MoveSendTimer = 0.0f;
    float MoveSendInterval = 0.2f;
    float EstimatedLatency = 0.0f;
    float StartupDelayTimer = 1.0f;
    float Height = 0.0f;
    FVector GroundNormal = FVector::UpVector;
    bool bDisableServerReconciliation = false;
    bool bIsOnGround = false;
    FVector LastServerPosition = FVector::ZeroVector;
    float ServerStateUpdateTimer = 0.0f;
    int32 ServerStateReplicationCounter = 0; // Debug counter for server

    void UpdateMoveSendInterval(float DeltaTime);
    void ApplyHover(float DeltaTime, UBoxComponent* PhysicsBody);
    void UpdateServerState(float DeltaTime);

    UBoxComponent* GetPhysicsBody() const;
    class AReplicatedPodRacer* GetPodRacerOwner() const;
};

/*

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PawnMovementComponent.h"
#include "PIDController.h"
#include "PodMovementComponent.generated.h"

// Unified the struct name to FPodRacerMoveStruct
USTRUCT()
struct FPodRacerMoveStruct
{
    GENERATED_BODY()

    UPROPERTY() float ThrusterInput = 0.f;
    UPROPERTY() float RudderInput = 0.f; // The SMOOTHED rudder input
    UPROPERTY() bool bIsBraking = false;
    UPROPERTY() bool bIsDrifting = false;
    UPROPERTY() bool bIsBoosting = false;
    UPROPERTY() float DeltaTime = 0.f;
    UPROPERTY() int32 MoveNumber = 0;
    UPROPERTY() float Timestamp = 0.f;

    bool IsValid() const { return DeltaTime > 0; }
    void Reset() { *this = FPodRacerMoveStruct(); }
};

USTRUCT()
struct FPodRacerState
{
    GENERATED_BODY()

    UPROPERTY() FTransform Transform;
    UPROPERTY() FVector_NetQuantize100 LinearVelocity;
    UPROPERTY() FVector_NetQuantize100 AngularVelocity;
    UPROPERTY() FPodRacerMoveStruct LastMove;
    UPROPERTY() FVector_NetQuantizeNormal GroundNormal = FVector::UpVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGroundStateChanged, bool, bIsOnGround);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTPODRACER_API UPodMovementComponent : public UPawnMovementComponent
{
    GENERATED_BODY()

public:
    UPodMovementComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    void SetThrusterInput(float Input) { RawThrusterInput = Input; }
    void SetRudderInput(float Input) { RawRudderInput = Input; }
    void SetIsBraking(bool bBraking) { bIsBrakingInput = bBraking; }
    void SetIsDrifting(bool bDrifting) { bIsDriftingInput = bDrifting; }
    void SetIsBoosting(bool bBoosting) { bIsBoostingInput = bBoosting; }

    // --- Debugging ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PodRacer|Debug")
    bool bDisableServerReconciliation = false; // The new debug flag

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PodRacer|Debug")
    bool bEnableDebugLogging = false; // The new debug flag

    // --- All physics and movement properties ---
    UPROPERTY(EditAnywhere, Category = "PodRacer|Hover") float HoverHeight = 100.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Hover") float MaxGroundDist = 500.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Hover") FPIDController HoverPID;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics") float HoverGravity = 2000.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics") float FallGravity = 4905.0f;//9810.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics") float SidewaysGripFactor = 100.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Physics") float Mass = 100.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Steering") float MaxTurnRate = 2.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Steering") float SteeringMultiplier = 4.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Steering", meta = (ClampMin = "0.0", ClampMax = "1.0")) float HighSpeedSteeringDampFactor = 0.4f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Steering") float KeyboardSteeringInterpSpeed = 5.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Steering") float KeyboardSteeringReturnSpeed = 8.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Drive") float TerminalVelocity = 20000.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Drive") float DriftMultiplier = 1.25f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Drive") float BoostMultiplier = 3.0f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Drive") float SlowingVelFactor = 0.99f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Drive") float BrakingVelFactor = 0.98f;
    UPROPERTY(EditAnywhere, Category = "PodRacer|Hover") TEnumAsByte<ECollisionChannel> GroundCollisionChannel = ECC_WorldStatic;

protected:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    
    void SimulateMove(const FPodRacerMoveStruct& Move);

    UPROPERTY(BlueprintAssignable, Category = "PodRacer")
    FOnGroundStateChanged OnGroundStateChanged;

private:
    // --- State & Input ---
    FPodRacerMoveStruct LastCreatedMove;
    float SmoothedRudderInput = 0.f;
    float RawThrusterInput = 0.f;
    float RawRudderInput = 0.f;
    bool bIsBrakingInput = false;
    bool bIsDriftingInput = false;
    bool bIsBoostingInput = false;
    uint32 CurrentMoveNumber = 0;

    // --- Networking ---
    UPROPERTY(Transient, ReplicatedUsing=OnRep_ServerState)
    FPodRacerState ServerState;

    UFUNCTION()
    void OnRep_ServerState();

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_SendMove(const FPodRacerMoveStruct& Move);

    TArray<FPodRacerMoveStruct> UnacknowledgedMoves;

    // --- Helpers ---
    FPodRacerMoveStruct CreateMove(float DeltaTime);
    void ClearAcknowledgedMoves(const FPodRacerMoveStruct& LastServerMove);
    class AReplicatedPodRacer* GetPodRacerOwner() const;
    class UBoxComponent* GetPhysicsBody() const;
    void ApplyHover(float DeltaTime, UBoxComponent* PhysicsBody);
    void ApplyPropulsion(const FPodRacerMoveStruct& Move, UBoxComponent* PhysicsBody);
    bool bWasOnGroundLastFrame = false;

    UPROPERTY(EditAnywhere, Category = "PodRacer|Hover") float LatencyThreshold = 0.1f; // 100ms
    float EstimatedLatency = 0.0f;
    bool bUseFixedHeightMode = false;
    void UpdateLatencyEstimation(float DeltaTime);

    float StartupDelayTimer = 1.0f;
};
*/