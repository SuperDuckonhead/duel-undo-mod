# Undo network protocol and server admission

Status: codecs, actual client/menu, authoritative host, private AI, player restoration and retained-branch terminal replay are implemented and tested locally. Two-device LAN and manual visual acceptance remain open.

## Outer reservation

On 2026-09-10 the entire fixed client/gframe/network.h was inspected, including
all CTOS and STOC definitions. Neither direction assigns 0x7e (126).
network.h assigns 0x7e to CTOS_UNDO/STOC_UNDO in this fork. Internal
WireKind values do not reserve existing outer protocol IDs. The maximum encoded
envelope is 49,231 bytes and fits the baseline MAX_DATA_SIZE (65,534).

## Envelope v1

All integers are unsigned little endian, emitted field by field. No C++ struct
layout or native memcpy serialization is used.

| Offset | Field | Bytes |
| --- | --- | --- |
| 0 | version = 1 | 2 |
| 2 | kind = 1..14 | 1 |
| 3 | session | 16 |
| 19 | base epoch | 8 |
| 27 | request | 8 |
| 35 | targetIndex | 8 |
| 43 | targetDigest | 32 |
| 75 | payload length | 4 |
| 79 | payload | 0..49,152 |

Kinds: Hello 1, Response 2, Request 3, Consent 4, Prepare 5, Ready 6,
Commit 7, CommitAck 8, Resume 9, Abort 10, AbortAck 11, Game 12, Status 13, RequestRejected 14.
Unknown kind/version, every truncation, trailing bytes, and oversize lengths
fail explicitly. Encode applies the same limits. Generic payload bytes are
opaque here; adapters must validate each message kind's payload schema and
allowed direction/connection before calling the coordinator.

NewSessionId uses BCryptGenRandom with the Windows system RNG; Linux uses
getrandom. Failure throws, with no pseudorandom/time fallback. Create a fresh
session for each duel. IsCurrent checks session + installed game epoch only;
SameKey additionally checks request + target index + target digest. Neither
function authenticates a network peer.

## Capability data

Hello payload is exactly 99 bytes: version u16, engine fingerprint 32 bytes,
rules fingerprint 32 bytes, logical-resource fingerprint 32 bytes, room mode
u8 (1 consent LAN, 2 loopback free). Images/audio are intentionally absent.
Missing Hello, unsupported version/mode, or differing engine/rules/logical
resources fails compatibility with the mismatching item. Resource fingerprint
construction remains the pinned-resource adapter's responsibility. A legacy
peer cannot satisfy this capability check; optional NetServer admission rejects create/join without an offer and ready/start without a confirmed challenge. Loopback binding and checking the accepted socket's peer
address are mandatory before selecting free mode; a room flag or a claimed
nickname/address is insufficient.

## Fragment codec and visibility boundary

Fragment payload is index u32, count u32, total byte length u32, then chunk.
All are little endian. Chunks have a canonical capacity of 49,140 bytes;
only the final chunk can be shorter. Empty streams are represented by one
empty chunk. Maximum assembled size is 16 MiB (explicit rejection beyond it).
Index must start at zero and increase by one, count must exactly match total
length, and every fragment must match the first fragment's count/length.
Missing, duplicate, reordered, truncated and inconsistent fragments fail.
Malformed Add permanently poisons that assembler; create a new one to retry.
Finish rejects incomplete data; it does not make missing fragments successful.

The caller MUST bind one assembler to one exact TxKey and authenticated
recipient seat before Add, reject any other envelope key/recipient, and feed
only that player's filtered restore stream. The byte codec cannot establish
visibility or distinguish a same-length cross-stream substitution by itself.
N3 must define the player restore material, create/check its separate visible
stream digest, and validate the complete target model BEFORE Ready. Never send
a host replay, both decks, hidden identities, or canonical full-state hash.
targetDigest is separate: hash ONLY a public target description plus transaction
identity. The authoritative adapter must not hash canonicalState, prompt bytes
containing private identities, or the host transcript into targetDigest.

## Coordinator contract

All calls occur on the host duel thread. nowMs comes from a trusted monotonic
clock (nonnegative int64 milliseconds); backward timestamps are rejected.
The event loop calls Tick before dispatching incoming readiness/ack events.
Consent expires at elapsed >= 30,000 ms, including an approval at exactly the
deadline. Preparing, Committing and Aborting also use 30,000 ms watchdogs in
this foundation. Preparing timeout requests abort; uncertain Commit or missing
abort confirmation enters PausedFailed. WaitBoundary has no timer: the engine
may finish the current resolution, and must report completion via Boundary.

