# M2 — UE5 集成说明（脚手架）

把 M1 的抽象 `Layout` 渲染成可行走的 UE5 第三人称关卡。本目录是 **UE 侧胶水**，
依赖 UE5 头文件。

## 状态：本机未安装 UE，UE 侧未编译验证

| 部分 | 文件 | 状态 |
|---|---|---|
| 引擎无关核心（坐标映射 / 世界连通 / 确定性哈希） | `../m2_adapter.hpp` + `../m2_adapter_test.cpp` | ✅ 已用 g++ 实证（见下） |
| UE 胶水（ISM 生成 / 碰撞 / 运行时导航 / 玩家落点） | `DungeonSpawner.h` `.cpp` | ⚠️ 未编译（无 UE 工具链）|

引擎无关核心已验证的事实（`./m2test`）：
- 保真度：spawn-plan 覆盖全部 `width*height` 个格子；floor+corridor+door = 抽象可走格数。
- 连通：用**世界坐标 4-邻接**重建的可达性，与 M1 网格 flood-fill 完全一致（500/500 种子）。
- 确定性：同种子两次 spawn-plan 的 FNV-1a 哈希相同。

> 这些只证明了"算法→世界坐标"忠实。`#1 UE 构建`、`#3 PIE 可走`、以及 in-engine 的
> `#2/#4` 仍需在装了 UE 的工程里实跑，不能由本仓库断言。

## 集成步骤（有 UE5 第三人称 C++ 工程后）

1. 工程用 **C++ 模板**（不是纯蓝图），确保有 `Source/<Module>/`。
2. 拷贝到该模块目录：`DungeonSpawner.h`、`DungeonSpawner.cpp`、`../dungeon.hpp`、`../m2_adapter.hpp`。
3. `dungeon.hpp` / `m2_adapter.hpp` **只在 `DungeonSpawner.cpp` 里 include**，不要进任何公开
   头文件，避免标准库/算法类型进入 UE 反射头，并规避 unity-build 的跨文件包含问题。
4. `<Module>.Build.cs` 的依赖加上导航模块：
   ```csharp
   PublicDependencyModuleNames.AddRange(new[] {
       "Core", "CoreUObject", "Engine", "InputCore",
       "NavigationSystem"            // 运行时 navmesh 需要
   });
   ```
5. 生成工程文件并构建（**这才是验收 #1 的 UE 工具链构建**）：
   ```bat
   "<UE>\Engine\Build\BatchFiles\Build.bat" <Project>Editor Win64 Development -Project="<...>.uproject" -WaitMutex
   ```
6. 编辑器里把 `ADungeonSpawner` 拖进关卡（或放进默认关卡），设 `Seed`。

## 运行时导航（关键 carry-forward）

运行时 spawn 的几何，**静态烘焙的 navmesh 不会覆盖**。两选一：
- **推荐**：`Project Settings > Navigation Mesh > Runtime Generation = Dynamic`，
  让 `DungeonSpawner` 运行时 spawn 的 `NavMeshBoundsVolume` 生效。
- 或在关卡里**手放**一个足够大的 `NavMeshBoundsVolume` 盖住整张图，并仍设为 Dynamic。

`.cpp` 里运行时给 BoundsVolume 设尺寸是用缩放近似的（操作 Brush 较 fiddly）。
若运行时盒体不准，就用手放的 BoundsVolume，更稳。

## 邻接一致性（carry-forward）

M1 的走廊与 flood-fill 是 **4-连通**。`m2_adapter` 的世界可达性也只连正交邻居，
碰撞按格子实体生成，三者一致——不会出现"对角相邻但走不过去"的伪连接。

## 确定性（carry-forward）

种子经 `cfg.seed = (uint64_t)Seed` 直传给 `std::mt19937_64`。
**不要**改用 `FRandomStream`，否则同种子会得到不同地牢。

## 装好 UE 后的四条验收怎么取证

1. **构建**：贴上 `Build.bat ... Development` 的命令与 `Build succeeded` 结果（MSVC，非 g++）。
2. **保真**：PIE 时看 `LogTemp` 里 `[Dungeon] seed=.. rooms=.. world reach X/X .. fully-connected=YES`，
   并截一张生成关卡的图。
3. **可走**：PIE 操控第三人称角色从起点房间走到最远房间，沿途截图；撞墙不穿模、地面有 navmesh。
4. **确定性**：同 `Seed` 跑两次，比对日志里的 `planHash`（或房间世界坐标）一致。

## UE 集成注意事项（适配核心审计建议，集成 spawner 时处理）

下面是静态审计在装引擎前发现、集成时要处理的点（均非当前缺陷；scaffold 已标 UNVERIFIED）：

1. 依赖：`DungeonSpawner.cpp` 用到 `UNavigationSystemV1` / `FNavigationSystem`，集成时**必须**给 `<Module>.Build.cs` 的 `PublicDependencyModuleNames` 加 `"NavigationSystem"`（现有 `uegame.Build.cs` 只有 `AIModule`，缺它会链接失败）。
2. `NavMeshBoundsVolume` 的 spawn 要放在 `BeginPlay`，**不要**留在 `OnConstruction`：后者在编辑器每次移动/加载都会重跑，会生成重复 nav 体积。集成时把 nav 体积那段从 `Build()` 移到运行时。
3. 用 `SetActorScale3D` 调 `NavMeshBoundsVolume` 尺寸不可靠（盒体来自 Brush 几何，不随 actor scale）。优先用「运行时导航」一节的手放 BoundsVolume 方案。
4. `ConstructorHelpers::FObjectFinder<UStaticMesh>` 在 lambda 里声明为 `static` 较脆（只在 CDO/构造期有效，跨 hot-reload/cooked 无保证）。改为构造函数作用域内、每次使用各取一次更稳。
5. `AddInstance(Xf)` 用的是组件本地空间（UE5 签名 `AddInstance(const FTransform&, bool bWorldSpace=false)`）。只要 spawner actor 留在世界原点，local==world，放置正确；若 actor 被移动，几何会偏移而 `StartWorld`（绝对世界坐标）不同步——集成时把 actor 钉在原点，或给 `AddInstance` 传 `bWorldSpace=true`。
6. `UCLASS ADungeonSpawner` 没有 `<PROJECT>_API` 导出宏：单模块内用没问题；若要被其他模块引用需补上。
