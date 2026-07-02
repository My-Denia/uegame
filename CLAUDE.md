# uegame 工作区规则(agent 操作经验)

本文件是本仓库的 agent 操作纪律,来自 M1-M4/v1 的实战沉淀。语义对齐副本:`AGENTS.md`(Codex 读);两边同改。里程碑状态、分支拓扑不写在这里(进长期记忆/git 历史),这里只放稳定的"在这个仓库怎么干活"。

## 0. 项目不变量

- `dungeon.hpp` 是 M1 引擎无关核心,契约禁改(tag `m1-baseline`)。适配层是 `m2_adapter.hpp`,UE 只在 .cpp 里 include 它,绝不进反射头文件。
- 确定性纪律 = 预注册:任何影响生成/种子/布点的改动,先用 WSL g++ 预测,再进 UE PIE 验证,两边输出逐位相等才算过。预注册值以命令输出为准,不在文档里复制哈希(避免二重 pin);`--csv` 让参考值直接跟随当前数据表(改表后布点数量会变、敌人哈希随之移动,别用工具内置默认值对新表做预测):
  `wsl -e bash -c "g++ -std=c++17 -O2 m2_adapter_test.cpp -o /tmp/m2t && /tmp/m2t floors <runSeed> --csv uegame/Content/Data/CombatConfig.csv"`
  (楼层数默认取表内 MaxFloors,与 FloorManager 判赢层数对齐;取证需要更多层可显式传 n。)
- 战斗数值唯一来源 `uegame/Content/Data/CombatConfig.csv`。加载器是进程级 LoadOnce 缓存——改 CSV 必须重启编辑器才生效,PIE 重开没用。
- 地图 `Lvl_ThirdPerson` 里没有摆 DungeonSpawner:运行时由 FloorManager 自举(无 spawner 时用默认种子 7)或 `Dungeon.StartRun` 动态生成。不要按"关卡里有 spawner"的直觉推理。
- 更深的导航/runtime navmesh 细节见 `m2_ue/M2_UE_README.md`。

## 1. 构建与编辑器周期

- .h 改动必须全量 UBT,且编辑器必须先关(Live Coding 挡 UBT;运行中的编辑器锁 Content 文件,连大体积 git checkout/rebase 都会卡死)。.cpp-only 改动可在编辑器内 `LiveCoding.Compile`。
- 引擎根:`C:\Program Files (x86)\Epic Games\UE_5.8`(注册表 `HKCU\SOFTWARE\Epic Games\Unreal Engine\Builds`;不在 Program Files 无 x86 那份)。
- 构建:`"C:\Program Files (x86)\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" uegameEditor Win64 Development -project="C:\Files\uegame\uegame\uegame.uproject" [-Rebuild] -WaitMutex`。UBA 下全量 Rebuild 约 40 秒;里程碑门 = 全量 Rebuild 零警告。
- 编辑器开(MCP 偶发不自启,用 ExecCmds 兜底):`start "" UnrealEditor.exe "C:\Files\uegame\uegame\uegame.uproject" -ExecCmds="ModelContextProtocol.StartServer"`;关:taskkill 优雅优先,确认进程退出再动构建或 git。

## 2. MCP/PIE 取证

- 桥:Epic ModelContextProtocol 插件,`127.0.0.1:8000/mcp`,JSON-RPC(先 `initialize` 拿 `Mcp-Session-Id` 响应头,后续请求带同名头,否则 tools/list、tools/call 不挂在已初始化会话上)。工具经 `call_tool` 元工具调用,参数形状:
  `{"toolset_name":"uegameEditor.UegameMcpToolset","tool_name":"ExecConsoleCommand","arguments":{"command":"..."}}`
  键名是 `arguments`,不是 `tool_arguments`——错键返回 isError 但外表像发出去了(曾造成一整轮取证静默失效)。
- initialize / tools/list 回纯 JSON;tools/call 回 SSE → curl 用 `--max-time 2` 抓首条 `data:` 行,否则每次调用白等到超时。
- `ExecConsoleCommand` 返回值带 `handled=yes/no`,必须读:handled=no 表示命令没被处理(如 `pause` 经 GEngine->Exec 走不到 CheatManager);CVar(如 `t.MaxFPS`)正常可用。
- MCP 每 tick 只泵一条命令:同帧双动作竞态从外部注入不可达。要测这类窗口,写 in-handler 复合取证命令(参照 `Dungeon.DescendThenDie`),不要浪费时间调时序。
- PIE 取证序列必须整段放进单个脚本一次跑完(敌群 DPS 会赢过逐回合命令节奏);节奏优先:动词连发、少插状态查询;需要固定种子链就 `Dungeon.StartRun <seed>` 重锚(幂等,免疫此前有机死亡造成的链漂移)。
- 断言写进脚本(grep 日志锚点),不靠肉眼。锚点:`[RunStarted] [FloorConfig] [FloorStarted] [FloorCompleted] [RunWon] [RunFailed] [RunRestart] [Stairs] [FloorStatus] enemyPlan planHash`。日志在 `uegame/Saved/Logs/uegame.log`(有落盘缓冲,取证动作结束后再切片)。
- 取证动词都在 `!UE_BUILD_SHIPPING` 内(DungeonEvidence.cpp):`Dungeon.StartRun/Descend/DescendThenDie/FloorStatus/SetHP/Regen` + M2/M3 一组。

## 3. Git 与 PR 门

- 提交必须签名(本机 1Password SSH 已配置,直接提交即可);严禁 `-c commit.gpgsign=false` 绕过——main 的 ruleset 要求签名,绕过的代价是整条历史重签+force push。commit message 不加 Co-Authored-By 或任何 AI 署名。
- 合并门:走 PR;Codex 认为没有问题才合并,否则修复循环:修→签名提交→push→逐条回复 inline 评论(`gh api repos/{owner}/{repo}/pulls/{n}/comments/{id}/replies`)→GraphQL `resolveReviewThread`→评论 `@codex review` 触发复审。Codex 干净的信号:review 正文"Didn't find any major issues"或对触发评论打 👍。
- CodeQL 默认配置含 actions 语言:分支上缺 `.github/workflows` 会 exit 32 fatal;merge main 把 workflows 带上即修。
- bug 修复默认先做修复前复现;窗口从外部不可达时,把尝试与机制证据留成负结果档,再用探针命令做修复后的确定性验证。

## 4. 证据纪律

- 截图/证据的 provenance 用 MD5 + 日志时间戳核对,不凭文件编号或直觉推断(执行审计曾抓出标错截图)。
- "命令发了没反应"先查返回值与参数形状,再怀疑目标系统;定性等根因证据齐了再下。

## 5. Claude 侧

- 长期记忆在 `~/.claude/projects/C--Files-uegame/memory/`(里程碑状态、教训索引);本文件只放稳定操作纪律,状态类事实进记忆,别写死在这里过期。
