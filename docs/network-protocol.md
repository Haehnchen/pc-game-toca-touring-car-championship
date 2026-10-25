# Native multiplayer protocol

Canonical evidence: unchanged `references/toca-1.1a/tourcars.exe`,790016 bytes,
CRC32 `8ce56d41`; decompiler export `references/decompiled/tourcars.c`.
Addresses below are image virtual addresses, with the common `004` prefix omitted.
No original packets or memory captures are committed. Native vectors use authored
values. Implementation status and acceptance remain in
[the multiplayer plan](implementation/04-multiplayer.md).

## Original transport and identities

| Original evidence | Original behavior | Native decision |
| --- | --- | --- |
| `493da4` / DirectPlay3A vtable `+68` | `Send(local DPID, destination, flags, buffer, size)`; destination0 broadcasts, flag1 requests guaranteed delivery | ENet host/client routing; transport `ConnectionId` is distinct from player and actor IDs |
| `493dfe` / vtable `+64` | `Receive(DPRECEIVE_ALL=1)` returns payload,size,destination,origin DPID; origin0 denotes a system event | Endpoint event owns bytes and actual connection identity; application origin cannot override that identity |
| `493e6c` | Filtered receive uses `DPRECEIVE_TOPLAYER=2` | No separate receive API needed |
| `DPLAYX.dll` ordinals 1/2; no Winsock imports | `DirectPlayCreate` / `DirectPlayEnumerateA`; networking uses DirectPlay providers only | ENet replaces the provider; no Wine/DirectPlay runtime dependency |
| `49385b`, GUID `4ac4f0`; `4938d4`, GUID `4ac690` | `IDirectPlay3A` IID `133efe41-32dc-11d0-9cfb-00a0c90a43cb`; application GUID `4730bda0-ebbc-11d0-bb84-00a024f00d11` | Not used natively |
| `493fd0`, `493650`, `4677c0` | Enumerates provider names/GUIDs, shows up to five; serial/modem/IPX labels do not establish an IPX-only protocol | Native Host/Join with IPv4:port replaces provider browser |
| `466713`, `466b70`, `466e80` | Lobby types `0..3`; 36-byte status and 12-byte request use guaranteed delivery; 68-byte setup/start and 20-byte AI option are sent five times, non-guaranteed (`46767b`, `46770f`) | Native lobby traffic is reliable ordered |
| `43b2d0`, `43b450`, `43c3f0`, `43c4a0` | Eight-byte packed controls | Fixed-width native codec |
| `43c5a0`, `43c6e0` | 84-byte car packet carries controls; nine signed components clamp to `[-0x10000,+0x10000]` | See N01b |
| `43b7a0`, `43bb40` | State type `0x1d` addresses a player ID mapped to a car; `0x1e` addresses a car index | Native ActorState uses an actor index |
| `43a7d0`, `43cf20`, `43bc70`, `43ce30/70` | Per-peer state interval adapts to a four-sample RTT estimate: level0 = 1 ms, levels 1–31 = `(level+1)*33` ms, level 32 = 3000 ms | See N01b; do not infer a fixed 30 Hz rate or server-authoritative physics |
| `441680`, `441720`, `43cba0`, `43bd70`; `43b4c0`, `43bfb0`, `43b670` | Pause notifies and keeps receiving; 501 ms heartbeat gate, 1000 ms response probe, 12000 ms timeout; pause extends deadlines by 50000 ms | See N01b |
| `493a30`, caller `476bfb` | Descriptor80 bytes, flags`44`, maximum8 players, Open(create=2) | Host included in eight humans; one local human per process |
| `493ae5`, `493c58`, `493ce2`, `493bcb` | Open(join=1), CreatePlayer writes local DPID, DestroyPlayer refreshes enumeration, Close | Join in lobby only; explicit leave and host-loss cleanup |
| `493d18`, `493f81` | GetPlayerData flag2 identifies host; EnumPlayers enumerates names/DPIDs | Host assigns stable nonzero `PlayerId`; no wire transport handles |
| `4639b6`, `463bd3` | Each local roster places its own DPID at index0, other active humans at1+; separate shared DPID order`52c8b4` maps to each entry's`+10` actor index | Wire roster order is shared; local input ownership refers to player ID, never vector index0 |
| `43c9e0`, `43ca00` | Every peer sends own car. Host also sends inactive human cars and AI actors in `[humanCount,humanCount+AIcount)` | Preserve distributed human ownership; host owns AI/takeover states |
| `466956`, `43c070` | System3/5 refresh create/destroy;`31` session loss;`101` promotes successor host and announces change (`43cb90`) | Host loss ends native session; automatic migration deferred |
| `493c00`, `46731c` | SetSessionDesc join-disable bit`20` before race start | New connections cannot claim an active race slot |

The protocol combines compact inputs with state correction; it is neither proven
deterministic input lockstep nor snapshots alone.

