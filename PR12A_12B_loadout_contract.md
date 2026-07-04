# M5 Build-Diversity — PR #12A / #12B contract

This document is the seam contract between **PR #12A** (the engine-agnostic loadout core, in
this repo now) and **PR #12B** (the future Unreal binding). #12A ships a pure, deterministic,
CTest-verified core and *nothing else*. #12B is specified here but **implemented by no code in
#12A**.

---

## Part 0 — What PR #12A delivers (and deliberately does not)

**Delivers** (all engine-agnostic, standard C++17, no engine types):

| File | Role |
| --- | --- |
| `m5_loadout.hpp` / `m5_loadout.cpp` | The core: `Affix`, `BaseStats`, `ResolvedStats`, `generate_offer()`, `resolve()`, `validate_affix_pool()`, domain-separated seeding, saturating cast. |
| `test_m5_loadout.cpp` | CTest `m5_loadout_determinism` — 14 counted sub-checks + 60000×8 fuzz. |
| `dump_m5_loadout_golden.cpp` + `golden_m5_loadout_seed7.txt` | CTest `m5_loadout_goldenvec` — byte-stable golden vector (seed 7, floors 1..3). |
| `diff_golden.cmake` + `.gitattributes` | Strict-byte golden differ (LF-pinned). |
| `CMakeLists.txt` (additive) | Two executables + two `add_test` gates. |

**Deliberately excluded from #12A** (these are all #12B):

- No UE binding — no `UObject`, no component, no `#include` of any engine header. The core does
  not even include `dungeon.hpp` / `m2_adapter.hpp` (only the *test* includes the latter, purely
  to prove RNG-stream isolation).
- No UI, no art, no assets.
- **No gameplay change.** `CombatComponent`, `FloorManager`, `DungeonSpawner`, `DungeonStairs`
  and `uegameCharacter` are untouched. `CombatConfig.csv` is untouched.
- No consumption of the layout / enemy / floor RNG streams. The loadout offer stream is derived
  from `domain_seed(run_seed, DOMAIN_LOADOUT_OFFER, floor_index, offer_index)`, a splitmix64 with
  a domain tag pinned distinct (compile-time `static_assert`) from every m2 magic constant. The
  determinism test proves the seed-7 layout/enemy/floor anchors are bit-identical with vs without
  thousands of interleaved offer draws.

**The current player numbers `15 dmg / 600 ms / 140 hp / 500 move` are a TEST FIXTURE only.** They
live in `test_m5_loadout.cpp` and `dump_m5_loadout_golden.cpp`, never in the production header —
`m5_loadout.hpp` carries no reference-base constant. (`15/600/140` are the real player values from
`CombatConfig.csv`; `500` move is a *synthetic* base — the CSV has no player move speed, only
`EnemyMoveSpeed=240`, which is an enemy stat and must not be used as the player base.)

---

## Part 1 — PR #12B: the Unreal binding specification

### 1. Runtime loadout state layer — `ULoadoutComponent` / `UUegameLoadoutManager`

Add a runtime state holder that owns the player's accumulated picks and the resolved stats. Two
viable shapes:

- `ULoadoutComponent` on the player pawn (`AuegameCharacter`) — natural if stats are read per-pawn.
- `UUegameLoadoutManager` as a `UGameInstanceSubsystem` / world subsystem — natural if the loadout
  must survive pawn respawns within a run.

Recommended: `ULoadoutComponent` on the character, mirroring how `UCombatComponent` /
`UHealthComponent` already hang off the pawn. It holds:

- `TArray<int32> ChosenAffixIds` — the run's picks (feeds `generate_offer`'s `chosen_ids` for
  `max_stacks` accounting, and maps to `Affix` objects for `resolve`).
- The current `m5::BaseStats` (built from `CombatConfig` + the synthetic/real move base — see §6).
- A cached `m5::ResolvedStats CurrentStats`, recomputed via `m5::resolve()` on every pick.

The affix **pool** is loaded from a DataTable/CSV (see §8) into a `TArray<m5::Affix>` once, then
validated with `m5::validate_affix_pool()` before first use.

### 2. Reward offer is generated on room-clear, not free stats at run start