Request accepts only an unbound key (zero targetIndex and targetDigest).
requester is the authenticated connection's seat 0 or 1. Request IDs are host
session-wide, positive and strictly increasing; adapter assigns/coordinates
this order, and clients must not independently assume overlapping counters.
UINT64_MAX may be used once; wrap is rejected. An epoch at UINT64_MAX cannot
start another transaction.

Every new Request enters WaitBoundary. At the safe boundary the host adapter
recomputes DuelHistory::Target(requester), confirms the duel is not finished,
and constructs AuthoritativeTarget from that history entry. Boundary binds
its index/public digest/checkpoint clock; the structure is a trusted internal
argument, NEVER a deserialized client request. The requester field must match.
Missing target, wrong requester or finished duel cancels. An immediately safe
request still uses Request followed by Boundary in the same host-thread turn.

Boundary receives the clock snapshot taken when the host freezes input/timers.
The host retains original prompt/core/model/history while preparing. Clock()
remains that frozen value after rejection/timeout/confirmed abort, and changes
to the target checkpoint clock only after both CommitAck. Apply it before
releasing input. A cancellation while waiting has no frozen-clock restore
obligation; the adapter must distinguish that from cancellation after Boundary.
Waiting allows the already-running engine resolution to reach its boundary,
but AcceptsGameInput rejects new player responses during every active state.

Consent is accepted only from the other authenticated seat and the complete
bound key. Each new successful transaction requires fresh consent in LAN mode.
Ready has its own two-seat mask, independent of CommitAck and AbortAck.
The host seat may become Ready ONLY after the candidate core, display and AI
(if used) are valid and the eventual ownership/history switch is prepared to
complete without failure. This foundational state machine does not perform or
verify that switch. C4/N3 adapters must meet that precondition.

Both Ready produce Commit with the original base-epoch key and an 8-byte
little-endian installed epoch (= base + 1) payload. Each CommitAck must carry
that same complete base key and the installed epoch in its payload (parsed by
the adapter into the method argument). Two distinct seats must acknowledge
before Epoch advances, target clock is selected and Resume is emitted.
Resume keeps the original base TxKey and carries installed epoch u64 too.
Ordinary game input uses the installed epoch; active control messages use
the full base key. Client adapters must retain their active transaction across
the local install so that Resume is not discarded by a game-epoch filter.

Before Commit, preparation failure produces Abort and waits for both distinct
AbortAck to establish old state restored. Consent rejection/timeout cancels
directly because no candidate has been exposed. Abort payload is a one-byte
reason: 1 unavailable/finished target, 2 consent timeout, 3 rejection,
4 preparation failure/timeout. Final cancellation is also Abort; receiver
handling must be idempotent. Fail with commitMayHaveEscaped, any failure after
Commit, commit-ack timeout, or abort-ack timeout stays PausedFailed. No later
ack unpauses it and no new Request is admitted; reconnect recovery is absent.

TakeOutgoing drains broadcast control intents; it performs no network/UI/core
or file work. Adapters route to both seats (including host-local handling).
The latest active intent may be resent for an exact duplicate request. A
64-result cache replays completed Request/CommitAck outcomes without repeating
restore/history truncation. Cache eviction never enables execution of an old
request: the session high-water request ID still rejects it. Cache replay is
suppressed while another transaction is active; clients must also reject
terminal intents for unrelated transactions.

## Implemented live bindings and remaining acceptance

The current client/menu exchanges capabilities and creates authoritative UndoDuel
sessions. Unwrapped duel responses are rejected; gameplay and private AI outputs
use installed-epoch/prompt gates. Public target digests, retained core ownership,
per-player model/widget preparation, commit barriers, clock restoration and the
explicit loopback listener policy are connected to production paths.

Initialized Game and same-machine two-client TCP tests cover these bindings,
including consent refusal/approval and preparation failure. Actual private AI
and final-branch replay paths are tested separately. Two-device LAN and human
visual acceptance remain open; see tests/local-acceptance.md and acceptance.csv.

## Production admission seam