The network race car count is active humans plus optional `8 - humans` AI,
not the offline16-car default (`447950`, `466d07`). The original compatibility
float1.0 disables AI; normal1.5 permits it. The native version handshake replaces
this compatibility mechanism. Car and gearbox settings are per human. Duplicate
cars are permitted by the receive/setup paths: `466713` copies selections directly,
and `4639b6` copies them into the roster. `4761bc`'s four-copy limit counts only
earlier local split-screen selections; Linkup selects local player0 and thus does
not implement a network-wide uniqueness rule. Native lobby permits duplicate cars.

## Complete original application message inventory

`43a3f0` initializes all race tags. Every application send uses `493da4`;
`466713` dispatches lobby traffic and `43b510` dispatches race traffic only for a
recognized active DPID. There is no tag`13` case. Original padding/trailing bytes
are not native fields. G means guaranteed; U means non-guaranteed.

| Phase | Type | Bytes | Send caller | Receive handler | Destination | Flags | Purpose / evidence | Unresolved question |
| --- | --- | ---: | --- | --- | --- | --- | --- | --- |
| Lobby | `00` |20|`46770f`|`466ce6/466d07`|broadcast|U,5 repeats|AI option,confirmed|none |
| Lobby | `01` |36|`466b70`|`466713` case1|broadcast|G|human selection/status,confirmed|none; individual fields below |
| Lobby | `02` |12|`466e80`|`466713` case2|broadcast|G|request fresh status,confirmed|none |
| Lobby→loading | `03` |68|`46767b`|`466c33`|broadcast|U,5 repeats|host settings/shared order,confirmed|loading retry lifecycle below |
| Loading/race | `10` |20|`43c3a0`|`43b620`|broadcast|U|host seed/loading heartbeat,confirmed|fresh scene resets; grid staging preserves |
| Loading/race | `11` |13|`43c3d0`|`43b780`|broadcast|U|client loaded heartbeat,confirmed|none |
| Driving | `12` |12|`43c3f0`|`43b650`|each active remote DPID|U|changed packed controls,confirmed|named high control bits |
| Driving | `14` |19|`43ca70`|`43bb60`|broadcast|U|17 car visual-damage variants for slot byte`+1`,confirmed|none |
| Driving | `15` |12|`43cac0`|`43bbd0`|broadcast|U|signed adaptive interval delta byte`+1`,confirmed|none |
| Driving | `16` |16|`43cae0`|`43bbf0`|broadcast|U|remove DPID`+4`, AI takeover,confirmed|none |
| Driving | `17` |12|`43cb00`|`43bc50`|target DPID|U|probe ID word`+4`,confirmed|none |
| Driving | `18` |8|`43cb50`|`43bc70`|target DPID|U|echo probe ID word`+2`,confirmed|none |
| Driving | `19` |11|`43cb80`|`43bd50`|broadcast|U|bonus-car8 tank shot;receiver`406a40(sender,0)`,confirmed|separate pending tank-projectile gameplay consumer |
| Race | `1a` |11|`43cb90`|`43bd60`|broadcast|U|sender becomes host DPID,confirmed|native migration deferred |
| Race | `1b` |20|`43cba0`|`43bd70`|broadcast|U|pause bool word`+4`,confirmed|none |
| Race | `1c` |12|`43cc10/43cc40`|`43be20`|broadcast|U|subtype1 gear/reverse pedal; subtype2 finish time,confirmed|native reliable ordering adaptation below |
| Driving | `1d` |84|`43c4e0`|`43b7a0/43b7d0`|each active remote DPID|U|DPID`+4` mapped to actor, then correction,confirmed|none; full field map below |
| Driving | `1e` |84|`43c4e0`|`43bb40/43b7d0`|each active remote DPID|U|actor index`+4`, host AI correction,confirmed|none; full field map below |

## Lobby fields and phases

Offsets below are original payload offsets, widths in bytes, little endian.
Integer fields are32-bit unless stated. `config` means `PTR_DAT_004b0660`.

