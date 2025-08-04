// Copyright 2024, Your Name/Company. All Rights Reserved.

#include "ProceduralTrackGenerator.h"
#include "Components/SplineComponent.h"
#include "ProceduralMeshComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "DestructibleBuildingActor.h" // You will need to create this class

AProceduralTrackGenerator::AProceduralTrackGenerator()
{
    // The actor can't tick, but it can run construction scripts in the editor.
    PrimaryActorTick.bCanEverTick = false;

    // Create the Spline Component and set it as the root.
    TrackSpline = CreateDefaultSubobject<USplineComponent>(TEXT("TrackSpline"));
    RootComponent = TrackSpline;

    // Create the Procedural Mesh Component.
    TrackMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TrackMesh"));
    TrackMesh->SetupAttachment(RootComponent);
}

void AProceduralTrackGenerator::BeginPlay()
{
    Super::BeginPlay();
}

// This function is called whenever the actor is moved or a property is changed in the editor.
void AProceduralTrackGenerator::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    // You could optionally call Generate() here for a live preview,
    // but it can be slow with many buildings. A button is often better.
}

void AProceduralTrackGenerator::Generate()
{
    // First, clear any previously generated content.
    ClearAll();

    // Create a random stream from the seed. This ensures the same seed always produces the same track.
    FRandomStream RandomStream(GenerationSeed);

    // Generate the core path of the track.
    GenerateSplinePoints(RandomStream);

    // Build the visible track geometry based on the new spline.
    GenerateTrackMesh();

    // Place destructible buildings alongside the track.
    PlaceBuildings(RandomStream);
}

void AProceduralTrackGenerator::ClearAll()
{
    // Clear the procedural mesh data.
    TrackMesh->ClearAllMeshSections();

    // Destroy all previously spawned building actors.
    for (AActor* Building : SpawnedBuildingActors)
    {
        if (Building)
        {
            Building->Destroy();
        }
    }
    SpawnedBuildingActors.Empty();

    // Clear the spline points, leaving just the default start and end.
    TrackSpline->ClearSplinePoints(true);
}

void AProceduralTrackGenerator::GenerateSplinePoints(FRandomStream& Stream)
{
    TrackSpline->ClearSplinePoints();

    FVector CurrentLocation = GetActorLocation();
    FRotator CurrentRotation = FRotator::ZeroRotator;

    // TODO: Add rotational and zaxis constraints based on adjustable properties

    for (int32 i = 0; i < NumberOfControlPoints; ++i)
    {
        // Add a new point at the current location.
        TrackSpline->AddSplinePoint(CurrentLocation, ESplineCoordinateSpace::World, false);

        // Determine the next location.
        const float Distance = Stream.FRandRange(MinPointDistance, MaxPointDistance);
        
        // Add some random rotation for variety in turns.
        const float YawChange = Stream.FRandRange(-MaxYawChange, MaxYawChange);
        const float PitchChange = Stream.FRandRange(-MaxPitchChange, MaxPitchChange);
        const float RollChange = Stream.FRandRange(-MaxRollChange, MaxRollChange);
        CurrentRotation += FRotator(PitchChange, YawChange, RollChange);
        
        // Move forward to the next point's location.
        FVector NextLocation = CurrentLocation + CurrentRotation.Vector() * Distance;
        float NextZDifference = NextLocation.Z - CurrentLocation.Z;
        if (FMath::Abs(NextZDifference) > MaxZOffsetOnNextPoint)
        {
            NextLocation.Z += (NextZDifference * -1);
            CurrentLocation = NextLocation;
        } else
        {
            CurrentLocation += CurrentRotation.Vector() * Distance;
        }
        // TODO: Should try to always tend back to the original Z axis level
    }

    // Close the loop to make it a continuous circuit.
    TrackSpline->SetClosedLoop(true, true);

    // Update the spline to finalize the shape.
    TrackSpline->UpdateSpline();
}