NetServer StartServer takes an optional Hello. Its configured mode is checked against the actual bound socket; free mode requires exactly 127.0.0.1 and accepted peers in 127/8. The client advertises with zero session, receives the host session/mode challenge (request 0), echoes it exactly, then receives confirmation (request 1). Engine/rules/resources match throughout. Ready/start are closed until confirmation. Unconfirmed connections expire after 30 seconds. An ordinary server with no capability keeps the baseline protocol.

Game packets carry the session/installed epoch, current prompt in request, consecutive per-recipient packet sequence in targetIndex and zero digest. Fragmented raw STOC is limited to 1 MiB; malformed current streams poison assembly until an authenticated reset, while old-session/epoch and completed duplicates are ignored. Response carries Manual/Automatic origin, u16 length and 1..256 bytes; a network peer cannot claim Bot origin. Status is 36 bytes containing state, eligibility, prompt/time players, next request ID, current prompt ID and both millisecond clocks. These codecs do not themselves wire the client or perform a restore.

## Directed request rejection and room policy

RequestRejected (14) is host-to-requester only. It echoes the exact submitted
unbound key (current session/epoch, nonzero request, zero targetIndex/digest)
with one byte: 1 Busy, 2 Unavailable. Invalid/old-epoch requests are ignored.
It does not cancel, acknowledge or resume the other selected transaction.
A client clears only its own matching pending request, keeps another active
consent/prepared transaction frozen, and shows a persistent busy notice.
Same-request duplicate active requests still retransmit their original control.

Consent text uses only public requester seat and targetIndex+1. It does not
include private prompt/card data. A response already queued by the UI closes
undo admission until a new input boundary or the actual rejection/retry boundary.

Fixed legacy STOC_ErrorMsg uses discriminator 0x7e for explicit public policy
notices. Code bit31 means fatal; low values are Tag1, Observer2, Full3,
Incompatible4, MissingMod5, HandshakeTimeout6, unsupported AI Match7. These notices bypass the
gameplay/AI callback journal and remain deliverable while a transaction is
paused. Fatal replies drain before closing, bounded by a two-second deadline.
Undo rooms accept exactly two duelists in Single or human Match mode;
AI Match, Tag and observers are rejected before unsupported participation begins.

## Match game boundaries

Restore profile 2 adds host-to-client RoundStart (15); envelope framing v1 and
the 99-byte Hello v2 layout are unchanged. A profile 1 peer fails the restore
format compatibility check. RoundStart key is `{roomSession, previousEpoch, 0,
0, zeroDigest}`; payload is little-endian u64 nextEpoch followed by u8 game
number 2 or 3. nextEpoch must equal previousEpoch+1 without overflow. The first
game starts implicitly at number 1, epoch 0. Epoch also advances on undo commits
and never resets between games.

Native Match scoring, side validation, readiness, loser turn choice and final
termination remain in SingleDuel. Each finished game exports its final replay
before clearing undo history. The old core/epoch continues to carry ordered
replay and native siding/start/turn-choice packets. At the valid next TPResult,
the host prepares a fresh per-game state, emits RoundStart at the old epoch,
then installs the new state and emits MSG_START at the new epoch. The room
session and participant identities persist; history, prompts and streams do not.

The client requires a normal game end, CHANGE_SIDE and its native DUEL_START
before accepting the exact next transition. Full STOC_DUEL_END is terminal.
An uncertain restore cannot be reopened by a round transition. The client clears
old models, prompts, commands and pending response identities, resets stream
sequence to zero, and waits for the new authoritative prompt/status before
enabling gameplay. Native side-deck and turn-choice responses keep their original
dispatch. Old-epoch gameplay, consent and undo controls cannot affect the next
game; no rollback crosses a finished-game boundary or changes side-deck edits.


## Responses already in flight when undo starts

A client that has queued a response cannot also request undo for that prompt.
If another participant's transaction arrives before transport Poll, the client
retains its unsent exact-token response while frozen. Final Abort permits its
first delivery; epoch replacement and terminal/failure discard it.

For a response already sent when the other request freezes the host, the host
keeps at most the first valid Manual/Automatic response from the current actor,
bound to the original endpoint, session/epoch/prompt and active transaction.
No input is applied during freeze. Only a completed Abort whose final control
was successfully broadcast can drain that value through normal current-token,
player, state, clock and core-legality checks. Commit or terminal/disconnect/
uncertain failure discards it. Conflicting or duplicate inputs cannot replace
the saved value. Clients do not blindly resend previously delivered responses.