| Type | Offset / mask | Width | Signedness / scale / range | Producer | Consumer | Legal original sender | Meaning / status |
| --- | --- | ---: | --- | --- | --- | --- | --- |
| All |`+0`|1|unsigned tag0..3|send callers above|`466713`|session DPID|confirmed tag; other header bytes unused |
|00|`+4`|4|bool|`46770f`|`466ce6`|host|AI enabled;`466d07` chooses0 or8−humanCount |
|01|`+4`|4|car0..9|`466b70`,config`+dc`|`466713`→roster`+4`|selection owner|car ID,confirmed |
|01|`+8`|4|gearbox1/2|config`+e0`|roster`+8`, `463e64`, `40a770`|selection owner|manual/automatic,confirmed; native bool maps through existing setup |
|01|`+c`|4|sensitivity1..3|config`+100`|roster`+c`, `463e77`, `418b60`|selection owner|steering sensitivity,confirmed |
|01|`+10`|4|bool|`466b70` argument|roster`+10`|selection owner|active/committed selection,confirmed |
|01|`+14`|4|bool|`4687d2`|roster`+14`, `467278`|selection owner|CD-drive eligibility,confirmed; start enabled if some active player has it |
|01|`+18`|4|bool|config`+a4`|`466713`|host advertises1|host marker; clears stale host flags |
|01|`+1c`|4|track−1 or0..9|config`+1c` if active host|`52e6c4`|host|host track;−1 when absent/uncommitted |
|01|`+20`|4|IEEE float1.5|constant`3fc00000`|`466d07` compatibility|selection owner|compatibility marker,confirmed |
|02|none|0|none|`466e80`|calls`466b70`|session member|request selection/status |
|03|`+4`|4|track0..9|config`+1c`|config`+1c`|host|confirmed |
|03|`+8`|4|laps1/2/3/5/10/15/20|config`+18`|config`+18`|host|confirmed laps (`47297b`) |
|03|`+c`|4|weather option0..5|config`+a0`|config`+a0`|host|confirmed weather |
|03|`+18`|4|human count0..8|`463f95` copies`52c8b0`|`463faf`|host|active humans,confirmed |
|03|`+1c`|32|8 unsigned DPIDs|`52c8b4`|`463faf/463bd3`|host|shared order,confirmed |
|03|`+3c`|8|8 actor indices|`52c8d4`, `463c52`|`463f51`|host|shared random grid permutation,confirmed |

The type01 original roster layout stores sensitivity at`52e6d4` and CD eligibility
at`52e6dc`; they are distinct. Names come from DirectPlay enumeration, not the
eight bytes holding car and gearbox. Native names retain the existing frontend's
nine printable ASCII character limit.

Original lobby state is membership→selection→active commit→host Start. Host
Start requires an active CD-eligible player (`467278`), disables joining, sends
settings/order and AI option, then enters scene state7. A client excluded from the
shared order is removed with “race has started without you” (`466c33/46731c`).
Loading heartbeats mark each active roster entry loaded; `43a820` waits for every
active human. Host type10 supplies the shared RNG seed (`43b620→49ff50`). Native
Ready is an explicit replacement for committed selections/CD eligibility and is
revision-bound. Native Start supplies the shared seed and grid before the
all-human loading barrier. Host-only choices apply to all players. Leave/rejoin works in lobby;
mid-race join/reconnect and host migration are excluded from the baseline.

## Native wire version3

Integers use explicit little endian encoding, no C++ object memory. All lobby
traffic uses reliable ordered `Channel::control`. Total packet limit1024 bytes,
including the24-byte envelope. Transport reliable queue limit65536 bytes. Poll
work/queue limits belong to the adapter, not the wire codec.
Each typed message declares its fixed wire ID; neither IDs nor lobby/race
routing depend on the C++ variant order. Queued latest states remain typed until
the existing send cadence permits encoding; publish-time validation remains.
The fixed eight-byte controls and24-byte orientation packers allocate no heap storage.

| Offset | Width | Field | Rule |
| --- | ---: | --- | --- |
|0|4|magic|bytes`54 4f 43 41` (`TOCA`) |
|4|2|version|3; a mismatch yields typed version error |
|6|1|kind|1..20 as below |
|7|1|flags|0; other bits invalid |
|8|4|generation|0 for unbound Hello; nonzero current session generation thereafter |
|12|4|origin|0 for unbound Hello; stable player ID thereafter |
|16|4|sequence|sender counter; session validates freshness per connection/generation |
|20|2|payload bytes|must exactly match received length−24 |
|22|2|reserved|0 |

