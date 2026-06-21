// DungeonSpawner.h  — UE5 适配层 (M2)
//
// =====================================================================
//  【未验证脚手架 / UNVERIFIED SCAFFOLD】
//  本机未安装 UE5，本文件无法在此编译验证。它依赖 UE5 头文件。
//  一旦有 UE5 第三人称 C++ 工程：
//    1. 把本文件 + DungeonSpawner.cpp 放进 Source/<YourModule>/
//    2. 把引擎无关核心 dungeon.hpp、m2_adapter.hpp 一并拷入该模块目录，
//       且只在 .cpp 里 #include（不要进公开头文件），避免把 std/算法类型
//       推进 UE 反射头，也避开 unity-build / IWYU 的包含问题。
//    3. 见 M2_UE_README.md 的集成与验收步骤。
// =====================================================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DungeonSpawner.generated.h"

class UInstancedStaticMeshComponent;

UCLASS()
class ADungeonSpawner : public AActor
{
    GENERATED_BODY()

public:
    ADungeonSpawner();

    // M1 种子：换种子 = 换地牢。保持 int->uint64 直传给 std::mt19937_64，
    // 不要替换成 FRandomStream，否则同种子会得到不同地牢 (carry-forward: 确定性)。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    int32 Seed = 7;

    // 世界格子尺寸 = 网格步长 (cm)。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    float TileSize = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    float WallHeight = 200.f;

    // 是否在运行时生成覆盖整张图的 NavMeshBoundsVolume。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    bool bSpawnNavBounds = true;

    // 是否把现有第三人称角色挪到起点房间。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
    bool bTeleportPlayerToStart = true;

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;

    UFUNCTION(BlueprintCallable, Category = "Dungeon")
    void Build();

private:
    UPROPERTY() USceneComponent* Root = nullptr;
    UPROPERTY() UInstancedStaticMeshComponent* FloorISM = nullptr;
    UPROPERTY() UInstancedStaticMeshComponent* WallISM = nullptr;
    UPROPERTY() UInstancedStaticMeshComponent* CorridorISM = nullptr;
    UPROPERTY() UInstancedStaticMeshComponent* DoorISM = nullptr;

    // 起点世界坐标（供 teleport 用），Build() 时写入。
    FVector StartWorld = FVector::ZeroVector;
};
