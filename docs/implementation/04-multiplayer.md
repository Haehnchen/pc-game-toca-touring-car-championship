# Multiplayer recovery and implementation

Design: **ENet for LAN and direct IPv4, with the recovered TOCA game protocol
translated into explicit native messages.** One local player per process, up to
eight human players including the host, one transport implementation. Simplicity
has priority: internet convenience must not add infrastructure. Linux and
Windows have equal interface/dependency priority; macOS uses the same backend.
[Shared rules](../porting-plan.md#delegation-and-acceptance-rules).

Original evidence, message inventory, field tables, wire format and measurements:
[network protocol](../network-protocol.md). Local process checks cover the
racing/session baseline and eight-peer capacity.

## Reuse and scope decisions

| Part | Treatment |
| --- | --- |
| DirectPlay3A, COM objects, provider selection, IPX/modem/serial | Replaced by ENet and native host/join. No Wine or DirectPlay runtime dependency. |
| Lobby choices, readiness, start/race/results, player/car mapping | Recovered game rules ported as project code. |
| Packed controls, car state, correction, race events | Source-backed field meanings and arithmetic through bounded fixed-width codecs; never send C++ memory layouts. |
| Native physics, route/lap records, renderer, HUD, spatial audio | Existing actor contracts extended for remote humans; original numeric/update order preserved. |
| Peer identity, phase validation, queues, session lifecycle | Small native implementation around the transport. |
| Original host migration | Effects recovered; baseline ends the session cleanly when the host leaves or is lost. Automatic migration is a deliberate deferred fidelity item. |
| Unchanged original clients | Out of scope; matching payloads does not make ENet a DirectPlay provider. |

Initial use: private LAN or an explicitly reachable IPv4 endpoint (a direct
internet connection can need firewall/router configuration). Outside this
scope: automatic NAT traversal, relay hosting, accounts, matchmaking, public
server lists, IPv6, combined split-screen/network play, mid-race joining,
reconnect, spectators, dedicated servers, rollback/prediction redesign. Joining
is allowed in the lobby; a new connection cannot claim an active race slot. LAN
discovery is an optional later convenience, not a condition for direct join.
Tank projectile gameplay/replication (original tag `19`) is unfinished bonus-car work.

## Transport library decision

| Candidate | Supplies | Decision |
| --- | --- | --- |
| **ENet 1.3.18**, MIT | Reliable/unreliable UDP, independently sequenced channels, retransmission, connection monitoring, bandwidth controls. | **Selected.** Small C library, no crypto/Protobuf. IPv4 only; no built-in encryption, authentication, discovery or lobby. Pinned archive CRC32 `00c20465`, commit `5a9c537fd464b3c6d3c55e1d3bd47588faf71b42`. |
| SDL_net 3.2.0, zlib | Async addressing, IPv4/IPv6, TCP streams, UDP datagrams. | Compatible with SDL3, but needs own framing, two-connection association, ordering rules and a reliable-UDP layer. More application transport work. |
| GameNetworkingSockets 1.6.1, BSD-3-Clause | Reliable/unreliable UDP, lanes, encryption, IPv6, diagnostics, optional P2P/ICE. | Strong candidate if internet features become requirements; crypto/Protobuf build dependencies; P2P still needs signaling/STUN/relay (no Steam services in the open-source library). Excess scope. |

Sources: ENet [ChangeLog](https://github.com/lsalzman/enet/blob/v1.3.18/ChangeLog),
[channel design](https://github.com/lsalzman/enet/blob/v1.3.18/docs/design.dox),
[omitted features](https://github.com/lsalzman/enet/blob/v1.3.18/docs/mainpage.dox),
[IPv4 API](https://github.com/lsalzman/enet/blob/v1.3.18/include/enet/enet.h);
SDL_net [release](https://github.com/libsdl-org/SDL_net/releases/tag/release-3.2.0),
[API](https://wiki.libsdl.org/SDL3_net/CategorySDLNet),
[CMake](https://github.com/libsdl-org/SDL_net/blob/release-3.2.0/CMakeLists.txt);
GameNetworkingSockets [release](https://github.com/ValveSoftware/GameNetworkingSockets/releases/tag/v1.6.1),
[features](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.1/README.md),
[build](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.1/BUILDING.md),
[P2P](https://github.com/ValveSoftware/GameNetworkingSockets/blob/v1.6.1/README_P2P.md).
These are transport libraries only; none replaces recovery of TOCA's rules. No
second backend or network-plugin framework. Reopen selection only for a
demonstrated transport limitation or a new requirement (IPv6, encrypted
internet, P2P).

## Original 1.1a facts

Evidence source: `references/decompiled/tourcars.c` of canonical
`references/toca-1.1a/`, PE imports, two GUID reads and assembly; DirectPlay
meanings matched against Wine's [interfaces/constants](https://github.com/wine-mirror/wine/blob/master/include/dplay.h)
and [export ordinals](https://github.com/wine-mirror/wine/blob/master/dlls/dplayx/dplayx.spec)
(interface references only). Wrappers, session descriptor (`493a30`: size
`0x50`, flags `0x44`, 8 players), message inventory, race packet sizes
(`43c270` send switch, `43b510` dispatcher; no tag `0x13`), cadence, heartbeat
and pause handlers: [network protocol](../network-protocol.md).

## Native integration contracts

| Existing code | Connection |
| --- | --- |
| `src/frontend/controller.*`, `src/core/game_mode.hpp` | Linkup mode, lobby screens, host/join/roster actions. `RaceSetup::players` means one or two **local** players and is not a network player count. |
| `src/app/application.cpp` (`SceneSession`), `src/app/options.*` | Session lifetime, menu/loading/race transitions, diagnostic host/join options (not saved preferences). One local input/camera/listener bound to its actor. |
| `src/simulation/rivals.*`, `motion.*` | Explicit local-human / remote-human / AI ownership in the fleet; remote cars never run local AI. Typed remote-state application API; `restore_source_snapshot()` stays a diagnostic original-memory replay interface. |
| `src/race/race_progress.*`, `src/simulation/session.*` | Route/lap/finish records through recovered authority rules; smoothed remote positions never decide winners. |
| `src/simulation/time.*` | Native 25 Hz/40 ms schedule. Transport time is monotonic wall time and stays live during pause, focus loss, loading, results. |
| `src/audio/rivals.*` | Remote engine/position state into spatial audio, one local listener; discrete sounds once per accepted event. |
| `cmake/Dependencies.cmake`, lock, `cmake/NetworkDependencies.cmake`, `Testing.cmake` | One locked dependency, `toca-network`; CRC32 download/cache/test-target machinery. |

Module map: `src/network/types.hpp` (explicit player/actor/generation IDs; no
ENet/SDL handles in simulation contracts); `transport.*` (RAII endpoints/packets,
nonblocking bounded polling, channels, disconnect/statistics; events own their
payload until consumed); `protocol.*` (fixed-width byte codecs, legal
lengths/ranges, typed messages, version/generation; the session validates
sender/phase/ownership); `session.*` (membership, player↔connection mapping,
immutable lobby view, ready/settings revisions, phases); `race_state.*` (race
routing, per-actor freshness, discrete event identities);
`src/frontend/network_lobby.*` (lobby presentation); `src/network/content.cpp`
(content CRC32). Tests mirror modules under `tests/network/`; the impairment
fixture lives there; real-application flows in `tests/integration/test_network_*.py`.

Topology: **host with connected clients**. A client sends to the host, which
routes allowed messages to destination players; host-local messages use the
same validation/dispatch. The host assigns session player IDs and binds the
originating connection to that identity. Relaying does not make the host the
physics owner of every car (collision/result authority:
[network protocol](../network-protocol.md#race-ordering-and-explicit-native-adaptations)).
Account for the extra relay hop in tests.

Transport boundary: connect/listen, send-to-player/broadcast, bounded
nonblocking receive, disconnect, statistics. Library handles stay in
`transport.*`. No transport callback mutates physics during rendering or
mid-contact; decoded events apply at documented simulation/phase boundaries.

Messages carry protocol version, session/race generation, kind, origin
identity, sequence and explicit payload length; original payload sizes are not
native envelope sizes. Length, ranges, active sender, ownership and phase are
validated before use; sequence comparison is wrap-safe; old generations are
rejected. CRC32 over an ordered set of simulation-content inputs plus an
explicit gameplay/protocol version excludes local Mega artwork, resolution and
audio settings; it detects mismatched content, not identity or tampering.

Reliable control traffic and fresh state use independent ENet channels; not
every original unreliable event becomes a guaranteed command without checking
its retry/expiry behavior. Transport delivery alone does not mean a peer has
loaded the track or applied a start/results transition. Freshness is tracked per
actor/generation; duplicate events must not repeat effects or sounds.

Focus/pause deactivates `SceneSession`, and loading/catch-up can delay the main
loop: service the network outside that gate, between catch-up batches and during
loading; cap idle waits. Never run simulation from a transport thread. The
blocking/synthetic `--step-input` clock is not evidence of real network timing.
Shared RNG/roughness and local presentation draws need an ownership audit;
exchanging a seed alone does not prove equal simulations.

## N01 — Recover protocol and ownership

Recovery tables (message inventory, fields, ownership/lifecycle, native wire
contract, codec vectors) live in [network protocol](../network-protocol.md).
Unknown values are marked `unknown` with the next caller/branch to inspect;
original binary/memory traces stay private (only path/provenance/CRC32 recorded).

### N01a — Identity and phase contract

Done: DirectPlay wrapper/system-event typing, every send/receive case;
local-index-0 vs shared player/car ordering, active/AI slots, grid setup,
roster/car rules, readiness, loading/start barrier, total cars vs the
eight-human limit; join/leave/restart permissions, allowed Extras/modifiers,
original migration effects vs native host-loss difference; native envelope,
lobby codecs, version/content checks, sender/phase validation.

### N01b — Fields, authority and time

Done: field offsets/widths/masks/legal senders (input-on-change, both 84-byte
forms, RNG seed); owners for physics, body modes, contacts/damage/props,
disconnected-actor takeover, lap events, results; adaptive send cadence,
late-message behavior, smoothing/snap arithmetic, heartbeat and pause-adjusted
timeouts; seed application and shared-state lifetimes; race codec/channel
mapping with deliberate delivery changes. Gameplay ambiguity: bounded assembly
first, paired original observation (isolated Wine state, unchanged game root)
only if needed. Unresolved collision/result authority would block N04 race
acceptance, not transport or lobby.

## N02 — ENet integration experiment and dependency gate

Done: pinned ENet (HTTPS, `toca_dependency_source()`, CRC32 from
`tools/checksum.py`); `cmake/NetworkDependencies.cmake` (C language support,
Windows socket libraries, upstream flags separate from project warnings); RAII
transport; endpoint CPU checks and two-game diagnostic host/join; third peer
for host routing/sender binding; recovered size classes with reliable control
interleaved with fresh state; impairment injected **below ENet** by a scoped
loopback UDP proxy.

Caps: application message 1024 bytes; at most one unsent latest state per
actor; 64 KiB outstanding reliable data per peer (including data already in
ENet); bounded receive work per pump and ENet packet/waiting-data limits; a full
queue replaces stale state or returns a defined error.

Acceptance workload (CPU transport fixture): eight synthetic actor streams at
25 msg/s for 50 s per profile, plus one reliable control operation per second;
loss-free, then 100 ms RTT/2% seeded loss; separate 10 s stress with ±20 ms
jitter, 1% duplication, 1% reordering. Native probe targets (not recovered
constants): control applied within 2 s, p95 delivered-state age below 250 ms at
100 ms RTT, no active stream without fresh delivery for 1 s, hard peer loss exits
within configured timeout plus one service interval. Measurements:
[network protocol](../network-protocol.md#n02-transport-measurements). Diagnose
application mistakes before rejecting ENet. These measurements cover Linux
loopback and controlled impairment.

## N03 — Native sessions and lobby

### N03a — First usable Linkup menu

Host, Join by IPv4 address/port, Back through the original localized frontend
path; connection progress, full/incompatible-session errors, cancellation.
Bounded roster/ID mapping, host settings, ready states, protocol/content checks;
one process contributes one human (the two-local-player input/camera/audio
arrays never represent network peers). Entering the waiting room commits the
local driver; Back or a changed local car withdraws it. Peers beyond eight and
joins after the lobby closes are rejected. Start is enabled only for the
committed host and starts the committed subset.

### N03b — Session loss and restart boundaries

Host owns membership and session generation; client-leave behavior recovered
per phase (lobby/loading/race/results) with AI takeover or removal. Host
departure/loss ends the session with a visible reason and clean menu return on
all clients (no successor election/reconnect); old messages and remote
input/audio cleared before a new session. Reception and timeout handling stay
alive in menus/loading/unfocused states. Eight processes fit; a ninth gets a
full-session result. No abandoned sockets, stuck input or duplicate cues.

### N03c — Optional LAN discovery after direct join works

- [ ] If useful, add one bounded discovery query/reply using ENet's portable socket
  functions, a short expiry and protocol/version identifiers. No extra networking
  library, continual subnet scan or background discovery outside the Linkup page.

Direct IP remains the complete baseline if this convenience is deferred.

### N03d — Original menu flow and usable session status

Source route: `467a49/467af9` Host/Join; host options `469bb7/469c7b`, common
garage `477586`, host track selector `46cb8a`, waiting room `4671b0/46731c`.
Clients enter the garage after joining, then skip host options/track selection.
The original waiting room shows the roster directly; Back returns to the host's
track selector or the client's garage. Entering commits the driver; the host
starts that subset and excludes uncommitted selectors. Result confirmation
closes the session (`46381a`, state 2). Direct IPv4 setup remains the native
transport adaptation.

Native menu: shared illustrated garage and track selector with host/client
forward and Back routes; waiting room with names, committed car/OK, host Start,
client track caption (Enter commits, Escape withdraws only that driver); host/
local markers and shared settings summary; host's active non-loopback IPv4
address:port candidates (interface candidates, not a reachability test; Join
starts empty); original localized option/weather labels, name prompts,
lost-session and excluded-driver messages; native endpoint/transport failures
localized in the five menu languages; indicators when another player pauses or
is still loading.

## N04 — Replicated race and multiplayer outcomes

Implemented inside the common simulation/fleet; the transport selects no new
gameplay rules and is not a server-authoritative replacement for the original
physics.

### N04a — Shared load, grid and two moving cars

- Lobby identities map to explicit actor IDs; recovered Linkup fleet, choices,
  allowed modifiers and shared RNG state initialized at the original point.
- Loading acknowledgments and a race generation/start barrier; network service
  continues during synchronous resource loading; all peers commit the same
  settings and start phase.
- Loading recovery: waiting players shown, Escape accepted during resource reads
  and the barrier, menu/audio restored after resource failure, loading bounded
  to 120 s even with a connected peer (native policy; original `43b670` bounds
  liveness only; extends `43a820`/`43b670`). Host cancellation ends the session
  with a typed reason; client cancellation removes that human and the host
  proceeds.
- Typed remote controls/state/correction in `Motion` and fleet ownership;
  packed-field arithmetic and original remote update/contact order preserved;
  authoritative events go to `RaceProgress`/`Session`, not interpolated
  presentation positions.
- Models, damage/visibility, HUD, engine/contact/horn audio, one local listener;
  send at the recovered cadence independent of presentation FPS.
- Linkup Car Boost: menu row `4b7574` invokes `469386`; mode 3 consumes
  config+2c in `415090→428440`; throttle/tire-offset ramp after grid release.
  The original start packet does not transmit config+2c; native LobbySettings
  shares the host's choice (explicit consistency adaptation). Separate from
  disallowed mechanical name-code cheats. Details:
  [network protocol](../network-protocol.md#race-ordering-and-explicit-native-adaptations).

### N04b — Complete race, pause and results

- Contact/damage/prop and AI/disconnected-car ownership, authoritative lap/
  finish events and result ordering with original race rules and numeric order.
- [G01b's Linkup pause menu](03-gameplay.md#g01b--pause-menus-for-the-remaining-modes):
  pause/resume notifications, race/lap clock freeze, host/client restart/leave
  permissions; receiving continues during overlays and focus changes; the ENet
  adaptation keeps its 12 s liveness timeout during pause.
- Discrete race/effect/audio events applied once, local sound settings kept,
  stale messages after restart rejected; return through the recovered results/
  lobby route with the expected CD/menu transition.
- Both peers show the same authoritative outcomes; loss/reordering neither
  invents contacts/results nor repeats sounds. Host loss uses N03b termination;
  original automatic migration remains unimplemented and does not count as
  fidelity acceptance.

### N04c — Local eight-peer capacity

Eight peers ran ten minutes under controlled loss with bounded queues/memory and
one collision, client loss, restart and new lobby entry
([capacity results](../network-protocol.md#local-eight-peer-capacity)). Latency
and ownership limits are recorded. Automatic migration and internet services
remain outside this capacity result.

## Checks

Registered with `toca_add_test`; `make check-network` selects the focused
network set. Components: `network-protocol` (field vectors, signed/masked
boundaries, malformed lengths, sender/phase/generation rules, smoothing
threshold), `network-transport` (ENet loopback, below-transport impairment,
bounded queues, channels, disconnect), `network-session` (membership,
commitments, subset start, host loss), `network-results` (active-human result
filtering with owner-authored order/times); native flows connect, lobby,
recovery, smoke, ai, capacity. `extended`: `network-transport-probe` (50 s +
50 s + 10 s workload, 180 s timeout), `native-network-race`,
`native-network-soak` (ten-minute `--soak` of the capacity fixture, 900 s,
cost 8). Use sanitizers for changed codecs/state application; peers get private
Xvfb displays, muted audio, state/config/trace paths and dynamically allocated
ports; peer logs, metrics and failure details under `build/test-artifacts/<case>/`.
