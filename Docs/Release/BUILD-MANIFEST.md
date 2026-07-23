# WARDENFALL Windows RC Build Manifest

- Export time: `2026-07-23T22:24:39+08:00`
- Product source commit: `1f449ca6200d71ff5bd30889cbc97ea849683260`
- Signed commit verification: good signature for `pjyqifei@gmail.com`
- Build target: Unreal Engine 5.8, Win64 Shipping
- Distribution entry point: `uegame.exe`
- Debug symbols: retained in the private package archive; excluded from the RC distribution copy

## File hashes

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `uegame.exe` | 171520 | `34F5260FF44A5DB7CE7C2E9BFD78DF6854F5BCFA1BF1EB8910E41006C1959F2C` |
| `uegame/Binaries/Win64/uegame-Win64-Shipping.exe` | 172355072 | `539BD13B534635D582B125849BACC35AB606C8FD0D861E79CB835EDC2301CEB4` |
| `uegame/Content/Data/CombatConfig.csv` | 796 | `55FC5112BA4C08F910CFFC5FA1039FDB5787E47BDD5BF4C3E64A5AFA606EF306` |
| `uegame/Content/Data/EncounterWeights.csv` | 235 | `39E6AB680FB8CB9617E5613FB3E93362931715C30BFCDD2836B9A6C3E7217977` |
| `uegame/Content/Data/EnemyArchetypes.csv` | 185 | `C2BFFA2731A7F5C035C24A9311DBB3FEECCABCB4FC687469D6DD75473A8EF6B1` |
| `uegame/Content/Paks/uegame-Windows.pak` | 11437950 | `78FA680EA9BF0D5BF7960B5D7C2261AA5DE6BB55A1C42F650F7F562966A50653` |
| `uegame/Content/Paks/uegame-Windows.ucas` | 263179136 | `CCD52BCF2D1F6085D0891217C3A5659EF3E585C2FA436B3B3D7BD3C736D32E8E` |
| `uegame/Content/Paks/uegame-Windows.utoc` | 225343 | `E8BB77BA72F33C9DA3D0A78705D7396ECB76B881E2EB764ACF20A503C8343771` |

## Verification

- 16/16 Core CTests passed.
- Full Development rebuild passed.
- Full Shipping rebuild passed.
- Cook/stage/package/archive passed.
- Staged `CombatConfig.csv` is byte-identical to source.
- Package path scan found no Model Context Protocol or `DungeonEvidence` artifact.
- Shipping executable string scan found no forensic command or MCP identifier.
- Shipping title/start/contract/quit smoke passed.
- Frozen M1-M7 source/golden diff was empty at candidate creation.
