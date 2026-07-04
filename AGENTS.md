# uegame 工作区规则(agent 操作经验)

本文件是本仓库的 agent 操作纪律,来自 M1-M4/v1 的实战沉淀。语义对齐副本:`CLAUDE.md`(Claude 读);两边同改。里程碑状态、分支拓扑不写在这里(看 git 历史与 PR),这里只放稳定的"在这个仓库怎么干活"。

## 0. 项目不变量

- `dungeon.hpp` 是 M1 引擎无关核心,契约禁改(tag `m1-baseline`)。适配层是 `m2_adapter.hpp`,UE 只在 .cpp 里 include 它,绝不进反射头文件。
- 确定性纪律 = 预注册:任何影响生成/种子/布点的改动,先用 WSL g++ 预测,再进 UE PIE 验证,两边输出逐位相等才算过。预注册值以命令输出为准,不在文档里复制哈希(避免二重 pin);`--csv` 让参考值直接跟随当前数据表(改表后布点数量会变、敌人哈希随之移动,别用工具内置默认值对新表做预测):
  `wsl -e bash -c "g++ -std=c++17 -O2 m2_adapter_test.cpp -o /tmp/m2t && /tmp/m2t floors <runSeed> --csv uegame/Content/Data/CombatConfig.csv"`
  (楼层数默认取表内 MaxFloors,与 FloorManager 判赢层数对齐;取证需要更多层可显式传 n。)
- 战斗数值唯一来源 `uegame/Content/Data/CombatConfig.csv`。加载器是进程级 LoadOnce 缓存——改 CSV 必须重启编辑器才生效,PIE 重开没用。
- 接触伤害量化到敌人 0.5s PursueTick(`Now-lastHit>=DamageInterval` 只在 tick 上判定):`EnemyDamageInterval` 取 0.5 的整数倍,否则名不副实(1.25 实际 1.5s 才触发);`Dungeon.BalanceReport` 的 DPS 按有效间隔 `ceil(interval/0.5)*0.5` 算,别照名义值高估伤害。
- balance 现为 provisional(Run 2 锁定):客观地板 OBJ1(首接触≥10s)/OBJ3(站桩存活≥45s)受"敌人贴脸生成 + 全图追踪 AI"结构性约束,数值调不到——真杠杆是感知模型(AggroRange/LOS/leash)+命中反馈+楼梯策略(descend-anytime,`bRequireFloorClearToDescend=false`),归后续 run。分工:客观地板 agent 用 BalanceReport+探针断言,feel 归 owner;地板不可达时带证据交 owner 重校准,别硬调数值凑绿。
- 地图 `Lvl_ThirdPerson` 已摆放 DungeonSpawner(`bAutoStartRun`,出货入口;Run 2 起)。首局种子默认走熵源随机,由 FloorManager 的单一 `ResolveFirstRunSeed` 选定并记 `[RunSeed] source=entropy`(全局唯一熵点,契约 F 修订版);可复现走 `Dungeon.SetRunSeed <n>` 或配置 `bUseFixedFirstSeed=true`(钉 demo 种子 7)。无 spawner 的地图仍由 FloorManager 自举兜底(经同一 resolver)。spawner 的编辑器 `Seed` 只驱动几何预览,不决定跑局种子。
- 更深的导航/runtime navmesh 细节见 `m2_ue/M2_UE_README.md`。

## 1. 构建与编辑器周期

- .h 改动必须全量 UBT,且编辑器必须先关(Live Coding 挡 UBT;运行中的编辑器锁 Content 文件,连大体积 git checkout/rebase 都会卡死)。.cpp-only 改动可在编辑器内 `LiveCoding.Compile`。
- 引擎根:`C:\Program Files (x86)\Epic Games\UE_5.8`(注册表 `HKCU\SOFTWARE\Epic Games\Unreal Engine\Builds`;不在 Program Files 无 x86 那份)。
- 构建:`"C:\Program Files (x86)\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" uegameEditor Win64 Development -project="C:\Files\uegame\uegame\uegame.uproject" [-Rebuild] -WaitMutex`。UBA 下全量 Rebuild 约 40 秒;里程碑门 = 全量 Rebuild 零警告。
- 编辑器开(MCP 偶发不自启,用 ExecCmds 兜底):`start "" UnrealEditor.exe "C:\Files\uegame\uegame\uegame.uproject" -ExecCmds="ModelContextProtocol.StartServer"`;关:taskkill 优雅优先,确认进程退出再动构建或 git。
- 编辑器摆 actor(如 placed spawner)存图走 UE5 OFPA(One-File-Per-Actor):产 `.umap` + `uegame/Content/__ExternalActors__/.../*.uasset`,两者都要进同一提交,否则 fresh checkout 的 .umap 引用不到 actor。`git check-ignore` 对带尾斜杠的目录路径会误报 ignored,核实要查具体 `.uasset` 文件(空输出/exit 1 = 未 ignored)。回滚 .umap 前先关编辑器(锁 Content)。

## 2. MCP/PIE 取证(纯 HTTP,任何带 shell 的 agent 都能用)