void AProceduralTrackGenerator::GenerateTrackMesh()
{
    TrackMesh->ClearAllMeshSections();
    if (TrackSpline->GetNumberOfSplinePoints() < 2) return;

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FProcMeshTangent> Tangents; // Not used in this basic example, but good practice.
    TArray<FColor> VertexColors;      // Not used in this basic example.

    const int32 NumSegments = TrackSpline->GetNumberOfSplinePoints();
    const float DistanceStep = TrackSpline->GetSplineLength() / (NumSegments * 10); // Increase density for smoother curves

    for (float Distance = 0; Distance <= TrackSpline->GetSplineLength(); Distance += DistanceStep)
    {
        const FVector Location = TrackSpline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::Local);
        const FVector Direction = TrackSpline->GetDirectionAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::Local);
        const FVector UpVector = TrackSpline->GetUpVectorAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::Local);
        
        const FVector RightVector = FVector::CrossProduct(Direction, UpVector).GetSafeNormal();

        // Add vertices for the left and right side of the track segment.
        Vertices.Add(Location - RightVector * TrackWidth / 2); // Left Vertex
        Vertices.Add(Location + RightVector * TrackWidth / 2); // Right Vertex

        // Add normals pointing up.
        Normals.Add(UpVector);
        Normals.Add(UpVector);

        // Add UVs. U is along the track, V is across it.
        UVs.Add(FVector2D(Distance / (TrackWidth * 2), 0));
        UVs.Add(FVector2D(Distance / (TrackWidth * 2), 1));
    }

    // Create triangles connecting the vertices.
    for (int32 i = 0; i < Vertices.Num() - 3; i += 2)
    {
        Triangles.Add(i);
        Triangles.Add(i + 2);
        Triangles.Add(i + 1);

        Triangles.Add(i + 1);
        Triangles.Add(i + 2);
        Triangles.Add(i + 3);
    }
    
    // Create the mesh section.
    TrackMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, true);
    TrackMesh->SetMaterial(0, TrackMaterial);
}

void AProceduralTrackGenerator::PlaceBuildings(FRandomStream& Stream)
{
    if (BuildingAssets.Num() == 0) return;

    for (float Distance = 0; Distance < TrackSpline->GetSplineLength(); Distance += BuildingSpacing)
    {
        // Get the transform at the current distance along the spline.
        const FTransform SplineTransform = TrackSpline->GetTransformAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
        const FVector RightVector = SplineTransform.GetUnitAxis(EAxis::Y);

        // Randomly decide to place on the left or right.
        const float Side = UKismetMathLibrary::RandomBoolFromStream(Stream) ? 1.0f : -1.0f;
        const float Offset = Stream.FRandRange(BuildingSideOffsetMin, BuildingSideOffsetMax);

        // Calculate the final spawn location for the building.
        const FVector SpawnLocation = SplineTransform.GetLocation() + (RightVector * (TrackWidth / 2 + Offset) * Side);
        
        // Choose a random building from the asset array.
        const int32 BuildingIndex = Stream.RandRange(0, BuildingAssets.Num() - 1);
        FDestructibleBuildingAsset& Asset = BuildingAssets[BuildingIndex];

        if (Asset.DestructibleActorClass)
        {
            // Spawn the destructible actor blueprint.
            FTransform SpawnTransform(SplineTransform.GetRotation(), SpawnLocation);
            
            // Add some random yaw rotation for variety.
            SpawnTransform.SetRotation(FQuat(FRotator(0, Stream.FRandRange(0, 360), 0)));

            ADestructibleBuildingActor* NewBuilding = GetWorld()->SpawnActor<ADestructibleBuildingActor>(Asset.DestructibleActorClass, SpawnTransform);
            if (NewBuilding)
            {
                // Store the reference so we can clean it up later.
                SpawnedBuildingActors.Add(NewBuilding);
            }
        }
    }
}
