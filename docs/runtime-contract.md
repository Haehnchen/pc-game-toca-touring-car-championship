# Native startup contracts

SDL-free value types in `src/core/contracts.hpp`: native boundaries, not binary layouts or a recovered simulation rate.

- The application owns settings and applies validated values to display/audio services. CLI overrides saved preferences; saving needs a deliberate action. Diagnostic paths, frame counts and traces never enter the preferences file.
- The host input adapter owns held state and accumulates press/release edges until it publishes one immutable `InputSnapshot`. A quick tap keeps both edges; key repeat adds no press. Focus loss releases held actions. Pointer coordinates use the inverse of the displayed 640×480 UI transform with an explicit outside flag. `any_key_pressed` keeps non-repeated keyboard presses for the title condition (incl. keys without a game action). C01 consumes keyboard menu edges with Confirm over navigation; held-key
  repeat and gameplay mappings remain G03 work.
- Name editing consumes owned UTF-8 text for the current poll plus Backspace; only original uppercase Latin letters, digits, spaces, nine-character bound; Escape beats Return. SDL text input is active only for that focused screen; focus loss clears pending text; consumed characters never replay. First accepted text replaces the prior name; Backspace removes the last character; Escape discards unconfirmed edits; an empty confirmation restores the localized player default.
- Selection commits: difficulty commits when name confirmation enters Car (previews do not commit); car confirmation commits the available car before Track; Track Back stores the preview even when locked and returns to Car, re-entry restores it; Donington confirmation commits and enters scene loading (its controls and audio stay reachable while movement is ported).
- `ClockStep`: unsigned 64-bit monotonic nanoseconds and elapsed nanoseconds, supplied explicitly by host polling and replay. Presentation caps/VSync create no simulation ticks or replay input edges. The first local control consumer uses the native scheduling adaptation below.
- The application assigns increasing event sequence numbers and timestamps; consumers process each event once, in order, by direct calls. Startup, focus, shutdown carry lifecycle state; `play_sound` identifies an explicitly loaded sample. A startup event invents no title cue. CD music in `assets/music/` joins the recovered frontend/race calls through a separate music track ([music](music.md)).

Original evidence: `0x0049a240` pumps host messages separately from active dispatch; `0x0049a357` reports activation. `0x004923e8` keeps held bits and computes newly pressed bits as `(new ^ old) & new`; `0x004922a0`/`0x004922f5` expose held and newly pressed masks separately (native release edges are adapter bookkeeping). `0x00469f10`/`0x00469f9b` pace the frontend via performance counters; their divisor does not establish a simulation timestep. See [the evidence ledger](recovery.csv).

## Frontend flow

- First screen: title and external palette of `0x004684ac` from the read-only canonical store. A key or five elapsed seconds advance to the copyright loading frame; after it is presented, the app loads frontend resources/WAVs, starts CD02, enters language selection.
- The SDL-free controller owns language, selection, Single Race options, driver name, car/track preview and committed choices, transmission, highlight phase, Quit; the view produces frames through the existing renderer. Initial menus are their own Back states; the selection route returns through its recovered parents. Car selection uses original 2D garage strips, Track original CM3D miniatures; rotation advances by -34/4096 turns per active 30 Hz tick with retained fractional elapsed time; focus loss pauses it, re-entry resets the pose. Full race/menu
  lifecycle beyond the current Donington entry/return remains F05b/C01/G01 work.
- Offline audio advances the preceding elapsed interval before new input/cues take effect. Frontend initialization establishes a fresh consumer time boundary (host trace still records consecutive poll deltas incl. load time). Music uses its own mixer track, explicit start timestamps, completed-state checks; the active frontend checks every 61/30 s independent of the presentation limit (menu timing adaptation, not a simulation schedule).

## Scene scheduling

- The scene owns a separate 25 Hz deadline schedule and timestamped `TimedActions` queue. SDL key events take effect on their first eligible tick; late arrivals go to the next unconsumed tick; gear edges are consumed once. The host waits for the earlier of presentation and simulation deadlines (a low FPS limit cannot make the input/controls loop sleep a whole frame). GPU/VSync stalls keep due ticks; focus/minimize pauses reset the deadline and clear held/pending input on resume. Native adaptation, separate from the recovered `SourceClock` arithmetic.
- Scene offline audio advances to each deadline before updating motor controls, then mixes the remaining interval to host time. Engine objects stop before sample slots are cleared/reused by the frontend. Native traces expose each simulation update and each layer's original frequency/volume values. This
  stationary control/motor contract does not establish full driving or original
  frame equivalence; see [timing](simulation-time.md) and
  [vehicle state](vehicle-motion.md#local-keyboard-and-drivetrain-consumers).
