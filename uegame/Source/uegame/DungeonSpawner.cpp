// DungeonSpawner.cpp - M2 UE glue. See DungeonSpawner.h for the advisory list applied.
//
// The algorithmic core (coordinate mapping, world-space reachability, determinism hash)
// is engine-agnostic and independently verified by g++ (m2_adapter_test.cpp, 5000/5000
// seeds). This file only turns the verified spawn plan into ISM instances + collision +
// runtime navigation, and logs the fidelity numbers for the PIE evidence run.

#include "DungeonSpawner.h"

#include "AI/Navigation/NavigationDirtyArea.h"
#include "Combat/CombatConfig.h"
#include "Combat/DungeonEnemy.h"
#include "Combat/DungeonStairs.h"
#include "Combat/EncounterConfig.h"
#include "Combat/FloorManager.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
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
		Ism->SetCollisionObjectType(ECC_WorldStatic);   // explicit: ComputeLOSTo traces ECC_WorldStatic; "BlockAll" only implies it, so pin it (a profile change can't then silently stop walls blocking LOS; same value today -> nav/geometry unchanged)
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

	// On the bAutoStartRun path StartFloor(1) -> RegenerateFloor (next tick) is the sole
	// player placement, at the RESOLVED run seed. Teleporting here too would first drop the
	// player at the preview-seed start, then move them again next tick (a one-frame wrong
	// landing). Static designer spawners (bAutoStartRun=false) still place at BeginPlay.
	if (bTeleportPlayerToStart && !bAutoStartRun)
	{
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			// Floor slab top is ~+5; +100 clears the capsule half-height with margin.
			Pawn->SetActorLocation(StartWorld + FVector(0.0f, 0.0f, 100.0f));
			UE_LOG(LogTemp, Display, TEXT("[Placement] player at start (BeginPlay static path) seed=%llu"),
				static_cast<unsigned long long>(GetEffectiveSeed64()));
		}
	}

	// On the bAutoStartRun path the first floor's wave is owned by StartFloor(1) ->
	// RegenerateFloor one tick later, which despawns every enemy before respawning. Spawning
	// here too would only be a transient wave destroyed that same tick (double-spawn), so the
	// auto-start path skips it. Spawners with bAutoStartRun=false (static designer-placed
	// dungeons) still populate at BeginPlay.
	if (bSpawnEnemies && !bAutoStartRun)
	{
		if (UWorld* World = GetWorld(); World && World->IsGameWorld())
		{
			SpawnEnemies();
		}
	}

	if (UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		SpawnStairs();   // M4: descend trigger in the farthest room (no-op until a run is active)

		// M4: gameplay entry into the floor loop. Deferred one tick so the player pawn and
		// the GameInstance subsystem are guaranteed up; re-checked at fire time because a
		// run may have been started by then (e.g. the FloorManager's own helper spawner).
		if (bAutoStartRun)
		{
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateWeakLambda(this, [this]()
				{
					UUegameFloorManager* FM = UUegameFloorManager::Get(GetWorld());
					if (bAutoStartRun && FM && !FM->IsRunActive())
					{
						// The run seed comes from the single resolver (entropy by default),
						// NOT this spawner's editor Seed - a placed spawner is the shipping
						// ENTRY, not a seed source, so a fresh install gets a random first run.
						// GetEffectiveSeed64() stays a geometry/enemy-plan seed only (Build,
						// SpawnEnemies), used for the transient pre-run preview build.
						const uint64 RunSeed = FM->ResolveFirstRunSeed();
						UE_LOG(LogTemp, Display,
							TEXT("[Dungeon] AutoStartRun: runSeed=%llu (placed spawner path)"),
							static_cast<unsigned long long>(RunSeed));
						FM->StartRun(RunSeed);
					}
				}));
		}
	}
}

