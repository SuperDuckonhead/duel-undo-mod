# C3 real-engine reconstruction cases

All six fixtures are public authored inputs for the installed real ocgcore + Lua scripts. No personal deck, runtime library, image or sound is copied. Each `.duel` contains InitialState, accepted ResponseRecords, target and final checkpoints. Their full host-only canonical state must never be sent to an ordinary player.

Resource digest for all cases: `bff81471fcd422aa17c700f4d770a65f49886369900bfb2bf7da73517f9a1858` (13,616 resolved scripts; 15,049 normalized card records). Card IDs and scripts were read and verified against the installed cards.cdb and ResourceView, not guessed.

Cards: 89631139 Blue-Eyes White Dragon; 46986414 Dark Magician; 37812118 Cup of Ace; 8267140 Cosmic Cyclone; 79759861 Tribute to the Doomed; 83968380 Jar of Greed; 88264978 Red-Eyes Darkness Metal Dragon.

All initial states have rule 5, LP 8000 each, draw-count 1, no-check enabled, 20 alternating Blue-Eyes/Dark Magician cards per deck. No-shuffle and shuffled cases draw five starting cards; effect scenarios have zero automatic starting cards and the public explicit placements listed below. Seed is the shown word repeated exactly 8 times (fixed core SEED_COUNT). Scenario name and parameter bytes are empty for all six. Response hex is the exact unpadded bytes; the driver pads to the fixed 256-byte core response buffer. `M` means Manual and `A` Automatic. The binary fixture is authoritative for each before-checkpoint and the complete insertion sequence.

## draw-random

Actual Cup of Ace script tosses a coin and draws two cards for the selected side; subsequent end-turn input also matches.

- Seed words: `[73, 73, 73, 73, 73, 73, 73, 73]`; options: `0x00050010`; no-check/no-shuffle: `True/True`.
- Accepted responses: 10; retained prefix: 0; target player/message: `0/11`.
- Target prompt: `0b00000000000196f740020002000196f7400200020000000000000100`.
- Target transcript SHA-256: `f42e3c788695607011ea350c07ef60d53f6271ad51b3fe95169f4895f3d8f4e2`.
- Target canonical SHA-256: `afc61cd2ba24272228ca62a9b18752689360d132b131981e3347c52f305d2cf0`.
- Fixture file SHA-256: `acafdd7560358671bbd2aec812da736e62a2636ceb4dd756003d6ae311eb0a12`.
- Explicit additions `(code, owner, controller, location, sequence, position)`: `[(37812118, 0, 0, 2, 0, 1)]`.

| Index | Player / origin | Prompt ID | Exact response hex |
| --- | --- | --- | --- |
| 0 | 0 / M | 11 | `05000000` |
| 1 | 0 / M | 18 | `000800` |
| 2 | 1 / A | 16 | `ffffffff` |
| 3 | 0 / A | 16 | `ffffffff` |
| 4 | 0 / A | 16 | `ffffffff` |
| 5 | 1 / A | 16 | `ffffffff` |
| 6 | 0 / M | 11 | `07000000` |
| 7 | 1 / A | 16 | `ffffffff` |
| 8 | 1 / A | 16 | `ffffffff` |
| 9 | 0 / A | 16 | `ffffffff` |

## initial-shuffle

Uses the actual host mtrandom Fisher-Yates routine once before recording insertions, same seed and reverse new_card insertion as SingleDuel. Rebuild uses only saved insertion order and does not call host shuffle.

- Seed words: `[42, 42, 42, 42, 42, 42, 42, 42]`; options: `0x00050000`; no-check/no-shuffle: `True/False`.
- Accepted responses: 8; retained prefix: 0; target player/message: `0/11`.
- Target prompt: `0b00000000000000000101`.
- Target transcript SHA-256: `9ac77a243d0d0e15a9b8924e6874f6cfd617ab7260c161294a183f960754ae51`.
- Target canonical SHA-256: `532bec891990233131d7c9bc6aa7739044ceb906a397e0713dffb553d0fd0fac`.
- Fixture file SHA-256: `eb86af227081ff46d8638bf0002c47d8bf3a1e3d9778b6b5301ac1cb823c9a72`.
- Player 0 actual ordered insertion codes: `[46986414, 89631139, 89631139, 89631139, 46986414, 46986414, 89631139, 89631139, 46986414, 46986414, 89631139, 89631139, 46986414, 89631139, 46986414, 46986414, 46986414, 46986414, 89631139, 89631139]`.
- Player 1 actual ordered insertion codes: `[46986414, 46986414, 89631139, 89631139, 46986414, 89631139, 89631139, 89631139, 46986414, 46986414, 46986414, 46986414, 89631139, 46986414, 89631139, 89631139, 89631139, 46986414, 46986414, 89631139]`.

