// DungeonSpawner.cpp - M2 UE glue. See DungeonSpawner.h for the advisory list applied.
//
// The algorithmic core (coordinate mapping, world-space reachability, determinism hash)
// is engine-agnostic and independently verified by g++ (m2_adapter_test.cpp, 5000/5000
// seeds). This file only turns the verified spawn plan into ISM instances + collision +
// runtime navigation, and logs the fidelity numbers for the PIE evidence run.

#include "DungeonSpawner.h"

#include "Components/BrushComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "UObject/ConstructorHelpers.h"

// Engine-agnostic M1/M2 core, resolved via the repo-root PrivateIncludePaths entry in
// uegame.Build.cs. Included ONLY in this .cpp - never in a reflected UE header - so
// std/algorithm types stay out of UHT's view and unity builds cannot leak them around.
#include "m2_adapter.hpp"

ADungeonSpawner::ADungeonSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// Advisory #4: a single plain FObjectFinder at constructor scope (valid only during
	// CDO construction; no static-in-lambda).
	ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* CubeMesh = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;

	auto MakeISM = [this, CubeMesh](const TCHAR* Name) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* Ism = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Ism->SetupAttachment(Root);
		Ism->SetCollisionProfileName(TEXT("BlockAll"));   // solid: no walking through walls
		Ism->SetCanEverAffectNavigation(true);            // include in navmesh generation
		if (CubeMesh)
		{
			Ism->SetStaticMesh(CubeMesh);
		}
		return Ism;
	};

	FloorISM    = MakeISM(TEXT("FloorISM"));
	WallISM     = MakeISM(TEXT("WallISM"));
	CorridorISM = MakeISM(TEXT("CorridorISM"));
	DoorISM     = MakeISM(TEXT("DoorISM"));
}

void ADungeonSpawner::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Geometry only, for live editor preview when Seed/TileSize change.
	// Advisory #2: no actor spawning here - OnConstruction reruns on every edit/move/load.
	Build();
}

void ADungeonSpawner::BeginPlay()
{
	Super::BeginPlay();

	// OnConstruction has already built geometry for both level-placed and runtime-spawned
	// actors; rebuild defensively only if empty.
	if (FloorISM && WallISM && FloorISM->GetInstanceCount() == 0 && WallISM->GetInstanceCount() == 0)
	{
		Build();
	}

	if (bSpawnNavBounds)
	{
		SpawnNavBounds();   // advisory #2: game worlds only, from BeginPlay
	}

	if (bTeleportPlayerToStart)
	{
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			// Floor slab top is ~+5; +100 clears the capsule half-height with margin.
			Pawn->SetActorLocation(StartWorld + FVector(0.0f, 0.0f, 100.0f));
		}
	}
}