void ADungeonSpawner::SpawnEnemies(int32 InEnemiesPerRoomOverride, float InEnemyHPOverride)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();   // echoes the row once (evidence)

	// M4: per-floor scaling arrives as overrides (<0 = base values from the DataTable row).
	const int32 EffPerRoom = (InEnemiesPerRoomOverride >= 0) ? InEnemiesPerRoomOverride : Cfg.EnemiesPerRoom;
	FCombatConfigRow EffCfg = Cfg;
	if (InEnemyHPOverride >= 0.0f)
	{
		EffCfg.EnemyMaxHP = InEnemyHPOverride;
	}

	// Same deterministic pipeline as the geometry: regenerate the layout for this seed
	// and derive placements from the seeded sub-stream. Start room excluded (deliberate
	// contract deviation, surfaced at M3 GATE 1): the player spawns there un-ambushed, and
	// rooms with zero initial enemies never fire RoomCleared.
	dungeon::Config DCfg;
	DCfg.seed = GetEffectiveSeed64();
	const dungeon::Layout Layout = dungeon::generate(DCfg);

	m2::WorldConfig WC;
	WC.tileSize   = static_cast<long long>(TileSize);
	WC.wallHeight = static_cast<long long>(WallHeight);
	const std::vector<m2::EnemyPlacement> Plan =
		m2::buildEnemyPlan(Layout, WC, EffPerRoom, Layout.startRoom);

	RoomAliveCounts.Init(0, static_cast<int32>(Layout.rooms.size()));
	RoomInitialCounts.Init(0, static_cast<int32>(Layout.rooms.size()));

	// --- M6B: composition-only assignment over the FROZEN plan. The plan itself (count,
	// order, room indices, gx/gy/wx/wy) is never touched - the m2 anchors cached here must
	// stay byte-identical to the pre-M6 baseline. Roles/types come from the m6 core fed by
	// (runSeed, floorIndex); outside an active run (editor preview, static spawners) the
	// assignment is skipped and every enemy keeps the Default row exactly as before M6.
	CachedSpawnPlanHash = m2::spawnPlanHash(Layout, WC);
	CachedEnemyPlanHash = m2::enemyPlanHash(Plan);
	bEncounterAssigned = false;
	CachedRoomRoles.Reset();
	CachedRoomTypeCounts.Reset();
	CachedTypeTally = FIntVector::ZeroValue;
	CachedRoomRoleHash = 0;
	CachedEnemyTypeHash = 0;
	EncounterHpMult = (Cfg.EnemyMaxHP > 0.0f) ? (EffCfg.EnemyMaxHP / Cfg.EnemyMaxHP) : 1.0f;
	TArray<int32> EnemyTypes;
	UUegameFloorManager* FM = UUegameFloorManager::Get(World);
	if (FM && FM->IsRunActive() && FUegameEncounterConfig::IsAvailable())
	{
		TArray<int32> PlacementRooms;
		PlacementRooms.Reserve(static_cast<int32>(Plan.size()));
		for (const m2::EnemyPlacement& P : Plan)
		{
			PlacementRooms.Add(P.roomIndex);
		}
		TArray<int32> RoomRoles;
		uint64 RoleHash = 0, TypeHash = 0;
		if (FUegameEncounterConfig::AssignForFloor(
				FM->GetRunSeed(), FM->GetFloorIndex(), PlacementRooms,
				static_cast<int32>(Layout.rooms.size()), Layout.startRoom,
				RoomRoles, EnemyTypes, RoleHash, TypeHash))
		{
			bEncounterAssigned = true;
			EncounterRunSeed = FM->GetRunSeed();
			EncounterFloorIndex = FM->GetFloorIndex();
			CachedRoomRoles = MoveTemp(RoomRoles);
			CachedRoomRoleHash = RoleHash;
			CachedEnemyTypeHash = TypeHash;
			CachedRoomTypeCounts.Init(FIntVector::ZeroValue, static_cast<int32>(Layout.rooms.size()));
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[EncounterPlan] assignment failed (inconsistent inputs) - spawning Default-only"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Display,
			TEXT("[EncounterPlan] skipped (%s) - spawning Default-only"),
			(FM && FM->IsRunActive()) ? TEXT("encounter tables unavailable") : TEXT("no active run"));
	}

	int32 Spawned = 0;
	TArray<int32> SpawnedTypes;   // M6B: type of each enemy that ACTUALLY spawned, plan order
	SpawnedTypes.Reserve(static_cast<int32>(Plan.size()));
	for (int32 PlanIdx = 0; PlanIdx < static_cast<int32>(Plan.size()); ++PlanIdx)
	{
		const m2::EnemyPlacement& P = Plan[PlanIdx];
		const FVector Loc(static_cast<float>(P.wx), static_cast<float>(P.wy), 100.0f);
		ADungeonEnemy* Enemy = World->SpawnActorDeferred<ADungeonEnemy>(
			ADungeonEnemy::StaticClass(), FTransform(Loc), nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Enemy)
		{
			continue;
		}
		Enemy->InitEnemy(EffCfg, P.roomIndex, this);
		if (bEncounterAssigned && EnemyTypes.IsValidIndex(PlanIdx))
		{
			const int32 T = EnemyTypes[PlanIdx];
			Enemy->ApplyArchetype(FUegameEncounterConfig::GetArchetype(T), EncounterHpMult,
			                      FUegameEncounterConfig::TypeName(T));
			if (CachedRoomTypeCounts.IsValidIndex(P.roomIndex) && T >= 0 && T < FUegameEncounterConfig::NumTypes)
			{
				CachedRoomTypeCounts[P.roomIndex][T] += 1;
				CachedTypeTally[T] += 1;
			}
			SpawnedTypes.Add(T);
		}
		Enemy->FinishSpawning(FTransform(Loc));
		++RoomAliveCounts[P.roomIndex];
		++RoomInitialCounts[P.roomIndex];
		++Spawned;
	}

	// M6B: the cached hash must describe the roster the evidence verbs print - i.e. what
	// actually spawned. On a full spawn (the normal case: AdjustIfPossibleButAlwaysSpawn)
	// this equals the planned-assignment hash and therefore the golden anchor; if a deferred
	// spawn ever fails, the hash follows the spawned sequence and the drift is logged loudly
	// instead of reporting a hash the printed tally cannot reproduce (Codex PR #16 P3).
	if (bEncounterAssigned && SpawnedTypes.Num() != EnemyTypes.Num())
	{
		CachedEnemyTypeHash = FUegameEncounterConfig::TypeSequenceHash(SpawnedTypes);
		UE_LOG(LogTemp, Warning,
			TEXT("[EncounterPlan] PARTIAL SPAWN planned=%d spawned=%d - enemyTypeHash now covers the spawned sequence only (golden parity not applicable)"),
			EnemyTypes.Num(), SpawnedTypes.Num());
	}

	// Evidence (acceptance B): deterministic enemy plan - tile-invariant hash must equal
	// the g++ value for the same seed and reproduce across PIE sessions.
	UE_LOG(LogTemp, Display,
		TEXT("[Dungeon] enemyPlan seed=%llu perRoom=%d enemies=%d spawned=%d startRoomExcluded=%d hash=0x%llx effHP=%.0f"),
		static_cast<unsigned long long>(GetEffectiveSeed64()),
		EffPerRoom, static_cast<int32>(Plan.size()), Spawned,
		StartRoomIndex,
		static_cast<unsigned long long>(CachedEnemyPlanHash),
		EffCfg.EnemyMaxHP);

	// M6B evidence: the two NEW anchors + role/type composition, formatted to diff against
	// golden_m6_encounter_seed7.txt field-for-field. Only logged when the assignment ran.
	if (bEncounterAssigned)
	{
		FString RolesStr;
		int32 RoleTally[FUegameEncounterConfig::NumRoles] = { 0, 0, 0, 0 };
		for (const int32 R : CachedRoomRoles)
		{
			RolesStr += FUegameEncounterConfig::RoleName(R);
			RolesStr += TEXT(" ");
			if (R >= 0 && R < FUegameEncounterConfig::NumRoles)
			{
				++RoleTally[R];
			}
		}
		RolesStr.TrimEndInline();
		UE_LOG(LogTemp, Display,
			TEXT("[EncounterPlan] runSeed=%llu floor=%d floorSeed=%llu enemies=%d roles=%s roleTally: Quiet=%d Standard=%d Skirmish=%d Stronghold=%d typeTally: Grunt=%d Runner=%d Brute=%d roomRoleHash=0x%llx enemyTypeHash=0x%llx spawnPlanHash=0x%llx enemyPlanHash=0x%llx"),
			static_cast<unsigned long long>(EncounterRunSeed), EncounterFloorIndex,
			static_cast<unsigned long long>(GetEffectiveSeed64()), Spawned,
			*RolesStr, RoleTally[0], RoleTally[1], RoleTally[2], RoleTally[3],
			CachedTypeTally.X, CachedTypeTally.Y, CachedTypeTally.Z,
			static_cast<unsigned long long>(CachedRoomRoleHash),
			static_cast<unsigned long long>(CachedEnemyTypeHash),
			static_cast<unsigned long long>(CachedSpawnPlanHash),
			static_cast<unsigned long long>(CachedEnemyPlanHash));
		for (int32 RoomIdx = 0; RoomIdx < CachedRoomRoles.Num(); ++RoomIdx)
		{
			const FIntVector C = CachedRoomTypeCounts.IsValidIndex(RoomIdx)
				? CachedRoomTypeCounts[RoomIdx] : FIntVector::ZeroValue;
			UE_LOG(LogTemp, Display,
				TEXT("[EncounterRoom] room=%d role=%s Grunt=%d Runner=%d Brute=%d"),
				RoomIdx, FUegameEncounterConfig::RoleName(CachedRoomRoles[RoomIdx]), C.X, C.Y, C.Z);
		}
	}
}

