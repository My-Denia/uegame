# WARDENFALL RC Manual QA

Use only the packaged Shipping executable, normal keyboard/mouse input, and visible game information. Do not use logs or diagnostic state to make gameplay decisions. Stop a route at its first real blocker.

## Candidate

- Source commit: `1f449ca`
- Executable: `WARDENFALL-Windows-RC\uegame.exe`

## Gate A — Complete natural run

- [ ] Title explains the run premise.
- [ ] Secure or Challenge consequence is understandable before selection.
- [ ] Four required rooms clear on floor 1.
- [ ] A reward changes visible build state and player behavior.
- [ ] Lit exit is understandable and reachable.
- [ ] Floor 2 loads and pressure increases.
- [ ] At least two contract decisions occur across the run.
- [ ] Floor 3 clearly introduces the Warden.
- [ ] Warden guard, break, stagger, damage window, and death are readable.
- [ ] Victory result remains visible.
- [ ] Result reports elapsed time, rooms, and build.
- [ ] R begins a fresh run or Q exits.

Result: `PASS / NEEDS_FIX`

Elapsed:

Primary build:

Contracts:

First blocker and reproduction:

## Gate B — Identity coverage

Short routes may stop once the behavior is clearly established.

| Route | Required observation | Result |
| --- | --- | --- |
| Executioner | Player deliberately prepares and lands low-health finishing hits | |
| Tempo | Player changes behavior to maintain the hit chain | |
| Bulwark | Player times managed damage and a counter window | |
| Hybrid | Both selected behaviors remain visible and useful | |

## Gate C — Product flow regression

- [ ] Escape pauses and resumes.
- [ ] R requires confirmation during a live run.
- [ ] Escape cancels the restart/quit confirmation.
- [ ] Natural defeat remains visible and does not auto-restart.
- [ ] R after defeat starts a valid new run.
- [ ] Q confirmation exits the original process with no replacement process.
- [ ] No crash, input loss, collision trap, unreachable objective, or softlock.

## Evidence

For each route, record start/end time, chosen contracts/rewards, result, first blocker, and 3-6 screenshots at the relevant state. A continuous recording is useful but not required for this manual gate.
