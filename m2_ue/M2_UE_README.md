# M2 — UE5 集成说明

把 M1 的抽象 `Layout` 渲染成可行走的 UE5 第三人称关卡。

## 状态:代码已落地进 UE 工程,待 UE 工具链编译验证

Spawner 已从本目录的脚手架**落地**到 UE 模块源码里;本目录现在只保留这份文档
(旧的 `DungeonSpawner.h/.cpp` 脚手架已删除,历史见 git)。

| 部分 | 位置 | 状态 |
|---|---|---|
| 引擎无关核心(生成/校验) | `../dungeon.hpp` | ✅ g++ 实证(>120k 次 generate+validate,0 失败) |
| 引擎无关适配(坐标映射/世界连通/确定性哈希) | `../m2_adapter.hpp` + `../m2_adapter_test.cpp` | ✅ g++ 实证(5000/5000 世界连通与网格一致;哈希稳定) |
| UE 胶水(ISM/碰撞/运行时导航/玩家落点) | `../uegame/Source/uegame/DungeonSpawner.h` `.cpp` | ✅ milestone A 已过:UBT/MSVC 14.44 编译 `Result: Succeeded`,零警告(unity 聚合含 DungeonSpawner.cpp) |
| 模块接线 | `../uegame/Source/uegame/uegame.Build.cs` | ✅ `NavigationSystem` 依赖 + 仓库根 include 路径 |
| 运行时导航配置 | `../uegame/Config/DefaultEngine.ini` | ✅ `[/Script/NavigationSystem.RecastNavMesh] RuntimeGeneration=Dynamic` |

## 已在仓库里完成的集成(无需再手动做)

1. `DungeonSpawner.h/.cpp` 位于 `uegame/Source/uegame/`,UBT 会自动编译。
2. `dungeon.hpp` / `m2_adapter.hpp` 留在仓库根(单一真源,同时被 standalone g++ 构建使用);
   `uegame.Build.cs` 的 `PrivateIncludePaths` 指到仓库根,`.cpp` 里 `#include "m2_adapter.hpp"` 可解析。
   两个头**只在 .cpp 里 include**,不进任何反射头(IWYU/unity-build 安全)。
3. `uegame.Build.cs` 依赖已加 `NavigationSystem`。
4. `DefaultEngine.ini` 已设 `RuntimeGeneration=Dynamic`(运行时 spawn 的几何,静态烘焙
   navmesh 不覆盖——这是 M2 的 carry-forward 约束)。

## 剩余里程碑

- ✅ **A 编译**(验收 #1,2026-07-02 已过):`"C:\Program Files (x86)\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" uegameEditor Win64 Development -Project="C:\Files\uegame\uegame\uegame.uproject" -WaitMutex` → `Result: Succeeded`,MSVC 14.44.35228 + Win SDK 10.0.26100,`[1/4] Compile Module.uegame.cpp`(unity 聚合含 `DungeonSpawner.cpp`)零警告零错误。注意 Live Coding 活跃时 UBT 会拒绝构建——先关编辑器。
- **B PIE 取证**(验收 #2/#3/#4):编辑器把 `ADungeonSpawner` 拖进关卡设 `Seed` → PIE。
  - 保真:`LogTemp` 里 `[Dungeon] seed=.. rooms=.. spawned N instances (walkable == plan-passable) .. fully-connected=YES planHash=0x..` + 关卡截图。
  - 可走:角色从起点房走到远房,沿途截图;撞墙不穿模、地面有 navmesh(`P` 键可视化)。
  - 确定性:同 `Seed` 两次 PIE,比对日志 `planHash` 一致。

## 审计六条建议在落地版中的处理

1. ✅ `NavigationSystem` 已加进 `Build.cs`(缺它 `UNavigationSystemV1` 链接失败)。
2. ✅ `NavMeshBoundsVolume` 只在 `BeginPlay` 且 `World->IsGameWorld()` 时 spawn;
   `OnConstruction` 只重建 ISM 几何(编辑器改 Seed 即时预览,不再产生重复 nav 体积)。
3. ⚠️ 运行时 spawn 的 brush 体积没有 brush 几何,`SetActorScale3D` 可能得到空 bounds——
   代码会把实测 bounds 打进日志(`bounds valid=YES/NO`),PIE 取证时一看便知走没走通;
   不行就用下面的手放方案(默认更稳)。
4. ✅ `FObjectFinder` 改为构造函数作用域的普通局部(不再 static-in-lambda)。
5. ✅ `AddInstance(..., bWorldSpace=true)`:地牢占据 m2 映射的**绝对世界坐标**,
   与 `StartWorld`(绝对)自洽,设计师移动 spawner actor 也不会错位。
6. ✅ 类加了 `UEGAME_API` 导出宏(跨模块引用安全;模板类自身省略该宏,属模板风格差异)。

## 运行时导航(手放兜底方案)

若 PIE 日志显示运行时体积 `bounds valid=NO`(或 navmesh 没铺开):在关卡里手放一个
`NavMeshBoundsVolume`,盖住 `(0,0)`–`(6400,4000)`(默认 64×40 格 × 100cm/格),
Brush 尺寸调大即可;`RuntimeGeneration=Dynamic` 已在 ini 里,无需再设。
然后把 spawner 的 `bSpawnNavBounds` 设为 `false`。

## 邻接一致性(carry-forward)

M1 走廊与 flood-fill 是 **4-连通**;`m2_adapter` 的世界可达性只连正交邻居;碰撞按格
实体生成。三者一致,不存在"对角相邻但走不过去"的伪连接。

## 确定性(carry-forward)

种子经 `cfg.seed = (uint64_t)Seed` 直传 `std::mt19937_64`。
**不要**改用 `FRandomStream`,否则同种子会得到不同地牢。