The loadout must be *earned*, not granted. The reward moment is **floor clear**, which the codebase
already signals: `ADungeonSpawner::NotifyEnemyDead()` (`DungeonSpawner.cpp:212`) calls
`AreAllRoomsCleared()` (`DungeonSpawner.h:113`) and, when true, pokes the stairs via
`OnFloorCleared()` (`DungeonSpawner.cpp:240` → `ADungeonStairs::OnFloorCleared()`,
`DungeonStairs.cpp:72`).

At that clear moment #12B calls `m5::generate_offer(pool, ChosenAffixIds, RunSeed, FloorIndex,
OfferIndex, 3)` to produce the 3-choose-1 offer. The `RunSeed` is the same run seed `FloorManager`
already resolves (`[RunSeed]`); `FloorIndex` is the current 1-based floor. The offer is presented;
the player picks one; the pick is appended to `ChosenAffixIds` and `CurrentStats` is re-resolved.

### 3. `RewardPending` / `LoadoutChoicePending` vs the auto-descend conflict

**This is the load-bearing integration hazard.** Today the stairs are `descend-anytime`
(`bRequireFloorClearToDescend = false`, `CombatTypes.h:70`), and the clear path
(`NotifyEnemyDead` → `AreAllRoomsCleared` → `OnFloorCleared`) makes the stairs live the instant the
last enemy dies. A player standing on the pad descends **immediately** — which would skip the
reward offer entirely.

#12B must introduce a pending-choice gate:

- Add `bool bRewardPending` / `bool bLoadoutChoicePending` (name TBD) set true when `OnFloorCleared`
  fires *and* a reward is owed for this floor.
- While `bRewardPending` is true, the descend transition is **suppressed** (the stairs treat the
  floor as "not yet resolved" even though enemies are dead). This is a *new* reason-to-block that is
  independent of `bRequireFloorClearToDescend` — do not overload that flag; it governs a different
  policy (require-clear vs descend-anytime), whereas `bRewardPending` governs reward-not-yet-taken.
- The player makes the choice → `bRewardPending = false` → the descend transition unlocks (re-poke
  the stairs the same way `NotifyEnemyDead` does when a floor clears while the player is already on
  the pad — the existing re-trigger fix, commit `7efdff0`, is the pattern to mirror).

Without this gate the reward is a race the player usually loses. The determinism of the *offer*
(which affixes are shown) is unaffected — it is a pure function of `(RunSeed, FloorIndex,
OfferIndex, pool, ChosenAffixIds)` — only the *timing* of presentation needs the gate.

### 4. `CombatComponent::TryAttack()` reads resolved loadout stats

Today `UCombatComponent::TryAttack()` reads the raw config: `Cfg.PlayerAttackCooldown`
(`CombatComponent.cpp:29`) and `Cfg.PlayerAttackDamage` (`CombatComponent.cpp:63`). #12B rewires
these two reads to the resolved loadout:

- cooldown: `Cfg.PlayerAttackCooldown` → `ResolvedStats.attack_ms / 1000.0` (ms → seconds).
- damage: `Cfg.PlayerAttackDamage` → `ResolvedStats.damage`.

When no loadout component is present (e.g. a bare test map), `TryAttack()` falls back to the CSV
values so existing behavior is preserved. This is the *only* change to `CombatComponent` and it is
a #12B change — #12A does not touch it. Note `attack_ms >= 150` is a hard core invariant, so the
resolved cooldown can never drop below 0.15s no matter how much attack-speed is stacked (this is
what stops an infinite-DPS softlock).

### 5. MaxHP bonus rule — top up current HP

`MaxHpFlat` raises `ResolvedStats.max_hp`. `UHealthComponent` exposes `GetMaxHP()` / `MaxHP` /
`CurrentHP` (`HealthComponent.h:63/66`) and is initialized via `Init(float InMaxHP)`.

**First-version rule (recommended): when MaxHP increases by Δ, add the same Δ to current HP.** So
picking `+30 MaxHP` at 80/140 HP yields 110/170, not 80/170. Rationale: a build pick should feel
like an immediate reward, not "you must now heal to use it." Implementation: on re-resolve, compute
`deltaMax = newMax - oldMax`; set `MaxHP = newMax` and `CurrentHP = min(CurrentHP + max(deltaMax,0),
newMax)`. A MaxHP *decrease* (not expected from positive affixes, but possible in theory) clamps
`CurrentHP` down to the new max and does not refund. This needs a small `UHealthComponent` setter
(e.g. `SetMaxHP(float, bool bTopUpCurrent)`); today only `Init` sets max.

### 6. MoveSpeedPct — first version options

Player move speed is currently **hardcoded**, not data-driven: `AuegameCharacter` sets
`GetCharacterMovement()->MaxWalkSpeed = 500.f` (`uegameCharacter.cpp:41`). Two first-version options:

- **Option A (defer): disable/hide `MoveSpeedPct`.** Ship #12B with the three stats that already have
  a clean data path (damage, attack interval, MaxHP) and keep `MoveSpeedPct` out of the live pool
  (weight 0 or omitted). Lowest risk; the core already supports it.
- **Option B (datafy): make player move speed a real base and write it back.** Add a player move
  base (to `CombatConfig` or the loadout base), seed `ResolvedStats.move_speed` from it, and on
  re-resolve set `GetCharacterMovement()->MaxWalkSpeed = ResolvedStats.move_speed`. This replaces the
  hardcoded `500.f`. Note the golden's `500` synthetic base was chosen to match this future default.

Recommended: **Option A for the first #12B**, promote to Option B once move speed is datafied.

### 7. `Dungeon.LoadoutStatus` — new shipping-gated forensic verb (16 → 17)

Add `Dungeon.LoadoutStatus` to `DungeonEvidence.cpp`, inside the existing
`#if !UE_BUILD_SHIPPING` block, following the established `FAutoConsoleCommandWithWorldAndArgs`
pattern. It prints the current loadout: `ChosenAffixIds`, per-kind totals, and the resolved
`damage / attack_ms / max_hp / move_speed`, so PIE forensics can assert build state from logs
(anchor e.g. `[LoadoutStatus]`).