| Index | Player / origin | Prompt ID | Exact response hex |
| --- | --- | --- | --- |
| 0 | 0 / M | 11 | `07000000` |
| 1 | 1 / A | 16 | `ffffffff` |
| 2 | 1 / A | 16 | `ffffffff` |
| 3 | 0 / A | 16 | `ffffffff` |
| 4 | 1 / M | 11 | `07000000` |
| 5 | 0 / A | 16 | `ffffffff` |
| 6 | 0 / A | 16 | `ffffffff` |
| 7 | 1 / A | 16 | `ffffffff` |

## lp-discard-cost

Actual Cosmic Cyclone has already paid 1000 LP at the target selection checkpoint. Continue to banish Jar of Greed, then activate Tribute to the Doomed, discard Dark Magician, and destroy opposing Blue-Eyes. LP remains 7000, hand is empty, target is in graveyard.

- Seed words: `[81, 81, 81, 81, 81, 81, 81, 81]`; options: `0x00050010`; no-check/no-shuffle: `True/True`.
- Accepted responses: 19; retained prefix: 6; target player/message: `0/15`.
- Target prompt: `0f00000101017c41010501080008`.
- Target transcript SHA-256: `b5c92022138fe43bff6645e15a933052e932c54d5c5368e2316e5488f9accb93`.
- Target canonical SHA-256: `9d70c0d5035fbf4bc96a9f97cf9446d3875d66b0010f5619ac750d75a8525993`.
- Fixture file SHA-256: `5744e7940b562a552015cfb8aaa008d38c5fe9bd96fdbdeef39d5f800d248689`.
- Explicit additions `(code, owner, controller, location, sequence, position)`: `[(8267140, 0, 0, 2, 0, 1), (79759861, 0, 0, 2, 0, 1), (46986414, 0, 0, 2, 0, 1), (83968380, 1, 1, 8, 0, 8), (89631139, 1, 1, 4, 0, 1)]`.

| Index | Player / origin | Prompt ID | Exact response hex |
| --- | --- | --- | --- |
| 0 | 0 / A | 16 | `ffffffff` |
| 1 | 1 / A | 16 | `ffffffff` |
| 2 | 0 / A | 16 | `ffffffff` |
| 3 | 1 / A | 16 | `ffffffff` |
| 4 | 0 / M | 11 | `05000100` |
| 5 | 0 / M | 18 | `000800` |
| 6 | 0 / M | 15 | `0100` |
| 7 | 1 / A | 16 | `ffffffff` |
| 8 | 0 / A | 16 | `ffffffff` |
| 9 | 0 / A | 16 | `ffffffff` |
| 10 | 1 / A | 16 | `ffffffff` |
| 11 | 0 / M | 11 | `05000000` |
| 12 | 0 / M | 18 | `000800` |
| 13 | 0 / M | 15 | `0100` |
| 14 | 0 / M | 15 | `0100` |
| 15 | 1 / A | 16 | `ffffffff` |
| 16 | 0 / A | 16 | `ffffffff` |
| 17 | 0 / A | 16 | `ffffffff` |
| 18 | 1 / A | 16 | `ffffffff` |

## multiple-chain-choices

Actual Jar of Greed on each side forms a two-link chain. Restore before the second activation choice, resolve both scripts, and verify each player drew one card and both traps reached graveyards.

- Seed words: `[93, 93, 93, 93, 93, 93, 93, 93]`; options: `0x00050010`; no-check/no-shuffle: `True/True`.
- Accepted responses: 6; retained prefix: 1; target player/message: `1/16`.
- Target prompt: `10010101000000000000000000007c4101050108000800000000`.
- Target transcript SHA-256: `e9f430adab0f4a3e815eb3a5c8d541b43dfcc97c39c4639ed9b944eaa2a23133`.
- Target canonical SHA-256: `7c7b201657583453584fcf755017d121a1d0f6cc6fb77b5ea46f3c996aad13b9`.
- Fixture file SHA-256: `f7c126749d9e30426375610e1de9a7126eb3ce38e8799c723e613ce25a9c11e6`.
- Explicit additions `(code, owner, controller, location, sequence, position)`: `[(83968380, 0, 0, 8, 0, 8), (83968380, 1, 1, 8, 0, 8)]`.