- 桥:Epic ModelContextProtocol 插件,`127.0.0.1:8000/mcp`,JSON-RPC(先 initialize 拿 `Mcp-Session-Id` 响应头,后续请求带同名头)。工具经 `call_tool` 元工具调用,参数形状:
  `{"toolset_name":"uegameEditor.UegameMcpToolset","tool_name":"ExecConsoleCommand","arguments":{"command":"..."}}`
  键名是 `arguments`,不是 `tool_arguments`——错键返回 isError 但外表像发出去了(曾造成一整轮取证静默失效)。
- initialize / tools/list 回纯 JSON;tools/call 回 SSE → curl 用 `--max-time 2` 抓首条 `data:` 行,否则每次调用白等到超时。
- `ExecConsoleCommand` 返回值带 `handled=yes/no`,必须读:handled=no 表示命令没被处理(如 `pause` 经 GEngine->Exec 走不到 CheatManager);CVar(如 `t.MaxFPS`)正常可用。
- MCP 每 tick 只泵一条命令:同帧双动作竞态从外部注入不可达。要测这类窗口,写 in-handler 复合取证命令(参照 `Dungeon.DescendThenDie`),不要浪费时间调时序。
- PIE 取证序列必须整段放进单个脚本一次跑完(敌群 DPS 会赢过逐回合命令节奏);节奏优先:动词连发、少插状态查询;需要固定种子链就 `Dungeon.StartRun <seed>` 重锚(幂等,免疫此前有机死亡造成的链漂移)。
- 断言写进脚本(grep 日志锚点),不靠肉眼。锚点:`[RunStarted] [FloorConfig] [FloorStarted] [FloorCompleted] [RunWon] [RunFailed] [RunRestart] [Stairs] [FloorStatus] [RunSeed] [BalanceReport] [BalanceProbe] enemyPlan planHash`。日志在 `uegame/Saved/Logs/uegame.log`(有落盘缓冲,取证动作结束后再切片;编辑器重启会把旧日志滚成 `uegame-backup-*.log`,跨会话取分布要翻备份)。
- 存活/时长类取证取真值用日志锚时间差(`[FloorStarted floor=1]`→`[RunFailed]`),不要用探针从"arm 时刻"计的 elapsed:auto-run 已跑几秒才 arm 只会测到残余存活,给假的极短值(实测踩过 1.4s/2s 假象);一个 PIE 会话的连环死亡就是整条分布。
- 取证动词(Run 2 起 15 个)都在 `!UE_BUILD_SHIPPING` 内(DungeonEvidence.cpp gate 43–692):`Dungeon.StartRun/SetRunSeed/Descend/DescendThenDie/FloorStatus/SetHP <v> [holdSec]/Regen/BalanceReport` + M2/M3 一组。`SetHP` 的 `[holdSec]` 无敌靠 `HealthComponent::bInvincible`(取证专用,源头 no-op TakeDamage,默认 false 永不进正常玩法)。另有编辑器专属 `DungeonEditor.PlaceSpawnerInLevel`(uegameEditor,WITH_EDITOR,PIE 活跃时拒绝,SpawnActor 后 `Build()` 重建预览)——不计入这 15。

## 3. Git 与 PR 门

- 提交必须签名(本机 1Password SSH 已配置,直接提交即可);严禁 `-c commit.gpgsign=false` 绕过——main 的 ruleset 要求签名,绕过的代价是整条历史重签+force push。commit message 不加 Co-Authored-By 或任何 AI 署名。
- 合并门:走 PR;Codex 认为没有问题才合并,否则修复循环:修→签名提交→push→逐条回复 inline 评论(`gh api repos/{owner}/{repo}/pulls/{n}/comments/{id}/replies`)→GraphQL `resolveReviewThread`→评论 `@codex review` 触发复审。Codex 干净的信号:review 正文"Didn't find any major issues"或对触发评论打 👍。
- Codex 逐轮追上一轮修复挖 follow-up(新增取证/数学工具面尤甚,预期几轮才收敛):竞态/半修在源头根治,别只补表象——例:无敌 hold 走 `HealthComponent::bInvincible` 在 TakeDamage no-op(不打死链),而非 ticker re-top 或事后 Revive 去 race `OnDeath`→`NotifyRunFailed` 排队的重启。出货行为/钉哈希/验收证据不因评审轮次而动;每轮:修→签名提交→push→逐条回复→resolve→`@codex review`。
- CodeQL 默认配置含 actions 语言:分支上缺 `.github/workflows` 会 exit 32 fatal;merge main 把 workflows 带上即修。
- bug 修复默认先做修复前复现;窗口从外部不可达时,把尝试与机制证据留成负结果档,再用探针命令做修复后的确定性验证。

## 4. 证据纪律

- 截图/证据的 provenance 用 MD5 + 日志时间戳核对,不凭文件编号或直觉推断(执行审计曾抓出标错截图)。
- "命令发了没反应"先查返回值与参数形状,再怀疑目标系统;定性等根因证据齐了再下。
