# uegame — a seeded roguelite slice on UE 5.8, agent-first, evidence-gated

> Language note: this file is an English snapshot of [README.md](README.md) (zh-CN, canonical). If they diverge, the Chinese file wins. The M2 engine-integration deep dive lives at [m2_ue/M2_UE_README.md](m2_ue/M2_UE_README.md) (zh-CN).

**uegame** is a seeded roguelite slice on Unreal Engine 5.8: procedural dungeons, data-driven melee combat, three-floor runs (floor count comes from a DataTable), per-floor scaling, and a win/lose loop. Same seed, same run — layouts, enemy placements and the run-seed chain are deterministic properties that get *checked* bit-for-bit, not assumed.

It is also an experiment in how to build one. The whole M1→M4 arc was implemented by AI coding agents under evidence gates: predictions pre-registered before the engine runs, cross-compiler hash checks, forensic in-engine probes, independent audits, adversarial cross-tool review. This README is the account of both the game and the process. Every number in it traces to a commit, a PR thread, or a file in this repo — and where something was human-driven, unreachable, or unverified, it says so.

Status: v1 loop complete (M1+M2+M3+M4); PR #3 merged into `main` on 2026-07-02 (merge commit `94e679d`).

---

## 1. The five-minute version

Three goals:

1. Ship a playable roguelite slice on a real engine stack — not a slideware demo;
2. Keep the whole generation→placement→progression chain deterministic, independently re-verifiable with two commands;
3. Validate an agent-driven development framework — and record its boundaries honestly.

| | |
|---|---|
| ![M2: walking a corridor (seed 7)](m2_ue/evidence/m2_walk_corridor_seed7.png) | ![M4: floor-3 combat, ~3s before the win (runSeed 7)](m2_ue/evidence/m4_floor3_combat_prewin_run7.png) |
| M2: physically walking the seed-7 dungeon | M4: floor 3 of runSeed 7, six scaled enemies pressing, ~3s before the winning stairs (`6a9858f`) |

Scoreboard — every row can be re-checked by following its anchor:

| Claim | Anchor |
|---|---|
| Layout hash identical across 4 independent PIE sessions and bit-equal to the standalone g++ build (tileSize=100; two sessions at tileSize=200) | `816a8dc`; [M2_UE_README L12](m2_ue/M2_UE_README.md) |
| Spawn fidelity: 441 walkable cells == 441 plan-passable cells; in-world reachability 441/441; 8/8 rooms connected | `816a8dc` |
| M4: all 6 per-floor hashes (layout + enemies, 3 floors) pre-registered from g++, then reproduced in-engine bit-for-bit — including after the review-fix round, on the zero-console-input auto-start path | `8a4929c` → `5824e68` / `c7deca2` |
| 200 runSeeds generated twice each: 200/200 floor sequences bit-identical; 200/200 adjacent runSeeds differ | `8a4929c` |
| Enemy-placement determinism: four assertions at 500/500 each (bit-identical, in-room, start-room-excluded, zero duplicate cells); saturation stress 509/509 exact | `048ad06` / `686938b` / `1463b30` |
| Independent verifier (user-supplied, not agent-built) passes 1000/1000 validate+determinism on two seed ranges | `524be80` |
| Contact-damage cadence 10 per 1.00s, player HP 100→0, death restart ×2 — all log-evidenced | `ad57d7e` |
| Every Codex review P2 closed with a fix commit plus regression evidence; all threads resolved | PR #1 / PR #3 review threads |