void ADungeonSpawner::NotifyEnemyDead(int32 InRoomIndex)
{
	if (!RoomAliveCounts.IsValidIndex(InRoomIndex))
	{
		return;
	}
	RoomAliveCounts[InRoomIndex] = FMath::Max(0, RoomAliveCounts[InRoomIndex] - 1);
	UE_LOG(LogTemp, Display, TEXT("[Combat] enemy down in room=%d, alive=%d"),
		InRoomIndex, RoomAliveCounts[InRoomIndex]);

	// Rooms that never had enemies (e.g. the start room) never fire RoomCleared.
	if (RoomAliveCounts[InRoomIndex] == 0 && RoomInitialCounts[InRoomIndex] > 0)
	{
		// Evidence (acceptance E).
		UE_LOG(LogTemp, Display, TEXT("[RoomClear] room=%d cleared (initial=%d)"),
			InRoomIndex, RoomInitialCounts[InRoomIndex]);

		// If that was the last enemy room, the require-floor-clear gate just opened. The stairs
		// overlap is edge-triggered but the gate is level-triggered, so a player standing on the
		// pad when the final enemy falls would otherwise stay stuck until stepping off and back
		// on. Re-poke the stairs to descend in place. Harmless under descend-anytime (the player
		// would already have descended on overlap and the pad is gone).
		if (AreAllRoomsCleared())
		{
			if (UWorld* World = GetWorld())
			{
				// M5: offer the floor-clear reward BEFORE re-poking the stairs. NotifyFloorCleared() sets
				// RewardPending (unless this is the final floor), so the re-poke below is intentionally
				// blocked by the reward gate until the player picks - the reward can't be skipped by the
				// auto-descend for a player already standing on the pad.
				if (UUegameFloorManager* FM = UUegameFloorManager::Get(World))
				{
					FM->NotifyFloorCleared();
				}
				for (TActorIterator<ADungeonStairs> It(World); It; ++It)
				{
					It->OnFloorCleared();
				}
			}
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
	Cfg.seed = GetEffectiveSeed64();   // determinism carry-forward: mt19937_64; 64-bit for M4 floor seeds
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

	// All room centers in world coords (M3 room-clear / teleport verbs) + farthest room
	// (2D from start) for the WalkFar evidence command.
	StartRoomIndex = Layout.startRoom;
	RoomCentersWorld.Reset();
	FarthestRoomWorld = StartWorld;
	FarthestRoomIndex = StartRoomIndex;
	float BestDist = -1.0f;
	for (const dungeon::Room& R : Layout.rooms)
	{
		const FVector C(
			static_cast<float>(WC.originX + static_cast<long long>(R.cx()) * WC.tileSize + WC.tileSize / 2),
			static_cast<float>(WC.originY + static_cast<long long>(R.cy()) * WC.tileSize + WC.tileSize / 2),
			0.0f);
		RoomCentersWorld.Add(C);
		const float D = FVector::Dist2D(C, StartWorld);
		if (D > BestDist)
		{
			BestDist = D;
			FarthestRoomWorld = C;
			FarthestRoomIndex = RoomCentersWorld.Num() - 1;
		}
	}

	// --- Fidelity log (the programmatic half of acceptance #2) ---
	const m2::WorldReach WR = m2::worldReachability(Layout, WC);
	const int32 SpawnedWalkable =
		FloorISM->GetInstanceCount() + CorridorISM->GetInstanceCount() + DoorISM->GetInstanceCount();
	const int32 SpawnedTotal = SpawnedWalkable + WallISM->GetInstanceCount();

	UE_LOG(LogTemp, Display,
		TEXT("[Dungeon] seed=%llu rooms=%d connections=%d | spawned %d instances (%d walkable == %d plan-passable) | world reach %d/%d cells, %d/%d rooms | fully-connected=%s | planHash=0x%llx"),
		static_cast<unsigned long long>(GetEffectiveSeed64()),
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

	// Advisory #3, resolved. A runtime-spawned brush volume has no brush geometry, so its
	// bounds are empty. Worse, the nav system registers the volume during spawn
	// (PostRegisterAllComponents -> OnNavigationBoundsAdded) - attaching geometry AFTER
	// SpawnActor registers EMPTY bounds ("1 empty bounds" in LogNavigationDirtyArea) and
	// the navmesh never builds. Fix: deferred spawn, attach a map-sized UBoxComponent
	// (the nav system reads GetComponentsBoundingBox(true), NavigationSystem.cpp:4145)
	// BEFORE FinishSpawning, so registration sees the real bounds in one step.
	ANavMeshBoundsVolume* Vol = World->SpawnActorDeferred<ANavMeshBoundsVolume>(
		ANavMeshBoundsVolume::StaticClass(), FTransform(Center), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
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
	UBoxComponent* BoundsBox = NewObject<UBoxComponent>(Vol, TEXT("DungeonNavBoundsBox"));
	BoundsBox->SetupAttachment(Vol->GetRootComponent());
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsBox->SetBoxExtent(FVector(MapW * 0.5f, MapH * 0.5f, FMath::Max(WallHeight, 200.0f)));
	Vol->AddInstanceComponent(BoundsBox);
	Vol->FinishSpawning(FTransform(Center));
	if (!BoundsBox->IsRegistered())
	{
		BoundsBox->RegisterComponent();
	}

	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		Nav->OnNavigationBoundsUpdated(Vol);   // refresh with the final bounds
	}

	// The dungeon ISM geometry registered BEFORE this volume existed, so its dirty
	// areas were dropped as out-of-bounds (LogNavigationDirtyArea "Skipped ... empty
	// bounds"). Re-dirty the whole map explicitly so the generator rebuilds every
	// tile now that the bounds are in place.
	RefreshNavigation();

	const FBox VolBounds = Vol->GetComponentsBoundingBox(true);
	UE_LOG(LogTemp, Display,
		TEXT("[Dungeon] runtime NavMeshBoundsVolume at (%.0f, %.0f); bounds valid=%s size=(%.0f x %.0f x %.0f). If invalid/zero, hand-place a volume (README)."),
		Center.X, Center.Y,
		VolBounds.IsValid ? TEXT("YES") : TEXT("NO"),
		VolBounds.GetSize().X, VolBounds.GetSize().Y, VolBounds.GetSize().Z);
}

void ADungeonSpawner::SpawnStairs()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	// The pad only forwards to RequestDescend(), which no-ops without an active run - so
	// outside a run (BeginPlay before AutoStartRun fires, forensic Dungeon.Regen) spawning
	// it would present a dead trigger. Single enforcement point for every call site.
	const UUegameFloorManager* FM = UUegameFloorManager::Get(World);
	if (!FM || !FM->IsRunActive())
	{
		UE_LOG(LogTemp, Display, TEXT("[Stairs] skipped: no active run (pad would be inert)"));
		return;
	}

	// One pad per floor: drop the previous floor's stairs first.
	for (TActorIterator<ADungeonStairs> It(World); It; ++It)
	{
		It->Destroy();
	}

	const FVector Loc = FarthestRoomWorld + FVector(0.0f, 0.0f, 100.0f);
	ADungeonStairs* Stairs = World->SpawnActor<ADungeonStairs>(ADungeonStairs::StaticClass(), Loc, FRotator::ZeroRotator);
	if (!Stairs)
	{
		UE_LOG(LogTemp, Error, TEXT("[Stairs] spawn failed"));
		return;
	}

	const FCombatConfigRow& Cfg = FUegameCombatConfig::Get();
	// Evidence: active gate policy logged at spawn (config flag, DataTable-driven).
	UE_LOG(LogTemp, Display,
		TEXT("[Stairs] spawned in farthest room=%d at (%.0f, %.0f) policy=%s"),
		FarthestRoomIndex, Loc.X, Loc.Y,
		Cfg.bRequireFloorClearToDescend ? TEXT("require-floor-clear") : TEXT("descend-anytime"));
}

void ADungeonSpawner::RefreshNavigation()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		// Grid extent is fixed (64x40 cells), so the registered nav bounds stay valid across
		// floor transitions; marking the whole map dirty makes the dynamic navmesh rebuild
		// against the NEW geometry (M2 pattern).
		const dungeon::Config Cfg;
		const float MapW = Cfg.width  * TileSize;
		const float MapH = Cfg.height * TileSize;
		const FBox MapBox(
			FVector(-100.0f, -100.0f, -200.0f),
			FVector(MapW + 100.0f, MapH + 100.0f, WallHeight * 2.0f));
		Nav->AddDirtyArea(MapBox, ENavigationDirtyFlag::All, TEXT("DungeonSpawner floor rebuild"));
	}
}