| Kind | Payload fields in order | Range / ownership |
| --- | --- | --- |
|1 Hello|content CRC32:u32,name length:u8,name bytes,car:u8,manual:u8,sensitivity:u8|name1..9 printable ASCII;car0..9;bool0/1;sensitivity1..3;unbound client only |
|2 Welcome|assigned PlayerId:u32,host PlayerId:u32,roster revision:u32,settings revision:u32|nonzero IDs;host→joining client |
|3 Reject|reason:u8|1 protocol,2 content,3 full,4 started,5 invalid message,6 host left,7 timeout,8 race started without you,9 loading cancelled,10 load failed,11 loading timeout |
|4 Roster|revision:u32,count:u8, repeated(player:u32,name length:u8,name bytes,car:u8,manual:u8,ready:u8,sensitivity:u8)|1..8 distinct nonzero IDs;host only;order defines shared human ordering |
|5 Settings|revision:u32,track:u8,weather:u8,laps:u8,AI:u8,Car Boost:u8|track0..9,weather0..5,laps1/2/3/5/10/15/20;host only |
|6 SelectCar|car:u8,manual:u8|client selects own car;host republishes roster;duplicate models legal |
|7 Ready|settings revision:u32,roster revision:u32,ready:u8|own waiting-room commitment;settings from admission through current,roster from own-choice floor through current |
|8 Start|roster revision:u32,settings revision:u32,seed:u32,count:u8,grid:count bytes|host only;grid is rank→actor permutation for1..8 actors;fresh generation |
|9 Leave|empty|member requests own removal;host departure ends session |
|10 Loaded|empty|active human confirms scene loaded;reliable control |
|11 Go|empty|host releases all-loaded barrier into grid staging;reliable control |
|12 ActorInput|actor:u8,packed controls:8 bytes|human owner;input channel;changed words only |
|13 ActorState|actor:u8,original packed body:76 bytes|human owner or host AI/takeover;latest-state channel |
|14 Gear|actor:u8,gear:u8,reverse:u8|active human owner;gear0..7;reliable control |
|15 Finish|actor:u8,time centiseconds:u32,ordinal:u32|owner proposal ordinal0;host relay ordinal1..8;time≤429496729;reliable control |
|16 Pause|paused:u8|active human;aggregate freezes shared simulation;reliable control |
|17 ActorStatus|actor:u8,active human:u8|host announces removal/takeover;reliable control |
|18 DamageVisual|actor:u8,17 variant bytes|active human owner;each variant0..3;reliable control |
|19 ReturnLobby|empty|host only,from results;fresh generation;reliable control |
|20 RaceStatus|loaded mask:u8,pause mask:u8|host only;bits refer to active human actor slots;reliable control |

Codec rejects invalid lengths, kinds, names, booleans, IDs and field ranges before
returning a typed packet. Session checks actual connection binding, origin,
generation, phase and revisions before mutation. No packet origin can grant host
authority. Hello is accepted once per connection; content/protocol mismatch gets
a concrete Reject and disconnect. A changed car withdraws that driver's
commitment and advances only that player's choice floor. Other choices,
membership and host settings preserve waiting drivers. The host accepts concurrent
Ready messages within each player's admission/settings and own-choice/roster
bounds, using wrap-safe comparisons; future and pre-admission/choice revisions
are rejected. Host Start includes committed humans only, sends the trimmed roster
before Start, and rejects other selectors. Race generations must change
on load/restart, with per-actor freshness reset; old packets cannot enter a new race.
Latest-state packets use ENet unsequenced datagrams and application sequence
checks separately for each actor. Input freshness is independent from state
freshness. Control events are reliable and ordered. The adapter checks the
complete envelope against both1024 bytes and the negotiated datagram budget;
state must not silently become reliable through ENet fragmentation fallback.
Reliable ownership changes install authority before that owner's states can
apply; packets crossing channels before the change are ignored.

RaceStatus publishes each human's loading and pause state after Loaded/Pause/leave,
before Go or a changed aggregate Pause. Loaded bits are monotonic until departure;
AI/inactive bits and client-authored status are rejected. Aggregate Pause still
controls physics; owner status also updates while another human keeps it paused.

Loading uses the original all-active-human barrier (`43a820`, heartbeat `43b620`).
The original 12s deadline (`43b670`) measures liveness, not total loading. Native
sessions additionally bound the whole loading phase to120s of monotonic time,
including a responsive peer that never sends Loaded. Escape or local resource
failure unwinds loading and restores frontend resources/CD02. A host abort sends
a typed Reject to all peers; a client abort leaves its own session and lets the
remaining host continue using the existing inactive-human policy. Cancellation and
resource failure also work immediately after Go, while the local loader finishes
its grid handoff; the timeout remains loading-only. The application
services input/transport during synchronous reads and displays waiting names.
Pause ownership follows the source per-player flag scan (`43cd40`, `463f07`).

Host setup lists active non-loopback local IPv4 addresses with the bound port.
These are interface candidates, not a reachability test. Join starts empty and
asks for the host's IP:port; discovery/NAT services remain outside this package.
Native connection/loading messages support the five existing menu languages.

Content CRC32 covers the versioned native data sources and 22 immutable canonical paths:
`data/config.bfp`, `data/allcars.bfp` and KEEP/FREE archives for prefixes
`data/donl`, `data/silv`, `data/thru`, `data/bran`, `data/oult`, `data/dons`,
`data/crof`, `data/knoc`, `data/snet`, `data/land`. Paths are sorted as ASCII.
For each, append canonical path bytes, zero terminator, file size:u32LE and
file CRC32:u32LE. A `native/builtin-data` record precedes these sorted file
records. It fingerprints the named native C++ data sources listed in
`cmake/NativeData.cmake`, with project-relative paths and normalized LF newlines
so Linux/Windows checkouts agree. These sources include translations and typed
car/track/HUD/audio/qualifying/record data; the original EXE is not read.
Source edits conservatively change this identity, including formatting changes.
The final fingerprint is IEEE/zlib CRC32 of
that concatenation (`src/network/content.cpp`). Protocol version is checked separately in the
envelope. This excludes optional Mega/UI overrides, music, resolution, audio
preferences and local presentation settings. CRC32 detects differing data;
it is not authentication.

