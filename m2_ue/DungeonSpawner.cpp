// DungeonSpawner.cpp  — UE5 适配层 (M2)
//
// =====================================================================
//  【未验证脚手架 / UNVERIFIED SCAFFOLD】 —— 本机无 UE，未编译验证。
//  逻辑核心 (坐标映射 / 连通 / 确定性) 已由 m2_adapter.hpp + m2_adapter_test.cpp
//  在 g++ 下实证；本文件只负责把 spawn-plan 变成 UE 的 ISM 实例 + 碰撞 + 运行时导航。
//  集成后请按 M2_UE_README.md 跑 PIE 验收 (#1 构建 / #2 保真 / #3 可走 / #4 确定性)。
// =====================================================================

#include "DungeonSpawner.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Components/BrushComponent.h"
#include "Engine/World.h"

// 引擎无关核心：只在本 .cpp 内包含，避免 UE 反射头污染 / unity-build 冲突。
#include "m2_adapter.hpp"

ADungeonSpawner::ADungeonSpawner()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    auto MakeISM = [this](const TCHAR* Name) -> UInstancedStaticMeshComponent*
    {
        UInstancedStaticMeshComponent* Ism =
            CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
        Ism->SetupAttachment(Root);
        // 占位几何用引擎自带 Cube；实体碰撞，玩家不能穿墙。
        Ism->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Ism->SetCollisionProfileName(TEXT("BlockAll"));
        static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(
            TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (Cube.Succeeded())
        {
            Ism->SetStaticMesh(Cube.Object);
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
    Build();  // 编辑器里改 Seed/TileSize 即时重建，便于在关卡里预览
}

void ADungeonSpawner::BeginPlay()
{
    Super::BeginPlay();

    if (bTeleportPlayerToStart)
    {
        if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
        {
            // 抬高半个角色身位，避免出生卡进地板。
            Pawn->SetActorLocation(StartWorld + FVector(0, 0, 100.f));
        }
    }
}

void ADungeonSpawner::Build()
{
    if (!FloorISM) return;

    FloorISM->ClearInstances();
    WallISM->ClearInstances();
    CorridorISM->ClearInstances();
    DoorISM->ClearInstances();

    // ---- 调引擎无关核心：生成 Layout + spawn-plan ----
    dungeon::Config cfg;
    cfg.seed = static_cast<std::uint64_t>(Seed);   // mt19937_64 直传，确定性
    dungeon::Layout L = dungeon::generate(cfg);

    m2::WorldConfig wc;
    wc.tileSize   = static_cast<long long>(TileSize);
    wc.wallHeight = static_cast<long long>(WallHeight);
    const std::vector<m2::TilePlacement> Plan = m2::buildSpawnPlan(L, wc);

    // 占位 Cube 原始尺寸 100^3。floor/corridor/door 做成薄板，wall 做成柱体。
    const float XY = TileSize / 100.f;
    const float FloorThick = 0.1f;
    const float WallZ = WallHeight / 100.f;

    for (const m2::TilePlacement& P : Plan)
    {
        const FVector Loc(static_cast<float>(P.wx),
                          static_cast<float>(P.wy),
                          static_cast<float>(P.wz));
        const bool bWall = (P.kind == dungeon::Tile::Wall);
        const FVector Scale(XY, XY, bWall ? WallZ : FloorThick);
        const FTransform Xf(FRotator::ZeroRotator, Loc, Scale);

        switch (P.kind)
        {
            case dungeon::Tile::Floor:    FloorISM->AddInstance(Xf);    break;
            case dungeon::Tile::Corridor: CorridorISM->AddInstance(Xf); break;
            case dungeon::Tile::Door:     DoorISM->AddInstance(Xf);     break;
            case dungeon::Tile::Wall:     WallISM->AddInstance(Xf);     break;
        }
    }

    // 起点世界坐标（给 BeginPlay 的 teleport 用）。
    long long sx = 0, sy = 0;
    m2::startWorldPos(L, wc, sx, sy);
    StartWorld = FVector(static_cast<float>(sx), static_cast<float>(sy), 0.f);

    // ---- 保真度日志（验收 #2 的程序化检查：房间数 + 世界空间可达性）----
    const m2::WorldReach WR = m2::worldReachability(L, wc);
    UE_LOG(LogTemp, Display,
        TEXT("[Dungeon] seed=%d rooms=%d | world reach %d/%d cells, %d/%d rooms | fully-connected=%s | planHash=0x%llx"),
        Seed, static_cast<int>(L.rooms.size()),
        WR.reached, WR.passable, WR.roomsReached, WR.rooms,
        WR.fullyConnected() ? TEXT("YES") : TEXT("NO"),
        static_cast<unsigned long long>(m2::spawnPlanHash(L, wc)));

    // ---- 运行时导航 ----
    // 运行时 spawn 的几何，静态烘焙 navmesh 不覆盖，必须用 Dynamic 运行时生成：
    //   Project Settings > Navigation Mesh > Runtime Generation = Dynamic
    // 下面在运行时补一个覆盖整张图的 NavMeshBoundsVolume。
    // 注意：运行时设置 BoundsVolume 的盒体尺寸需要操作 Brush，较 fiddly；
    //       若不稳，改为在关卡里手放一个足够大的 NavMeshBoundsVolume（见 README）。
    if (bSpawnNavBounds)
    {
        if (UWorld* W = GetWorld())
        {
            const float MapW = L.config.width  * TileSize;
            const float MapH = L.config.height * TileSize;
            const FVector Center(MapW * 0.5f, MapH * 0.5f, WallHeight * 0.5f);

            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride =
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            ANavMeshBoundsVolume* Vol = W->SpawnActor<ANavMeshBoundsVolume>(
                ANavMeshBoundsVolume::StaticClass(), FTransform(Center), Params);

            if (Vol)
            {
                // 通过缩放近似覆盖（默认盒约 200^3）。精确做法见 README。
                Vol->SetActorScale3D(FVector(MapW / 200.f, MapH / 200.f, WallHeight / 100.f));
                if (UBrushComponent* Brush = Vol->GetBrushComponent())
                {
                    Brush->MarkRenderStateDirty();
                }
                if (UNavigationSystemV1* Nav =
                        FNavigationSystem::GetCurrent<UNavigationSystemV1>(W))
                {
                    Nav->OnNavigationBoundsUpdated(Vol);
                    Nav->Build();  // 触发运行时重建
                }
            }
        }
    }
}
