# WARDENFALL

WARDENFALL is a compact third-person action roguelite built in Unreal Engine 5.8. A run crosses three procedural floors, asks the player to choose safer or harder room contracts, grows one of three behavior-changing builds, and ends with a guarded Warden encounter rather than another ordinary clear room.

## What the player does

- Read the on-screen objective and clear the required rooms.
- Choose **Secure** for immediate recovery or **Challenge** for stronger enemies and a larger payoff.
- Shape an **Executioner**, **Tempo**, **Bulwark**, or hybrid build from readable reward choices.
- Adapt target priority and positioning to Grunts, fast Runners, heavy Brutes, and the Warden.
- Break the Warden's guard, capitalize on the stagger window, and reach a persistent victory result.

## Build identities

- **Executioner** rewards deliberate finishing hits against low-health targets.
- **Tempo** rewards keeping an attack chain alive through continued contact.
- **Bulwark** converts managed incoming damage into a readable counter window.
- **Hybrid** routes preserve the active behaviors of their component builds rather than collapsing them into one generic damage multiplier.

## Product presentation

The demo uses a deliberately abstract arena language made entirely from Unreal Engine primitives and procedural audio:

- deep navy floors and walls;
- cyan navigation and safe-state cues;
- amber choices, exits, and commitment moments;
- red damage and failure states;
- distinct enemy silhouettes and colors;
- modal title, pause, confirmation, reward, defeat, and victory flows;
- live HUD values read directly from authoritative gameplay state.

No paid or third-party art/audio asset is required by this candidate.

## Controls

| Input | Action |
| --- | --- |
| WASD | Move |
| Mouse | Look |
| F | Attack |
| 1 / 2 / 3 | Choose a contract or reward |
| Escape | Pause/resume or cancel a confirmation |
| R | Restart, with confirmation during a live run |
| Q | Quit to desktop, with confirmation |
| Enter / Space | Begin |

## Engineering result

The gameplay runtime is backed by frozen deterministic generation/loadout/encounter cores and additive Unreal integration:

- seeded procedural dungeon generation;
- deterministic enemy placement, room roles, and offer contracts;
- CSV-driven combat and run pacing;
- explicit room, exit, reward, finale, and result state;
- Shipping-only product flow with non-Shipping diagnostic commands compiled out;
- Windows package with loose staged gameplay data verified byte-for-byte against source.

The agent engineering harness is a development method, not part of the player's product. It supplied plan, build, test, evidence, and audit discipline; it is not needed to launch or complete the Shipping game.

## Verified candidate

- Product source commit: `1f449ca` (`Polish WARDENFALL product shell and presentation`)
- Engine: Unreal Engine 5.8
- Core tests: 16/16 passed
- Full Development rebuild: passed
- Full Shipping rebuild: passed
- Windows cook/stage/package/archive: passed
- Shipping launch/start/contract/quit-confirmation smoke: passed
- Staged `CombatConfig.csv` SHA-256 equals source:
  `55FC5112BA4C08F910CFFC5FA1039FDB5787E47BDD5BF4C3E64A5AFA606EF306`
- Shipping binary scan for forensic commands and Model Context Protocol identifiers: no matches
- Frozen M1-M7 source/golden diff: empty

## Honest boundary

This candidate has not yet received a post-polish, screen-only human recording of an uninterrupted three-floor Warden victory, nor final Tempo/Bulwark/hybrid manual coverage. Trailer and long-form gameplay footage are therefore capture-ready inputs, not completed evidence. The final public release remains an owner decision after that manual QA.