This raises the forensic-verb count from **16** to **17**. The README currently states "取证动词现
为 16 个" — #12B must update that line and the verb table to **17** in the same PR that adds the verb
(keep the pinned-count invariant and its doc statement in sync).

### 8. Affix DataTable / CSV missing policy — mirror `CombatConfig`

The affix pool source (DataTable or CSV) follows the exact policy `CombatConfig::LoadOnce`
(`CombatConfig.cpp`) already uses for a missing/malformed table:

- **Shipping / Test:** `UE_LOG(LogTemp, Fatal, ...)` and abort — never ship wrong or empty affix
  data silently (`CombatConfig.cpp:53` is the template).
- **Editor / dev:** `UE_LOG(LogTemp, Warning, ...)` and fall back to a safe built-in default pool so
  the editor stays usable (`CombatConfig.cpp:58` template).

Additionally, whatever pool is loaded must pass `m5::validate_affix_pool()` before first use; a pool
that fails validation is treated the same as a missing table (Fatal in Shipping/Test, warn+fallback
in Editor). Validation rejects id collisions, non-positive-weight-only pools, out-of-range
magnitudes/max_stacks, and **unknown affix kinds** — e.g. a stale numeric enum from an out-of-sync
table producing `static_cast<AffixKind>(4)` — so malformed rows can never reach `generate_offer()` /
`resolve()` (where an unknown kind would otherwise silently no-op). Like `CombatConfig`, the loader should be process-level **LoadOnce** cached — meaning an
affix-table edit requires an editor restart to take effect, consistent with the existing CSV
discipline.

---

## Acceptance carried from #12A into #12B

#12B must not regress any #12A gate: `m1_validate`, `m2_world_validate`, `m3_enemy_determinism`,
`m4_floor_determinism`, `m5_loadout_determinism`, `m5_loadout_goldenvec` all stay green, and the
seed-7 layout/enemy/floor anchors stay byte-unchanged. The UE-side wiring is validated separately by
MCP-driven PIE forensics (the `Dungeon.LoadoutStatus` verb from §7), not by the engine-agnostic CTest
gates.