Want to play now: jump to [§7 How to run](#7-how-to-run). The method: [§4 Verification methodology](#4-verification-methodology). What this process could *not* do: [§5 Honest boundaries](#5-honest-boundaries).

---

## 2. Architecture — four layers, and why

```
dungeon.hpp            M1 engine-agnostic core: layout generation + validation
   │                     (pure C++17, no engine types; frozen contract, tag m1-baseline → f357ff8)
m2_adapter.hpp         M2/M3/M4 engine-agnostic adapter: grid→world mapping, connectivity re-check,
   │                     FNV-1a layout hash, enemy-placement plans, splitmix64 floor-seed chain + scaling
uegame/Source/uegame   UE glue: DungeonSpawner (ISM geometry/collision/runtime navmesh),
   │                     FloorManager (GameInstance subsystem: run/floor state machine), Combat/, DungeonStairs
uegameEditor           MCP toolset (ExecConsoleCommand/StartPIE/StopPIE/GetPIEStatus)
                         + 13 Dungeon.* forensic verbs (all !UE_BUILD_SHIPPING)
```

Why this split:

- **The core predates the engine.** When the adapter layer was built and g++-verified, this machine had no UE installed — the commit body says so verbatim: "no UE on this machine, not compiled" (`822dfa4`; the same fact recorded again in `2455032`). The algorithm's correctness never depended on an engine being present.
- **One source, two compilers checking each other.** `dungeon.hpp`/`m2_adapter.hpp` stay at the repo root and are compiled both by the standalone WSL g++ build and by UE's MSVC build (`2455032`). The same seed must print bit-identical hashes on both sides — determinism is a checked property, not a hoped-for one.
- **Reflection isolation.** Both headers are included from .cpp files only, never from any UHT reflection header ([M2_UE_README L40–42](m2_ue/M2_UE_README.md)); UE's unity build and IWYU cannot leak into the core.
- **Single source for numbers.** All combat and progression values come from [uegame/Content/Data/CombatConfig.csv](uegame/Content/Data/CombatConfig.csv) — loaded at runtime, echoed once on load, loud fallback if missing (`593a88b`), staged as NonUFS loose files for packaged builds (`4358a35`).
- **RNG discipline.** Every random draw comes from a single explicitly-seeded `std::mt19937_64`; no time-based seeding, no global rand, no static mutable state (dungeon.hpp header). Enemy placement uses a `seed ^ 0x9E3779B97F4A7C15` sub-stream so the layout stream is untouched (`048ad06`); the floor-seed chain is a pure-integer splitmix64 pipeline that consumes no RNG stream at all (`8a4929c`).

---

## 3. Milestones

### M1 — engine-agnostic dungeon generator

Rejection-sampled rooms → complete graph over room centers → Prim MST for a guaranteed-connected backbone → a few loop edges → L-shaped corridors and doors (dungeon.hpp header; default 64×40 grid, 8–12 rooms, dungeon.hpp:51–54).

| Claim | Anchor |
|---|---|
| 1000-seed reachability + determinism + adversarial validator audit | `f357ff8` |
| Independent re-verification: 1000/1000 validate+determinism on two ranges (seeds 1..1000, 5000..5999), room-count range [8,12] | `524be80` |
| Verifier uses only the `dungeon::` public API — separate from the agent's own harness | [m1_verify.cpp](m1_verify.cpp) header |
| After the contract froze, later milestones keep regressing green (e.g. validate 100 → 100/100) | `686938b` |

### M2 — landing in UE: faithful, walkable, deterministic

The abstract `Layout` becomes a walkable third-person level; all four acceptance criteria evidenced through MCP-driven PIE (`816a8dc`).

| Claim | Anchor |
|---|---|
| Fidelity: 2560 instances; 441 walkable == 441 planned cells; world reach 441/441; 8/8 rooms; fully-connected=YES | `816a8dc` |
| Walkability: NAVPATH valid=YES partial=NO points=11 length=14976 (straight-line 9485 — collision-constrained detour); physically walked to the farthest room, ARRIVED dist2D=39 in 30.7s | `816a8dc`; [M2_UE_README L11](m2_ue/M2_UE_README.md) |
| Determinism: same-seed layout hash identical across 4 PIE sessions and bit-equal to g++ | `816a8dc` |
| Adapter-side independent check: 500 seeds, world connectivity and grid consistency 500/500 | `822dfa4` |
| Cumulative >120k generate+validate runs, zero failures (doc-level record) | [M2_UE_README L31](m2_ue/M2_UE_README.md) |

Three runtime-navigation problems were killed during the evidence run itself (two Live Coding hot patches, `816a8dc`): empty runtime NavMeshBoundsVolume bounds, out-of-bounds dirty areas being dropped, and the corridor-erosion case that became [§5's case study](#5-honest-boundaries).

### M3 — data-driven minimal combat

Enemy pursuit, contact damage, player melee, room-clear events; enemy placement generated in the engine-agnostic layer with the start room deliberately excluded — no ambush at the player spawn, a recorded design deviation (`593a88b`).

| Claim | Anchor |
|---|---|
| Enemy-plan hash identical across 3+ sessions == the g++ value; hash covers tile-invariant fields only, so any tileSize yields the same value | `ad57d7e` / `048ad06` |
| Placement determinism: 500/500 identical, 500/500 in-room, 500/500 start-room-excluded | `048ad06` |
| Pursuit convergence nearestDist 1396→630→70; first pathfinding 68/98 successful — 30/98 lose the navmesh-build race and recover via the 0.5s repath (honest number, kept as-is) | PR #1 |
| Damage chain CSV-traceable: contact 10 per 1.00s cadence, HP 100→0, restart ×2; melee kills at 15+15=30; cooldown rejections ×3 | `ad57d7e` |
| Room-clear touches only the target room; other rooms' counts unaffected | `ad57d7e` |
| Review counterexample killed: seed 1 room 9 (14,35) duplicate placement → sampling without replacement + deterministic exact fallback on budget exhaustion; saturation stress 509/509 | `686938b` / `7c5a769` / `1463b30` |

### M4 — floor progression, scaling, win/lose (v1 complete)

FloorManager (a GameInstance subsystem) owns {runSeed, floorIndex}; floor transitions go through a single in-place regeneration path — no level swap, the world and navmesh stay alive, and player HP carries across floors for free (`1ec27c4`). That buries M3's known defect: OpenLevel kills the RecastNavMesh (`5824e68`).

| Claim | Anchor |
|---|---|
| Pre-registration: runSeed=7 floor seeds, layout hashes, enemy hashes and scaling values for all 3 floors — plus nextRunSeed and a counterexample (runSeed=8 floor-1 hash differs) — recorded in the commit before any PIE run | `8a4929c` |
| Navigation survives in-place regeneration: floor-2 layout hash reproduces the pre-registered value; enemies re-path RequestSuccessful, nearestDist 2000+→81 | `5824e68` |
| Full chain reproduced after the review-fix round: zero-console-input auto-start reproduces the run-7 chain; all six hashes match; [RunWon] yields the g++-matching nextRunSeed | `c7deca2` |
| Scaling is data-driven: floor 3 runs 6 enemies/room at effHP=90; formula and DataTable echo logged per floor in [FloorConfig] | `1ec27c4`; PR #3 |
| HP persists across floors: [FloorStarted] floor=2 playerHP=23/100 | PR #3 |
| Seed chain reproduced across 3 independent sessions and checked against g++ by an independent auditor | PR #3 |
| Independent audit: 6/6 acceptance criteria met — and the same audit caught two mislabeled screenshots via MD5 + log-timestamp cross-check; evidence relabeled before close | PR #3; `6a9858f` |
| Same-tick race killed: death upgrades a queued descend; no floor ever starts with a dead pawn | `c7deca2` |

---

## 4. Verification methodology

The transferable part of this project is exactly six disciplines:

**Predict first, then run the engine.** Anything that shapes generation ships with a pre-registered prediction. The engine-agnostic core builds standalone under g++ and prints the reference values — per-floor seeds, layout hashes, enemy-plan hashes — which go into the commit message *before* the editor ever runs (`8a4929c`). Unreal then has to reproduce them byte-for-byte from a different compiler in a different process: first in a spike (`5824e68`), then across the full descend chain with zero console input after the review-fix round (`c7deca2`). A prediction that fails stops the milestone. The hash values themselves are deliberately not restated in this file — the repo's rule is that the reproduce command is the source of truth (`m2test floors 7 3`, see §7), and immutable commit messages hold the recorded values, so this document points rather than copies.

**Pinned hashes as regression armor.** The seed-7 layout and enemy hashes are re-asserted after every risky change: after deleting the template variants (`9ffaa26`), after the TileSize default flip (`8faf20a`), after review fixes (`1463b30`), on the M4 base (PR #3). If the pins hold, the refactor was harmless.

**Log-anchor assertions over eyeballs.** PIE evidence is judged by grepping log anchors ([RunStarted], [FloorConfig], [FloorCompleted], [RunWon], [RunFailed] — see the `1ec27c4`/`c7deca2` bodies); screenshots are illustration only. §5's case study is the reason: the good-looking screenshot is exactly what missed the real defect.

**Audits with teeth.** Before M4 closed, an independent execution audit re-checked 6/6 acceptance criteria against raw evidence — and actually caught something: two screenshots had wrong capture moments (inferred from file numbering instead of checked against MD5s and log timestamps). The evidence was relabeled before sign-off (`6a9858f`; PR #3). The audit is not a ceremony.

**Adversarial cross-tool review.** Every PR was reviewed round-by-round by a different model (Codex). It produced a genuine counterexample (the seed 1 room 9 duplicate placement, `686938b`) that forced sampling without replacement plus an exactness assertion; it also surfaced a review-surface problem — Epic's template variants accumulated 13 P2/P3 findings across three rounds, and the owner decided to delete them outright (`9ffaa26`). Every P2 closed with a fix commit and regression evidence (the fix commits count themselves: `686938b` 5×P2+1×P3, `4358a35` 2×P2, `1463b30` 2×P2, `c7deca2` 3×P2).

**Verifier separated from the verified.** M1's re-verification program uses only the `dungeon::` public API; its header says verbatim: "Not the agent's harness." And it entered the repo as a pre-existing user file, committed on request (`524be80`) — the check on the agents' work was itself human-supplied.

---

## 5. Honest boundaries

**What the agents did not do.** A human installed Unreal 5.8 and signed into the Epic launcher, created the project from Epic's third-person template (`dcef21b`), enabled the MCP editor plugin (`a5795fa` — recorded as *user-enabled*), and owned the taste and scope calls: pacing, the descend-anytime default (`1ec27c4`), and the decision to delete Epic's unused template variants after review kept surfacing findings in code the game never ran (`9ffaa26` — an *owner decision*). Even the independent M1 verifier entered the repo as a *pre-existing user file, committed on request* (`524be80`): the check on the agents' work was itself human-supplied. Every merge into `main` was human-approved.

**Where the machine cannot reach.** The MCP bridge pumps one console command per engine tick, so a same-tick race (descend queued, then death, same frame) could not be injected externally — it was verified by compiling the probe into the game (`Dungeon.DescendThenDie`, `c7deca2`). HighResShot renders without HUD, so the on-screen RUN WON text cannot be screenshot-evidenced; the win is proven by the [RunWon] log line instead (`6a9858f`). And the evidence pipeline is audited, not trusted: the first pass at labeling M4's screenshots got two captions wrong, caught by an independent auditor cross-checking MD5s against log timestamps (`6a9858f`).

**Case study: the corridor that looked walkable.** At tileSize=100, one-tile corridors are physically passable (the player capsule is 84cm across — radius 42, uegame/Source/uegame/uegameCharacter.cpp:24 — inside a 100cm corridor) and look perfectly fine in screenshots — but the navmesh's AgentRadius=35 erodes both sides below viability, Recast culls the corridor, and every room becomes a navigation island. No amount of looking at screenshots finds this; what found it was an objective assertion — the NAVPATH partial flag and MoveToActor result codes (`816a8dc`). The fix landed in the adapter layer: spawner default TileSize 200 (`8faf20a`), with the M1 core untouched. Every later milestone gates on log assertions, and screenshots were demoted to illustration. It is the most important lesson in this file: looking right is not being right, and walkable is not pathable.

**Known limitations, stated as-is.** Melee and contact checks are distance+facing tests with no line-of-sight — in principle they can hit through a wall (uegame/Source/uegame/Combat/, forward sphere range check). An enemy's first pathfinding request can lose the navmesh-build race (30/98, PR #1) and recovers via the 0.5s repath; the first-success moment is not separately logged. M3's death restart goes through OpenLevel, and navigation in the fresh world degrades within the same PIE session — the M4 run loop sidesteps this via in-place regeneration (`5824e68`) while single-floor mode keeps the old behavior. The win screen is debug text, not a UI menu (`6a9858f`).

---

## 6. Problems-killed ledger

| Problem | Root cause | Fix |
|---|---|---|
| MCP toolset silently failed to register | Editor subsystems don't exist yet at StartupModule time | Defer registration to OnPostEngineInit `18c8afa` |
| Runtime NavMeshBoundsVolume had empty bounds | Runtime brush volumes carry no geometry and register empty inside SpawnActor | Deferred spawn + map-sized BoxComponent attached before FinishSpawning `816a8dc` |
| Dungeon geometry's dirty areas dropped | Geometry registers before the nav volume exists; out-of-bounds dirty areas discarded | AddDirtyArea over the whole map after volume registration `816a8dc` |
| Corridors walkable but AI cannot path | tile-100 corridors eroded below AgentRadius=35 viability and culled | Default TileSize 200, absorbed in the adapter; core untouched `8faf20a` |
| Duplicate cells in enemy placements | Sampling with replacement; review counterexample seed 1 room 9 | Without-replacement + deterministic exact fallback on budget exhaustion `686938b` / `7c5a769` / `1463b30` |
| Dungeon.Spawn dirtied the editor level | MCP bridge falls back to the editor world when PIE is inactive | Refuse non-game worlds `1463b30` |
| Packaged builds silently fell back to default combat values | Raw CSV not cooked | DefaultGame.ini NonUFS loose-file staging `4358a35` |
| CodeQL required check fatal exit-32 | Branch had no .github/workflows; the actions language saw zero input | Merge main to bring the workflow files `c76a782` |
| Stairs appeared with no run active | BeginPlay and forensic Regen also spawned stairs | Single run-active gate `c7deca2` |
| Death vs queued-descend same-tick race | Pending transition was a bool with no priority semantics | Enum-typed pending + death upgrades descend; verified by an in-handler compound probe `c7deca2` |
| Floor transition killed navigation | OpenLevel rebuilds the world; RecastNavMesh lost | In-place regeneration: despawn→Build→re-dirty→teleport→respawn `5824e68` |
| Screenshot evidence mislabeled | Capture moments inferred from file numbering | Independent audit cross-checked MD5 + log timestamps; relabeled `6a9858f` |
| Review surface blown up by template code | Unused Epic variants still compiled; 13 P2/P3 findings accumulating without bound | Owner decision: delete outright (source + ~3.9MB assets) `9ffaa26` |
| Deprecation warning only visible in full builds | Live Coding increments masked C4996 | Full clean rebuild added to the gate; fixed `8faf20a` |

---

## 7. How to run

Prerequisites: Windows 10/11; UE 5.8 (Epic Games Launcher); Visual Studio 2022 with the C++ desktop workload (measured toolchain MSVC 14.44 + Win SDK 10.0.26100, `8eabc1b`); Git. The repo was ~847 files / ~135MB when the UE project landed (`dcef21b`).

```
git clone https://github.com/My-Denia/uegame.git
"C:\Program Files (x86)\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ^
  uegameEditor Win64 Development -project="<repo>\uegame\uegame.uproject" -WaitMutex
```

Expect the tail line `Result: Succeeded` (a measured incremental build took 16.75s, `8eabc1b`). Note: UBT refuses to build while the editor's Live Coding is active — close the editor first (`8eabc1b`).

**Play.** Open `uegame\uegame.uproject`, hit PIE on the default map Lvl_ThirdPerson (uegame/Config/DefaultEngine.ini:2). No console input needed: the FloorManager bootstraps a run after world init with the pinned default seed 7 (`c7deca2`). F = melee (`593a88b`); step on the stairs pad in the farthest room to descend; descending past floor 3 wins (MaxFloors=3, [CombatConfig.csv](uegame/Content/Data/CombatConfig.csv)); death loses and restarts floor 1 on the next chained seed. After editing the CSV, restart the editor — the loader is a process-level load-once cache (LoadOnce at uegame/Source/uegame/Combat/CombatConfig.cpp:15).

**Forensic console** (13 verbs, all `!UE_BUILD_SHIPPING`, uegame/Source/uegame/DungeonEvidence.cpp:157–490):

| Verb | Purpose |
|---|---|
| `Dungeon.StartRun <seed>` | Start a run and anchor the seed chain (idempotent; re-anchor after chain drift) |
| `Dungeon.Descend` / `Dungeon.FloorStatus` | Queue a descend / floor·HP·enemy snapshot |
| `Dungeon.SetHP <v>` / `Dungeon.DescendThenDie` | Set player HP (0 triggers death) / same-tick descend+lethal compound probe |
| `Dungeon.Regen <seed>` | In-place regeneration (the M4 spike path) |
| `Dungeon.Spawn <seed> [tile]` / `Dungeon.WalkFar` | Single-floor spawn (refuses editor worlds) / navpath evidence + walk to farthest room |
| `Dungeon.CombatStatus` / `Dungeon.Attack` / `Dungeon.KillNearest` | Combat snapshot / one melee swing / kill nearest enemy |
| `Dungeon.TeleportToRoom <n>` / `Dungeon.FaceNearest` | Teleport to room n / face nearest enemy (for the forward-offset melee) |

Logs land in `uegame/Saved/Logs/uegame.log`; grep anchors: [RunStarted] [FloorConfig] [FloorStarted] [FloorCompleted] [RunWon] [RunFailed] [Stairs] [RoomClear] (anchor list quoted in the `1ec27c4`/`c7deca2` bodies).

**Outside the engine (independent re-verification).** No UE needed — any C++17 compiler:

```
./build.sh                                            # M1 demo CLI (→ ./dungeon)
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic m1_verify.cpp -o m1_verify && ./m1_verify 1000
g++ -std=c++17 -O2 m2_adapter_test.cpp -o m2test && ./m2test floors 7 3
```

**Determinism self-check, two commands.** `m2test floors 7 3` above prints the per-floor seeds and hashes; in PIE, run `Dungeon.StartRun 7`, descend floor by floor, and grep the log for planHash / enemyPlan — both sides must match bit-for-bit. This is exactly the path the M4 acceptance ran (`8a4929c` → `c7deca2`).

---

## 8. Repo map

```
dungeon.hpp             M1 core (frozen contract, tag m1-baseline)
m2_adapter.hpp          engine-agnostic adapter (hashes, placements, seed chain for M2/M3/M4)
m1_verify.cpp           independent verifier (public API only; user-supplied)
m2_adapter_test.cpp     adapter CLI (report/validate/determinism/enemies/floors)
main.cpp + build.sh     M1 demo CLI
m2_ue/M2_UE_README.md   M2 engine-integration deep dive (zh-CN: runtime-navmesh forensics, repro commands)
m2_ue/evidence/         6 evidence screenshots (provenance audited via MD5+timestamps, 6a9858f)
uegame/                 UE 5.8 project (Source/uegame runtime module, Source/uegameEditor MCP module,
                          Content/Data/CombatConfig.csv single source of numbers, Config/)
.github/workflows/      PR review automation (Claude review; CodeQL via repo default setup)
```

The review trail is an exhibit in itself: [PR #1](https://github.com/My-Denia/uegame/pull/1) (M2+M3 — multi-round Codex review, every finding closed) and [PR #3](https://github.com/My-Denia/uegame/pull/3) (M4 — pre-registered chain reproduced + independent 6/6 audit).

This repo currently has no LICENSE file (all rights reserved); open an issue if you want to reuse something.
