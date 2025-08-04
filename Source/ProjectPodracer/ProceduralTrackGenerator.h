// Copyright 2024, Your Name/Company. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralTrackGenerator.generated.h"

// Forward declarations
class USplineComponent;
class UProceduralMeshComponent;
class ADestructibleBuildingActor;

// A simple struct to pair an intact mesh with its destructible counterpart.
// This makes it easy to manage assets in the editor.
USTRUCT(BlueprintType)
struct FDestructibleBuildingAsset
{
    GENERATED_BODY()

    // The static mesh for the building when it's intact.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assets")
    TSubclassOf<AActor> IntactBuildingClass;

    // The Blueprint or C++ class that handles the destruction logic (e.g., contains a Geometry Collection).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assets")
    TSubclassOf<ADestructibleBuildingActor> DestructibleActorClass;
};


UCLASS()
class PROJECTPODRACER_API AProceduralTrackGenerator : public AActor
{
    GENERATED_BODY()

public:
    // Sets default values for this actor's properties
    AProceduralTrackGenerator();

protected:
    // Called when the game starts or when spawned
    virtual void BeginPlay() override;
    virtual void OnConstruction(const FTransform& Transform) override;

public:
    // The main spline component that will define the path of the race track.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USplineComponent* TrackSpline;

    // The procedural mesh component that will be used to construct the track's visual mesh.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    UProceduralMeshComponent* TrackMesh;

    // --- Generation Parameters ---

    // The seed for the random number generator to ensure tracks can be replicated.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Generation")
    int32 GenerationSeed = 12345;

    // The number of control points for the spline. More points = more complex track.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Generation", meta = (ClampMin = "4", UIMin = "4"))
    int32 NumberOfControlPoints = 10;

    // The maximum distance a control point can be from the previous one.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Generation", meta = (ClampMin = "1000.0"))
    float MaxPointDistance = 10000.0f;

    // The minimum distance a control point can be from the previous one.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Generation", meta = (ClampMin = "500.0"))
    float MinPointDistance = 5000.0f;

    // The width of the racetrack mesh.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Generation", meta = (ClampMin = "100.0"))
    float TrackWidth = 1500.0f;

    // The Max Yaw Change on a new spline point
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Generation", meta = (ClampMin = "90.0"))
    float MaxYawChange = 45.0f;

    // The Max Pitch Change on a new spline point
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Generation", meta = (ClampMin = "90.0"))
    float MaxPitchChange = 15.0f;

    // The Max Roll Change on a new spline point
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Generation", meta = (ClampMin = "90.0"))
    float MaxRollChange = 0.0f;

    // The Max Z Location Offset between points
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Generation")
    float MaxZOffsetOnNextPoint = 0.0f;
    
    // The material to apply to the generated track mesh.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Procedural Generation")
    UMaterialInterface* TrackMaterial;

    // --- Building Placement ---

    // An array of available building assets that can be placed along the track.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building Placement")
    TArray<FDestructibleBuildingAsset> BuildingAssets;

    // The distance between each potential building placement along the spline.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building Placement", meta = (ClampMin = "100.0"))
    float BuildingSpacing = 2000.0f;

    // The minimum distance a building can be placed from the edge of the track.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building Placement", meta = (ClampMin = "0.0"))
    float BuildingSideOffsetMin = 200.0f;

    // The maximum distance a building can be placed from the edge of the track.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building Placement", meta = (ClampMin = "0.0"))
    float BuildingSideOffsetMax = 1000.0f;

    // --- Editor Functions ---

    // Main function to generate the entire track and place buildings.
    UFUNCTION(CallInEditor, Category = "Procedural Generation")
    void Generate();

    // Clears all generated components (track mesh and buildings).
    UFUNCTION(CallInEditor, Category = "Procedural Generation")
    void ClearAll();

private:
    // Helper function to generate the spline control points.
    void GenerateSplinePoints(FRandomStream& Stream);

    // Helper function to build the track mesh from the spline.
    void GenerateTrackMesh();
    
    // Helper function to place buildings along the track.
    void PlaceBuildings(FRandomStream& Stream);

    // References to spawned actors to allow for easy cleanup.
    UPROPERTY()
    TArray<AActor*> SpawnedBuildingActors;
};