## N01b: race fields, authority and timing

| State/event | Deciding peer/actor | Permitted senders | Application boundary | Retry/expiry | Pause/disconnect | Source / status |
| --- | --- | --- | --- | --- | --- | --- |
|Human controls|that human|owner DPID|control update before physics|changed words only|pause flag separate|`43b2d0/43c3f0`,confirmed |
|Human car correction|that human|owner DPID|`43b7d0` body/control import|adaptive interval|host takes over inactive human|`43c9e0/43ca00`,confirmed |
|AI correction|host|host|same import by actor index|adaptive interval|continues as AI|`43ca00`,confirmed |
|Disconnected actor|host|host remove event|clear active flag,`402be0`,`463f1a(0)`|remove message repeated by sender lifecycle, not guaranteed transport|body becomes AI|`43bbf0`,confirmed |
|Shared seed|host|host loading heartbeat|`49ff50` RNG initialization|heartbeat repeats|fresh`43a5c0(1)` resets seed;`43a5c0(0)` preserves it|`43a700` obtains time seed,`43b620` applies;confirmed |
|Props/part states|owning human|owner DPID|`43bb60→480c90`,17 visual damage variants|sent while changed flag set|reconstruction clears actor variants|`419ad0`→`43b4a0` only when actor=`463f30(0)`;confirmed |
|Collision/damage|each peer locally simulates;human owner publishes correction|owner state;host AI state|`421980→428820→428aa0→428b50`;both actors receive impulses/moments|state correction cadence|pause suppresses global advance;inactive human becomes AI|no host arbiter in these paths;`419ad0` publishes only local-owned visual variants;confirmed |
|Lap/finish time|human owner;host for inactive human|owner source-generated type1c|`43be20`,`463e9d`,`421390`|finish time resent in results every`4000` source timer units|pause shifts time bases|local index0 publishes own result;remote active humans never overwritten;AI excluded from human finish barrier;confirmed |
|Pause|each human;aggregate pauses simulation|each active DPID|`43bd70/43cd40`|explicit bool notification|deadline+50000ms;unpause resets receiving deadline to now+12000|`43cba0`,confirmed |