void ADungeonSpawner::Build()
{
	if (!FloorISM || !WallISM || !CorridorISM || !DoorISM)
	{
		return;
	}

	FloorISM->ClearInstances();
	WallISM->ClearInstances();
	CorridorISM->ClearInstances();
	DoorISM->ClearInstances();

	// --- Engine-agnostic core: generate the Layout and its spawn plan ---
	dungeon::Config Cfg;
	Cfg.seed = static_cast<std::uint64_t>(Seed);   // determinism carry-forward: mt19937_64
	const dungeon::Layout Layout = dungeon::generate(Cfg);

	m2::WorldConfig WC;
	WC.tileSize   = static_cast<long long>(TileSize);
	WC.wallHeight = static_cast<long long>(WallHeight);
	const std::vector<m2::TilePlacement> Plan = m2::buildSpawnPlan(Layout, WC);

	// The engine's basic Cube is 100x100x100 around its origin; scale to the tile footprint.
	const float XYScale    = TileSize / 100.0f;
	const float FloorScale = 0.1f;                  // thin walkable slab, top at z ~= +5
	const float WallScale  = WallHeight / 100.0f;

	for (const m2::TilePlacement& P : Plan)
	{
		const FVector Loc(static_cast<float>(P.wx), static_cast<float>(P.wy), static_cast<float>(P.wz));
		const bool bIsWall = (P.kind == dungeon::Tile::Wall);
		const FVector Scale(XYScale, XYScale, bIsWall ? WallScale : FloorScale);
		const FTransform Xf(FRotator::ZeroRotator, Loc, Scale);

		// Advisory #5: bWorldSpace=true - the dungeon occupies the absolute world
		// coordinates produced by the m2 mapping no matter where this actor sits, so
		// StartWorld (absolute) stays consistent even if a designer moves the spawner.
		switch (P.kind)
		{
		case dungeon::Tile::Floor:    FloorISM->AddInstance(Xf, /*bWorldSpace=*/true);    break;
		case dungeon::Tile::Corridor: CorridorISM->AddInstance(Xf, /*bWorldSpace=*/true); break;
		case dungeon::Tile::Door:     DoorISM->AddInstance(Xf, /*bWorldSpace=*/true);     break;
		case dungeon::Tile::Wall:     WallISM->AddInstance(Xf, /*bWorldSpace=*/true);     break;
		}
	}

	long long SX = 0;
	long long SY = 0;
	m2::startWorldPos(Layout, WC, SX, SY);
	StartWorld = FVector(static_cast<float>(SX), static_cast<float>(SY), 0.0f);

	// Farthest room center (2D) from the start room, for the WalkFar evidence command.
	FarthestRoomWorld = StartWorld;
	float BestDist = -1.0f;
	for (const dungeon::Room& R : Layout.rooms)
	{
		const FVector C(
			static_cast<float>(WC.originX + static_cast<long long>(R.cx()) * WC.tileSize + WC.tileSize / 2),
			static_cast<float>(WC.originY + static_cast<long long>(R.cy()) * WC.tileSize + WC.tileSize / 2),
			0.0f);
		const float D = FVector::Dist2D(C, StartWorld);
		if (D > BestDist)
		{
			BestDist = D;
			FarthestRoomWorld = C;
		}
	}

	// --- Fidelity log (the programmatic half of acceptance #2) ---
	const m2::WorldReach WR = m2::worldReachability(Layout, WC);
	const int32 SpawnedWalkable =
		FloorISM->GetInstanceCount() + CorridorISM->GetInstanceCount() + DoorISM->GetInstanceCount();
	const int32 SpawnedTotal = SpawnedWalkable + WallISM->GetInstanceCount();

	UE_LOG(LogTemp, Display,
		TEXT("[Dungeon] seed=%d rooms=%d connections=%d | spawned %d instances (%d walkable == %d plan-passable) | world reach %d/%d cells, %d/%d rooms | fully-connected=%s | planHash=0x%llx"),
		Seed,
		static_cast<int32>(Layout.rooms.size()),
		static_cast<int32>(Layout.connections.size()),
		SpawnedTotal,
		SpawnedWalkable,
		WR.passable,
		WR.reached, WR.passable,
		WR.roomsReached, WR.rooms,
		WR.fullyConnected() ? TEXT("YES") : TEXT("NO"),
		static_cast<unsigned long long>(m2::spawnPlanHash(Layout, WC)));
}

void ADungeonSpawner::SpawnNavBounds()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;   // advisory #2: never spawn volumes into editor worlds
	}

	// Same defaults Build() used (dungeon::Config defaults + this actor's tile settings).
	const dungeon::Config Cfg;
	const float MapW = Cfg.width  * TileSize;
	const float MapH = Cfg.height * TileSize;
	const FVector Center(MapW * 0.5f, MapH * 0.5f, WallHeight * 0.5f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ANavMeshBoundsVolume* Vol = World->SpawnActor<ANavMeshBoundsVolume>(
		ANavMeshBoundsVolume::StaticClass(), FTransform(Center), Params);
	if (!Vol)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Dungeon] NavMeshBoundsVolume spawn failed; hand-place one instead (see m2_ue/M2_UE_README.md)"));
		return;
	}

	if (USceneComponent* VolRoot = Vol->GetRootComponent())
	{
		VolRoot->SetMobility(EComponentMobility::Movable);
	}

	// Advisory #3 caveat: a runtime-spawned brush volume has no brush geometry, so scaling
	// may still yield empty bounds. We log the measured bounds so the PIE evidence shows
	// unambiguously whether this runtime volume worked or the hand-placed fallback is needed.
	Vol->SetActorScale3D(FVector(MapW / 200.0f, MapH / 200.0f, FMath::Max(1.0f, WallHeight / 100.0f)));

	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		Nav->OnNavigationBoundsUpdated(Vol);
	}

	const FBox VolBounds = Vol->GetComponentsBoundingBox(true);
	UE_LOG(LogTemp, Display,
		TEXT("[Dungeon] runtime NavMeshBoundsVolume at (%.0f, %.0f); bounds valid=%s size=(%.0f x %.0f x %.0f). If invalid/zero, hand-place a volume (README)."),
		Center.X, Center.Y,
		VolBounds.IsValid ? TEXT("YES") : TEXT("NO"),
		VolBounds.GetSize().X, VolBounds.GetSize().Y, VolBounds.GetSize().Z);
}