void ADungeonSpawner::RegenerateFloor(uint64 NewSeed, int32 InEnemiesPerRoomOverride, float InEnemyHPOverride)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("[Dungeon] RegenerateFloor is game-world only"));
		return;
	}

	// Despawn floor-N enemies silently (Destroy path skips HandleDeath, so no RoomClear noise).
	int32 Despawned = 0;
	for (TActorIterator<ADungeonEnemy> It(World); It; ++It)
	{
		It->Destroy();
		++Despawned;
	}

	SetSeed64(NewSeed);
	Build();                 // ClearInstances + rebuild geometry from the new layout
	RefreshNavigation();     // re-dirty the whole map so the navmesh rebuilds in place

	if (bTeleportPlayerToStart)
	{
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			Pawn->SetActorLocation(StartWorld + FVector(0.0f, 0.0f, 100.0f));
			UE_LOG(LogTemp, Display, TEXT("[Placement] player at start (RegenerateFloor) seed=%llu"),
				static_cast<unsigned long long>(NewSeed));
		}
	}
	if (bSpawnEnemies)
	{
		SpawnEnemies(InEnemiesPerRoomOverride, InEnemyHPOverride);
	}
	SpawnStairs();           // M4: fresh descend trigger in the new farthest room

	UE_LOG(LogTemp, Display,
		TEXT("[Dungeon] RegenerateFloor: seed=%llu despawned=%d (in-place, world+navsystem kept alive)"),
		static_cast<unsigned long long>(NewSeed), Despawned);
}
