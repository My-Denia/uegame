# M2 — UE5 集成说明

把 M1 的抽象 `Layout` 渲染成可行走的 UE5 第三人称关卡。

## ✅ M2 完成:四条验收全部取证(2026-07-02)

经 MCP(项目工具集 `uegameEditor.UegameMcpToolset`)驱动 PIE 实测,日志与截图见下:

1. **构建**:`Build.bat uegameEditor Win64 Development` → `Result: Succeeded`,MSVC 14.44,零警告(多次构建,含新增编辑器模块 13 动作全绿)。
2. **保真**:`[Dungeon] seed=7 rooms=8 connections=9 | spawned 2560 instances (441 walkable == 441 plan-passable) | world reach 441/441 cells, 8/8 rooms | fully-connected=YES` —— 实 spawn 实例数与抽象 Layout 逐项相等;截图 `evidence/`。
3. **可走**:`NAVPATH valid=YES partial=NO points=11 length=14976` + 角色实走 `ARRIVED at farthest room: dist2D=39 elapsed=30.7s`(tileSize=200;沿走廊绕行 14976 vs 直线 9485,碰撞约束下的路径跟随);截图 `evidence/m2_walk_corridor_seed7.png`、`evidence/m2_arrived_far_room_seed7.png`。
4. **确定性**:seed 7 @tileSize=100 的 `planHash=0xa7cdf0446a8c246e` 在**四次独立 PIE 会话**一致,且与 g++ standalone 值逐位相同(跨编译器实证);@tileSize=200 的 `0x5e03831865ce5cb2` 在两轮独立 PIE 一致。

### 运行时导航的三个实测结论(排障记录,含两次 Live Coding 热修)

1. 运行时 spawn 的 `NavMeshBoundsVolume` 没有 brush 几何 → bounds 为空且**在 SpawnActor 内部就以空 bounds 注册**。修法(已在代码里):`SpawnActorDeferred` + 在 `FinishSpawning` 前挂好地图尺寸的 `UBoxComponent`(nav 系统读 `GetComponentsBoundingBox(true)`,NavigationSystem.cpp:4145)。
2. 地牢几何注册早于 nav 体积 → 界外 dirty area 被丢弃。修法(已在代码里):体积注册后 `AddDirtyArea(全图, ENavigationDirtyFlag::All)` 强制重铺。
3. **tileSize=100 的 1 格走廊会被 navmesh 剔除**:`AgentRadius=35` 两侧侵蚀后仅剩 ~30cm,Recast 丢弃 → 每房成孤岛(路径 partial)。**建议 tileSize ≥ 200**(`Dungeon.Spawn <seed> 200` 或在 spawner 上设 `TileSize=200`;M3 Phase-0 起 200 已是 spawner 默认值,见提交 8faf20a)。玩家物理碰撞在 100 下其实能过(玩家胶囊直径 84cm<100cm,半径 42 见 uegameCharacter.cpp:24),被卡的只是 navmesh;后续里程碑若定 100 需调 agent radius 或加宽走廊。

### 复现命令(编辑器开着、MCP server 监听 8000 时)

MCP `call_tool`:`StartPIE` → `ExecConsoleCommand "Dungeon.Spawn 7 200"` → `ExecConsoleCommand "Dungeon.WalkFar"` → 日志看 `NAVPATH`/`ARRIVED` → `ExecConsoleCommand "HighResShot 1600x900"` → `StopPIE`。

## 状态:代码已落地进 UE 工程(以下为落地期记录)

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
- ✅ **B PIE 取证**(验收 #2/#3/#4,2026-07-02 已过,四条验收证据见顶部"M2 完成"节):编辑器把 `ADungeonSpawner` 拖进关卡设 `Seed` → PIE。
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
`NavMeshBoundsVolume`,盖住 `(0,0)`–`(12800,8000)`(64×40 格 × 当前默认 TileSize=200;若手动设 100 则为 `(6400,4000)`),
Brush 尺寸调大即可;`RuntimeGeneration=Dynamic` 已在 ini 里,无需再设。
然后把 spawner 的 `bSpawnNavBounds` 设为 `false`。

## 邻接一致性(carry-forward)

M1 走廊与 flood-fill 是 **4-连通**;`m2_adapter` 的世界可达性只连正交邻居;碰撞按格
实体生成。三者一致,不存在"对角相邻但走不过去"的伪连接。

## 确定性(carry-forward)

种子经 `cfg.seed = (uint64_t)Seed` 直传 `std::mt19937_64`。
**不要**改用 `FRandomStream`,否则同种子会得到不同地牢。