| Index | Player / origin | Prompt ID | Exact response hex |
| --- | --- | --- | --- |
| 0 | 0 / M | 16 | `00000000` |
| 1 | 1 / M | 16 | `00000000` |
| 2 | 0 / M | 16 | `ffffffff` |
| 3 | 1 / M | 16 | `ffffffff` |
| 4 | 0 / M | 16 | `ffffffff` |
| 5 | 1 / M | 16 | `ffffffff` |

## no-shuffle

No-check and no-shuffle both enabled; initial hand is Blue-Eyes / Dark Magician / Blue-Eyes / Dark Magician / Blue-Eyes. Two turns continue with matching deck order.

- Seed words: `[42, 42, 42, 42, 42, 42, 42, 42]`; options: `0x00050010`; no-check/no-shuffle: `True/True`.
- Accepted responses: 8; retained prefix: 0; target player/message: `0/11`.
- Target prompt: `0b00000000000000000101`.
- Target transcript SHA-256: `08b8c60dcd14c121ead58dc4508e12f397d71b603cb89142d6b49574a215a1c5`.
- Target canonical SHA-256: `966b6a84ec2a2af6811cdfb09e519c110300aebc053cc9a8c3d75ee3380e4971`.
- Fixture file SHA-256: `a731f0bee1c6079c2008cf670419de23143a6a5c73022d92b181ec2dcd09380c`.

| Index | Player / origin | Prompt ID | Exact response hex |
| --- | --- | --- | --- |
| 0 | 0 / M | 11 | `07000000` |
| 1 | 1 / A | 16 | `ffffffff` |
| 2 | 1 / A | 16 | `ffffffff` |
| 3 | 0 / A | 16 | `ffffffff` |
| 4 | 1 / M | 11 | `07000000` |
| 5 | 0 / A | 16 | `ffffffff` |
| 6 | 0 / A | 16 | `ffffffff` |
| 7 | 1 / A | 16 | `ffffffff` |

## once-per-turn

Actual Red-Eyes Darkness Metal Dragon summons Blue-Eyes. Another Blue-Eyes and free monster zones remain, but the consumed effect is unavailable. Restore before use and also after use (prefix 8), replay the same choices, verify the effect stays unavailable during that turn and returns on turn three.

- Seed words: `[117, 117, 117, 117, 117, 117, 117, 117]`; options: `0x00050010`; no-check/no-shuffle: `True/True`.
- Accepted responses: 16; retained prefix: 0; target player/message: `0/11`.
- Target prompt: `0b0000000112d1420500040000000112d1420500040020112d54000101`.
- Target transcript SHA-256: `1b6f3496367a67ea6ddb911053435820eddf57942dcabf2b14375b4b41c98705`.
- Target canonical SHA-256: `2b6f6fd34c9f3c87cf2e9234e4028e6f850bacc6596c76316225a884c2d5cc2f`.
- Fixture file SHA-256: `3061ddea167b1343db5c837b43817025775127af46f6892268b649d62721a9e4`.
- Explicit additions `(code, owner, controller, location, sequence, position)`: `[(88264978, 0, 0, 4, 0, 1), (89631139, 0, 0, 2, 0, 1), (89631139, 0, 0, 2, 0, 1)]`.

| Index | Player / origin | Prompt ID | Exact response hex |
| --- | --- | --- | --- |
| 0 | 0 / M | 11 | `05000000` |
| 1 | 1 / A | 16 | `ffffffff` |
| 2 | 0 / A | 16 | `ffffffff` |
| 3 | 0 / M | 15 | `0100` |
| 4 | 0 / M | 18 | `000401` |
| 5 | 0 / M | 19 | `01000000` |
| 6 | 0 / A | 16 | `ffffffff` |
| 7 | 1 / A | 16 | `ffffffff` |
| 8 | 0 / M | 11 | `07000000` |
| 9 | 1 / A | 16 | `ffffffff` |
| 10 | 1 / A | 16 | `ffffffff` |
| 11 | 0 / A | 16 | `ffffffff` |
| 12 | 1 / M | 11 | `07000000` |
| 13 | 0 / A | 16 | `ffffffff` |
| 14 | 0 / A | 16 | `ffffffff` |
| 15 | 1 / A | 16 | `ffffffff` |