The 84-byte packet header uses tag`+0`, target DPID/actor`+4`, and a76-byte
body at`+8`. Body offsets: XYZ`+00/+04/+08` signed Q16; linear/other body
vectors`+0c..+20` copied as six32-bit words; presence/sign mask`+24`; nine
signed orientation components represented by16-bit magnitude−1 values at
`+28..+38` with presence bits0..8 and sign bits9..17; two signed6-bit fields in
bits16..21/22..27 of word`+38`; low20 bits of`+3c` copy engine speed/control
field, bit20 enables16-bit extra field in word`+40` bits12..27; word`+40`
bits0..11 hold heading; packed controls at`+44/+48`. `43c5a0/43c6e0` clamp
the nine orientation components in-place to[−65536,+65536] before packing.
Six body-vector words copy actor offsets`+c8/+cc/+d0` (linear velocity) and
`+e4/+e8/+ec` (angular velocity), signed Q16, from the original actor base
`4cd760`; motion code uses these as velocity and rotational rates. Orientation
components copy actor basis`+150..+170`. Steering angle word copies dynamics`+274`
low12; the two signed6-bit values copy dynamics`+27a/+278` byte fields,
which are the native Motion `car_visual_angles` pair (pitch/roll presentation)
confirmed by source snapshot import.
The engine low20 bits copy dynamics`+124`; optional16 bits copy`+11c`.
The final packed controls import runs for retained human slots only, including
inactive humans; AI slots receive the body/engine fields while retaining their
locally generated AI axes (`43b7d0`'s final actor<humanCount condition).

`43b7d0` blends all XYZ as `(received + 2*old)/3`, integer division toward zero,
only if both absolute X and Z differences are≤`40000`; otherwise all XYZ snap.
Y difference never selects snap. Native arithmetic must widen intermediates to
avoid C++ signed overflow while preserving values in supported original ranges.

`43c4a0` sends controls when first word differs or second word differs under
mask`00f7ffff`. The8-byte pack stores steering magnitude−1 in word0 low16,
throttle magnitude−1 in high16, brake magnitude−1 in word1 low16. Word1
bits16/17 mean throttle/brake present,18 steering present,19 steering negative,
21 selects right versus left steering. Bit20 is held start (`dynamics+148`), bits22..23 copy the two-bit gear press
code (`+14c`). Bits24/25/26 copy dynamics`+314/+318/+310` filter modes.
Bits27/28 copy gameplay`+8/+30` booleans (AI/local state). The original input
consumer`43b1e0` reconstructs axes, held start and filter modes; it does not
consume every packed field. Native adapters must not infer extra effects from
those unused fields. Absent-axis payload bits are normalized to zero in the
authored helper; original buffers can retain irrelevant old magnitude bits.

Ordinary horn input is local-only in Linkup: `22f60` returns through the remote
control importer for every nonlocal roster index before reaching input action8
at`4234fc`; the sole horn-start caller there invokes`425880` locally. No ordinary
horn message appears in the application inventory. For car type8, that same
action instead calls`406a40(local,1)`: it creates a projectile from the car's
position/basis/velocity, plays effect type`1a` and broadcasts original tag`19`.
Receiver`43bd50` calls`406a40(sender,0)` to avoid rebroadcast; `406cd0` advances
gravity and terrain impact into`405a50`'s explosion effect. This is not a generic
car recovery/reset request. Tank projectiles remain a separate existing gameplay
gap; the ordinary horn and physical rollover paths need no invented wire event.

State transmission occurs after `421aa0` physics/contact work only once all
active humans are loaded, excluding terminal phase10. `43cee0` changes a counter
each such tick, but `43cf20` actually gates sends using elapsed milliseconds:
strictly greater than interval1ms for level0, `(level+1)*33ms` for1..31,
3000ms for32. Human-state send updates the per-peer last-send timestamp; the
host's additional AI sends use that same gate. Therefore the table values are
minimum elapsed gates, not a fixed independent snapshot timer.

Probe response matches a16-bit ID; four RTT samples average by unsigned sum/4.
Mismatch increments failure and raises interval level twice. Normal thresholds
raise above155ms and lower below90ms (alternative mode210/170ms), subject to
levels0..32 and lower-bound level6 (`43bc70`). Waiting/loading heartbeat gate501ms (`43b4c0`,caller`421f40`),
probe gate1000ms (`43bfb0`), receive timeout12000ms (`43b670`). Original race
state/controls have no per-actor sequence field. Native latest-state sequencing
must discard old values separately for each actor, not a global receive counter;
discrete events require reliable delivery and explicit deduplication identities.

## Synthetic codec vectors

| Input | Expected bytes/decoded value | Provenance |
| --- | --- | --- |
|Hello(gen0,origin0,seq`12345678`,CRC`8ce56d41`,name AB,car7,manual1)|`54 4f 43 41 03 00 01 00 00 00 00 00 00 00 00 00 78 56 34 12 0a 00 00 00 41 6d e5 8c 02 41 42 07 01 02`|native authored envelope vector in`test_protocol.cpp`;CRC is canonical executable provenance |
|Every lobby kind,IDs`abcdef01`,gen`fedcba98`,sequence`ffffffff`|exact typed round-trip;all truncations rejected|native synthetic boundaries |
|Eight members with9-character names|fits1024 bytes,IDs preserved|native capacity boundary |
|Boolean2,unknown kind,version1,reserved bits,trailing byte|typed rejection before session mutation|native malformed field vectors |
|Controls(right−65536,throttle65536,brake65536)|`ff ff ff ff ff ff 2f 00`; exact signed reconstruction|`43b2d0/43b1e0` synthetic boundary |
|Orientation(0,1,−1,65536,−65536,65537,−65537,32768,−32768)|mask`0002a9fe`;16-bit magnitudes0,0,0,ffff,ffff,ffff,ffff,7fff,7fff;clamp to±65536|`43c6e0/43b9a0`, synthetic boundary |
|OldXYZ0;new(40000,−7,40000) hex|87381,−2,87381 decimal;newX40001 instead snaps allXYZ|`43b7d0`, inclusive threshold and signed division |
|Only control sign bit19 changes|no changed-input send;only held-start bit20 changes causes send|`43c4a0` exact mask`00f7ffff` |

## Race ordering and explicit native adaptations

Collision response is distributed, not a host collision event: every process
runs local contact geometry and impulses for visible physical bodies, with
owner state correcting disagreement. Native host routing must retain this
ownership; central forwarding alone does not authorize host replacement of a
human's state. Detached particles/audio are local consumers of those contacts.
The17 visual damage bytes are actor-owned; receiver`43bb60` changes the two
mesh variant trees and does not grant remote mechanical damage authority.

`421390` computes the local human's finish time, and host may compute an inactive
human's time. Active remote humans keep the received value. `43be20` subtype2
writes time to both roster`+24` and actor gameplay`+120`, then applies the common
finish path. `43ccb0` waits for all active humans and appends inactive actors
before terminal state10. Offline16-car extrapolation/points at`4220c0` is not
proof of a network-wide host result arbiter. `422ac0` repairs duplicate result
entries, while Linkup menu`45ed30` displays the append-on-finish table`4c9140`.
`4220c0` can exchange adjusted time values along that order; it does not sort
actor IDs by race time. Native Finish preserves owner-authored centiseconds;
the host reliably sequences and relays them with an ordinal so all clients
append in one agreed arrival order. This is a delivery/order adaptation, not
host authority over human physics or lap times.

Result presentation has a separate filter: `45ed30` state13 scans all physical
classification entries, but `45f868..45f885` skips actor`ff` and any identity for
which`426e80` returns0. In Linkup that excludes AI identities`-1/ff` and inactive
humans; active humans use`463ec6`. `45f89a..45f8a5` increments the displayed rank
only for an included row. The first included human supplies the total-time/gap
baseline. Native `race/linkup_results.hpp` applies this filter without changing
the underlying append order or owner-authored times. AI zero/unavailable times
are never displayed or replaced with invented extrapolations.

Known original seed lifecycle: scene initializer`447950` invokes`43a5c0(1)`;
that clears saved finish times, initializes per-peer clocks and host calls
`43a700` to obtain/apply the new seed. `43b620` applies each host heartbeat's
seed through`49ff50`. The sole`43a5c0(0)` caller is`449f50`, grid phase9;
staging preserves the seed within the already loaded scene.

Native Linkup follows the original frontend route. Native Host/Join and numeric
IPv4/port entry replace the DirectPlay provider and discovered-session browser.
Host: session name (`476b38/476bfb`) → race options (`469bb7/469c7b`) → common
illustrated garage (`477586`) → track preview (`46cb8a`) → waiting room
(`4671b0/46731c`). Client: player name → garage → waiting room. The host session
name also names its local player. The five host options are laps, weather,
Car Boost, computer cars and Continue. Name, garage, gearbox and track screens
share the ordinary frontend implementations and menu audio.

Entering the waiting room commits selection (`466b70(1)`); Back withdraws it,
returning the host to Track or a client to Garage. Roster and host Start share
one screen. Each driver's changed car withdraws only that driver's commitment;
another join, departure or host settings change does not strand already waiting
drivers. The host starts the committed subset, with “Race started without you”
for those still choosing (`467278/466c33`). Native reliable delivery sends the
filtered roster before the new-generation Start; only its actors enter loading.
Ready revision validation retains each player's own car-choice floor.

Result confirmation closes the local session (`44a370→4638bb→49414f`); a new
race uses Host/Join again. Native host-only ReturnLobby (diagnostic, not the
ordinary result-menu route) keeps connections after results, advances the
generation, removes inactive humans and clears readiness; the next Start advances
the generation again. This is an explicit lifecycle adaptation; client Leave and
host loss still close the corresponding session. Host departure ends clients'
sessions; automatic host migration remains outside this native baseline.

Original Linkup Car Boost is enabled: row `4b7574` calls `469386`, whose mode3
branch toggles config+2c. `415090→428440` applies distance-based catch-up before
human body updates after grid release. It adjusts maximum throttle and tire
offsets, independently of the mechanical boost cheat and simulation clock.
The original start sender/receiver `46767b/466c33` does not copy config+2c;
sharing the host's option in native LobbySettings is an explicit consistency
adaptation, not a recovered original packet field. Source default is On
(`46a8bb`, config+2c). Native `simulation/car_boost.*` preserves the signed
Q16 XYZ-square distance, `0x1a` factor and source far-distance sentinel, `0x1999`
throttle and `0x1eb8` tire targets, `0x28f` per-tick ramps and front longitudinal
`+0xccc` exception. Race rank1 selects the leader, independently of spatial
integration order; only original human slots receive the prepass, including
inactive humans now driven by AI. It runs after grid release and its tire writes
take precedence over launch adjustment as in `415090→428440`.


Further bounded caller evidence: `418cd0` emits subtype1 only when the owner's
gear change commits (`dynamics+30c`), carrying gear byte`+330` at message`+4`
and reverse-pedal boolean`+304` at message`+8`. `43be20` installs those at
remote dynamics`+330/+304`; these are discrete owner events, not lap flags.

`43a5c0` forces configNoQual1 for Linkup. Original allowed name
codes in mode3 (`477409`) are only indices0/1/10/15/18: Micro, no-dashboard,
large hands, bonus-car and track-unlock overrides. Mechanical/weather/time/AI
modifiers are unavailable. Native lobby uses existing local unlock availability
and excludes gameplay modifiers from its shared settings.

Return/pause evidence: `441680` sends pause1 and leaves receive pumping in
`441720`; `4403e0` sends pause0 instead of offline clock adjustment. Peer
pause aggregation (`43cd40`) freezes global simulation while any active human
is paused; last unpause shifts race/lap source clocks (`43cd90`). Pause does
not close the network endpoint or remove a roster entry. Menu action10 sends
pause0 then requests race exit (`441bxx`); source does not make every user's
Escape a global race restart. Fresh lobby Start remains host-only.
Native simulation and lap clocks freeze with this aggregate pause. Transport
service and ENet keepalives continue independently, so native liveness keeps
its 12-second hard-loss deadline during pause instead of copying the original
50000ms application-receive grace extension. This is a transport adaptation;
pausing a healthy peer does not expire it.

Human grid ranks come from`463c52`: consume one unused CRT draw, then for each
human actor draw`rand()%humanCount` repeatedly until an unused rank is found.
The original stores actor→rank; native Start carries its inverse rank→actor.
AI ranks retain initialized trailing actor order (`460070/460170`). The fleet
reuses existing physics/contact arithmetic, performs AI decisions in ascending
actor order using the current race-order traffic list, integrates distant actors
ascending and full bodies descending, then applies contacts and ground updates.
Per-tick traffic, full-body dispatch and car/prop-contact inputs use bounded
eight-slot local buffers. Consumers receive only populated entries, preserving
rank/actor order without rebuilding these five heap-backed worklists each tick.
Human actors retain their source player identity after finish/disconnect even
when controls hand off to AI (`415090`), so the body selector and rollover path
must not treat that change as deletion of the human slot.
`401260` contains an optional randomized reaction-delay branch, but its sole
caller passes0 and clears all reaction bytes. Native Linkup release therefore
adds no randomized reaction delay; AI grid throttle still uses the recovered
`404ea0` RNG branch.

One setup discrepancy is confirmed in original assembly: visual model setup
`443280` uses the AI actor ordinal, while physical setup`44db44..44db7b` searches
only human roster entries, then unconditionally reads car selection at
`463e51(humanCount)` after an unsuccessful AI lookup. That inactive slot is
zero in a fresh process but can retain a prior larger session's car. No write
initializes an AI selection in`4639b6`. Native Linkup uses the confirmed fresh
process value0 for AI physical parameters and actor ordinal for its visual
model. This deterministic initialization intentionally excludes history from
an unused roster slot; it does not infer physical parameters from the model.

### DirectPlay library recheck

Reviewed against the DirectPlay3A operations above:
[directplay-lite](https://github.com/solemnwarning/directplay-lite) (partial
DirectPlay8, Windows/Visual Studio build); Wine's
[dplayx](https://github.com/wine-mirror/wine/blob/master/dlls/dplayx/Makefile.in)
(imports `dxguid`, `uuid`, `winmm`, `ole32`, `user32`, `advapi32`; not a
standalone transport); [FreeDirect](https://github.com/openeggbert/free-direct/blob/develop/docs/directplay-limitations.md)
(no player names/create-destroy notifications, incomplete GUID join checks and
nonhost unicast, no Microsoft wire compatibility, stops at DirectPlay/DirectPlay2A,
its transport is ENet). None simplifies the required subset; the baseline keeps
ENet and reproduces the recovered payload semantics, ownership and timing.

### N02 transport measurements

Full native ENet probe (111.96 s): seeded UDP proxy below ENet, eight actor
streams at 25 Hz (synthetic load; real publishing uses the recovered adaptive
cadence), one reliable control operation per second, same-clock acknowledgements.

| Profile | Received/sent states | State age p95/max | Maximum fresh-state gap | Reliable application ACK maximum |
| --- | --- | --- | --- | --- |
| 50 s loopback | 10000/10000 | 2/4 ms | 43 ms | 3 ms |
| 50 s, 100 ms RTT, 2% loss | 9816/10000 | 52/54 ms | 132 ms | 603 ms |
| 10 s, added jitter/duplication/reordering | 1936/2000 | 69/72 ms | 108 ms | 608 ms |

The stress receiver rejected 16 stale states. Queue bounds held (64 KiB reliable
per peer, eight latest actor slots); three endpoints exercised client→host→client
routing and hard-loss timeout. Native probe targets (not TOCA constants): control
applied within 2 s, p95 delivered-state age below 250 ms at 100 ms RTT, no active
stream without fresh delivery for 1 s.

Dependency: pinned MIT ENet archive, 83822 bytes, CRC32 `00c20465`, upstream commit
`5a9c537fd464b3c6d3c55e1d3bd47588faf71b42`; no other networking library. The input
channel uses the same unsequenced delivery as state, with independent per-actor
freshness in the router.

### Local eight-peer capacity

Eight native games, 600.16 s, seven private UDP proxies: 100 ms RTT per
client↔host link, 2% loss, ±20 ms one-way jitter, 1% duplication, 1% reordering
(client↔client crosses two links). Two complete races kept identical owner-authored
times/append order on all peers; contact, spatial audio, ninth-player rejection,
hard client loss (AI takeover), hard host loss (menu cleanup) and a fresh
eight-player session passed. Maxima: reliable queue 2869 bytes, latest-state
slots 49 across seven host links, proxy queue 13 datagrams, service gap 198 ms,
per-process RSS growth 32936 KiB, no proxy overflow. Sampled RTT 113–156 ms and
cadence gates 66–165 ms reflect ENet RTT estimates and the adaptive scheduling,
not a cross-machine end-to-end state age. These measurements cover local
controlled impairment.
