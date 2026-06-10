# Plan: radio-basecamp implementation

**Date:** 2026-06-09
**Status:** Specs resolved — ready to execute in priority order. Awaiting Alisher's go on Epic A.
**Brief:** [`../BRIEF.md`](../BRIEF.md) (feasibility insights binding)
**Architecture:** `radio_module` (core) + `radio_ui` (QML-only) — tutorial-v3 canonical split.

> Fieldcraft: new-project setup ⇒ plan-first. This is that plan. Scope-freeze during execution;
> log follow-ups, don't fold in refactors. Each issue ships ≤~200 lines diff, audited before merge.

---

## Priorities

- **P0 — vertical slice that proves the thesis.** A host can broadcast and a *separate* listener
  can discover-and-play over LogosMessaging with no central index. Issues #1–#9.
- **P1 — makes it a usable radio.** Liveness/TTL, status polish, private topics, player controls. #10–#14.
- **P2 — hardening & ship.** Error UX, packaging/install, docs, security pass. #15–#18.

The bottleneck is **discovery correctness** (the differentiator) and **MediaMTX bundling**
(the one true unknown). Both are pulled early (#2 spike, #6).

---

## Epics → Issues

### Epic A — Scaffold & build skeleton  (P0)

**#1 — Scaffold both modules (core + QML UI), buildable.** ✅ **DONE (2026-06-10):** both
`nix build` green — `radio_module_plugin.so` (1.9 MB) + `radio_ui` (Main.qml/metadata bundled).
Two fixes needed beyond the skeleton: (a) pin `delivery_module` `/v0.1.1` + `follows` (see #2b);
(b) `initLogos(LogosAPI*)` must be **`Q_INVOKABLE`, NOT `override`** — `PluginInterface` (real
`interface.h`) declares only `name()`/`version()` pure-virtual; `initLogos` is a commented-out
TODO there, called via the meta-object system. The create-logos-module skill's `override` is wrong.
- `radio_module/`: `flake.nix` (`mkLogosModule`), `metadata.json` (`type: core`, dep `delivery_module`),
  `CMakeLists.txt` (`logos_module(...)`), `src/radio_interface.h` + `radio_plugin.{h,cpp}` (API contract stubbed).
- `radio_ui/`: `flake.nix` (`mkLogosQmlModule`), `metadata.json` (`type: ui_qml`, `view: Main.qml`, dep `radio_module`), `Main.qml`.
- Reuse: `logos-module-builder-scaffold`, `git-init-gitignore-first`, `builder-core-module-src-layout`.
- **Headless test:** `nix build` succeeds for both; `radio_module/tests/run-headless-tests.sh` loads
  the plugin under `logoscore` and calls a no-op `ping()` → asserts `{"ok":true}`.
- **Done when:** both `nix build` green; `logoscore` loads `radio_module`; `nix run .` shows the QML shell.

### Epic B — Origin: MediaMTX control  (P0)

**#2 — SPIKE: bundle + spawn MediaMTX.** ✅ **CONFIRMED (2026-06-10, hermetic — no AppImage).**
- **Provisioning:** `mediamtx` is in nixpkgs (**1.18.2**) → bundle via `metadata.json` →
  `nix.packages.runtime: ["mediamtx"]`. No vendoring/`external_libraries` needed.
- **Spawn:** `mediamtx <config.yml>` starts cleanly; RTMP/HLS/API listeners open. The module
  will `QProcess`-spawn it the same way and kill on `stopStream()`/teardown.
- **GOTCHA (binding spec for the config generator):** an **empty** MediaMTX config rejects
  arbitrary stream paths (`path '<x>' is not configured`). The generated config MUST include a
  catch-all `paths:\n  all_others:`. Minimal working config verified:
  ```yaml
  rtmpAddress: :<p1>   # OBS ingest
  hlsAddress:  :<p2>   # listener HLS out
  apiAddress:  :<p3>   # status polling (#4)
  api: yes
  hls: yes
  hlsVariant: lowLatency
  rtsp: no  srt: no  webrtc: no   # v1 audio-first; enable srt/webrtc later
  paths:
    all_others:
  ```
- **Full loop verified:** ffmpeg→RTMP push → API `/v3/paths/list` shows path **ready** (H264+AAC)
  → HLS `index.m3u8` serves **HTTP 200** (valid LL-HLS, audio+video renditions). Status polling
  (#4) reads `/v3/paths/list` `items[].ready`/`tracks`. ffplay can then GET the `.m3u8`.
- **Still to verify in #2 impl:** WHIP ingest endpoint (`:8889/<path>/whip`); MediaMTX surviving
  inside `logos_host` (QProcess parent-death handling); port-in-use handling (#15).
- **Headless test:** `tests/run-headless-tests.sh` calls `startStream` → asserts the spawned PID is
  alive and the HLS port answers HTTP 200/404 (not connection-refused); `stopStream` → PID gone.

**#3 — Ingest URL + stream-key minting.** ✅ **DONE (2026-06-10, runtime-proven).** `startStream(configJson)`
spawns MediaMTX (lands #2 impl) and returns `{ok, path, streamKey, whipUrl, rtmpUrl, srtUrl, hlsUrl}` with the
host LAN IP; `stopStream` tears it down. Random 16-hex `path` doubles as the OBS stream key in v1 (real
publish auth → #18). Ports overridable via `RADIO_*_PORT`; binary via `RADIO_MEDIAMTX_BIN`.
- **Proof:** `tests/run-direct-test.sh` (in-process, bypasses logoscore's gated returns) — ALL PASS:
  card has all fields, MediaMTX API up after start, down after stop, path unique across calls.

**#4 — MediaMTX status polling.** ✅ **DONE (2026-06-10, runtime-proven).** `getStreamStatus()` →
`{ok, state: idle|waiting|receiving|live, hlsUrl}` by querying MediaMTX `GET /v3/paths/get/<path>`.
Uses **`QTcpSocket`** (synchronous, no event-loop reentrancy — not `QNetworkAccessManager`/`QEventLoop`).
Mapping: no process→idle; 404→waiting; source+ready+tracks→live; source-only→receiving. Emits
`streamStatusChanged` on edge.
- **Proof:** direct-test ALL PASS — `waiting` with no publisher, `live` after an ffmpeg RTMP push.

### Epic C — Discovery: announce + subscribe  (P0)

**#5 — `delivery_module` init + topic plumbing.** ✅ **DONE (2026-06-10): wiring built + loads;
decode path runtime-proven. Live IPC round-trip deferred to AppImage.** `startDiscovery()` does
`getClient → createNode({mode:Core,relay:true,preset:logos.dev}) → requestObject → onEvent(messageReceived)
→ start → subscribe(directoryTopic)` (proven scorched-earth pattern). `addTopic(t)` subscribes extra
topics; `getStations()` returns the cache. `ingestAnnounce(b64)` does a SINGLE `fromBase64` decode,
validates, self-echo filters (skip own `path`), stores keyed by path with `_lastSeen`.
- Reuse applied: `delivery-module-messaging`, scorched-earth `game_plugin.cpp`, `logosapi-member-no-redeclare` (fixed).
- **Proof:** direct-test ALL PASS for `ingestAnnounce` (valid stored, malformed dropped). Module loads
  via logoscore. The live two-node send/receive round-trip needs real `delivery_module` + AppImage
  (can't test headlessly — logoscore gates returns; `logoscore-gates-method-returns`). Remaining headless
  gap is the ONLY unverified part of the origin+discovery slice.

**#6 — Announce schema + publish.** ✅ **DONE (2026-06-10): schema + gating runtime-proven; send → AppImage.**
`buildAnnouncePayload(seq)` → `{v, name, host, path, streamUrl, visibility, description, startedAt, seq}`.
`announceOnce()` gates on `streamState()` (only `live`/`receiving`), then publishes on the announce topic —
**public → directory topic; private → `/radio-basecamp/1/<path>/json`** (set in `startStream`). Delivery-node
init refactored into shared `ensureDeliveryNode()` (used by #5 + #6). The #10 heartbeat will call `announceOnce()` on a timer.
- **Proof:** direct-test ALL PASS — gated `not_live` before streaming; once live, the gate passes and the
  payload carries the full schema. Actual `delivery_module.send` needs the AppImage (same gap as #5's round-trip).

### Epic D — Stream tab UI  (P0→P1)

**#7 — Stream tab: setup card + start/stop.** ✅ **DONE (2026-06-10): UI built, QML loads + elements
instantiate.** Name field, Public/Private (`ButtonGroup`), description, Start (disabled until name set)
→ `startStream`; OBS card (WHIP/RTMP/Stream-Key + Copy via hidden-`TextEdit` clip helper, no
`Qt.openUrlExternally`) + Stop → `stopStream`. Renders from `streamCard` property.
- **Proof:** integration-test passes (QML loads, form elements instantiate). **Findings (2026-06-10):**
  (a) `logos.callModule` WORKS in the standalone app (real returns, unlike gated bare logoscore) — UI tests
  can drive the backend; (b) `mediamtx` is NOT on PATH in the standalone-app sandbox (`execve: No such file`),
  so the full Start→card flow can't run there — verified in the running app; (c) the framework's
  `expectTexts` matches by `text` property **regardless of visibility** → it proves elements EXIST, not
  visible render. So all `ui-tests.mjs` assertions = "QML loads + elements instantiate", not visual correctness.

**#8 — Live status light.** ✅ **DONE (2026-06-10).** A 1.5s `Timer` (running while streaming) polls
`getStreamStatus()` → `streamState`; a dot+label row in the OBS card maps idle/waiting→"Waiting for
OBS…" (grey), receiving→"Receiving stream…" (amber), live→"🔴 Live (announcing)" (red). Applies
`qml-timer-state-polling`.
- **Proof:** integration-test passes — status label instantiates with default-state text. Live
  transitions need a real stream (mediamtx not on PATH in the UI sandbox; backend mapping already proven in #4).

### Epic E — Listen tab + playback  (P0→P1)

**#9 — Listen tab: directory render + tap-to-play.** ✅ **DONE (2026-06-10).** Backend: `play(hlsUrl,name)`
spawns `ffplay -nodisp -autoexit` (skill `ffplay-subprocess-player`); `stop()`/`getPlayerStatus()`.
UI: Listen tab starts discovery on open, polls `getStations()` (2s `Timer`), renders a `ListView`
(name / host · uptime), tap → `play(streamUrl)`, now-playing bar + Stop, + add-topic field.
- **Proof:** direct-test ALL PASS — `play` → ffplay running, `stop` → stopped (SDL dummy audio for headless).
  integration-test passes — Listen-tab elements instantiate. Tap-to-play with live rows needs
  delivery_module announces (cross-machine demo).

### Epic F — Liveness  (P1)

**#10 — Heartbeat re-announce (15s).** ✅ **DONE (2026-06-10).** A `QTimer` (interval `RADIO_HEARTBEAT_MS`,
default 15000) started in `startStream`, stopped in `stopStream`, fires `announceOnce()`.
- **Proof:** direct-test ALL PASS — with a 150ms interval, `announceAttemptCount` grows ≥3 over a 1.2s event loop while live.

**#11 — TTL expiry (45s).** ✅ **DONE (2026-06-10).** `getStations()` lazily prunes stations whose
`_lastSeen` is older than `RADIO_TTL_MS` (default 45000 = 3 missed 15s beats); emits `stationsChanged` on prune.
- **Proof:** direct-test ALL PASS — with TTL 200ms, an ingested station is present then pruned after 350ms.

**#12 — `+ Add topic` (private streams).** Field subscribes to an arbitrary topic; unlisted stations
join the directory view; de-dupe across topics.
- **Headless UI test:** enter a topic → assert subscribe called with it; mocked station on that topic renders.

### Epic G — Player controls + polish  (P1)

**#13 — Player controls.** ✅ **DONE (2026-06-10).** **No pause** — for *live* radio, pausing is just
stop (the live edge moves on and MediaMTX rotates the HLS segments away). Controls are **Play / Stop /
Volume**. `setVolume(pct)` clamps 0–100 and restarts `ffplay -volume` (it has no runtime volume IPC);
now-playing bar has a volume slider + Stop.
- **Proof:** direct-test ALL PASS — setVolume reports 40 + still playing after the change, stop → stopped.

**#14 — Stream/Listen empty + transitional states.** ✅ **DONE (2026-06-10).** Listen empty state shows
a `BusyIndicator` + "Open to discover stations" / "Listening for stations…"; Stream uses the #8 status
light ("Waiting for OBS…").
- **Proof:** integration-test passes — both empty-state strings instantiate.

### Epic H — Hardening & ship  (P2)

**#15 — Error UX & silent-failure guards.** ✅ **DONE (2026-06-10, runtime-proven).** Backend returns
distinct codes — `mediamtx_not_found` (`QProcess::FailedToStart`) vs `mediamtx_port_or_config` (immediate
exit) vs `mediamtx_spawn_failed`; `ffplay_not_found` vs `ffplay_failed`; `no_delivery_client`;
`config_write_failed`; `name_required`; etc. UI: a checked `call()` helper maps every `{ok:false}` to
human copy and shows a dismissable error **banner** (`implicitHeight`, not `height`). Failing calls
routed through it: start/play/addTopic/startDiscovery.
- **Proof:** integration-test drives a real failed Start (mediamtx absent in the sandbox) → banner shows
  "Broadcast server (MediaMTX) isn't available on this system." — end-to-end error surfacing verified.
**#16 — LGX packaging + install.** `nix bundle ... #dual`, `lgpm` install recipe; relaunch script.
  Reuse: `builder-lgx-install-recipe`, `lgx-package-format`.
**#17 — README + user docs** (mirror beacon/stash README shape).
**#18 — Security pass.** Stream-key entropy, topic-string injection, subprocess arg quoting,
  no secrets in announce payloads. Reuse: `basecamp-security-patterns`, `~/basecamp/CODEX.md`.

---

## Execution order (spikes-first)

1. **#1** scaffold → **#2 SPIKE** MediaMTX bundling (de-risk the unknown before UI work).
2. **#3 #4** origin minting + status → **#5 #6** discovery announce/subscribe.
3. **#7 #8** Stream tab → **#9** Listen tab + play. **← P0 vertical slice done: cross-machine demo.**
4. **#10 #11 #12** liveness + private topics → **#13 #14** controls/polish.
5. **#15–#18** harden, package, document, security.

**First demo milestone:** after #9 — Khidr (2nd machine) discovers and plays a stream broadcast
from the primary, over LogosMessaging, no central index. (Reuse cross-machine setup from the
scorched-earth P2P notes: distinct `SCORCHED_TCP_PORT`-style node separation if both run locally.)

---

## Assumptions Register

| # | Assumption | Verification | Break condition |
|---|------------|--------------|-----------------|
| 1 | MediaMTX can be bundled (nixpkgs or vendored) and spawned from `logos_host` | ✅ #2 spike: in nixpkgs 1.18.2, spawns + serves HLS | Not in nixpkgs AND vendored binary won't run under the AppImage's glibc |
| 2 | `delivery_module` is present in the target AppImage | `logoscore` load + `getNodeInfo` smoke | Absent (was ❌ in v173) → must install separately or bundle as dep |
| 2b | `delivery_module` builds as a flake dependency | ⏳ pin `/v0.1.1` + `follows logos-module-builder` (mirrors scorched-earth) so RLN/zerokit/rust resolve from cache | main (0.1.2) without `follows` → nixpkgs mismatch → tries to build rust-default/zerokit from source → FAILS (observed 2026-06-09) |
| 3 | `ffplay` plays HLS `.m3u8` headless with `-nodisp` | local: `ffplay -nodisp -autoexit <m3u8>` | ffplay build lacks HLS demuxer (unlikely; ffmpeg full) |
| 4 | QML sandbox allows copy-to-clipboard for the OBS card | trivial QML clip-helper test | clipboard blocked → fall back to C++ `openUrl`/clip invokable |
| 5 | Heartbeat-only discovery is acceptable UX (no instant directory on launch) | product call — accepted for v1 | users expect instant list → add Store/cache later |
| 6 | A single MediaMTX instance serves the small target audience | brief constraint (origin uplink limit) | audience exceeds uplink → Phase-2 swarm (out of scope) |

## Evidence Matrix (current state)

| Claim | Tested-local | Tested-public | Build-only | Inferred |
|-------|:-:|:-:|:-:|:-:|
| Qt Multimedia absent → ffplay required | ✅ (skill, FUSE mount) | | | |
| QML sandbox blocks network/fs | ✅ (skill) | | | |
| `delivery_module` API shape + base64 | ✅ (source + live smoke) | | | |
| delivery_module has no Store/query | ✅ (source: plugin header) | | | |
| ffplay plays HLS .m3u8 | | | | ⚠️ (verify in #9) |
| MediaMTX in nixpkgs (1.18.2), spawns, serves HLS | ✅ (#2 spike 2026-06-10) | | | |
| MediaMTX needs `paths: all_others` for arbitrary paths | ✅ (#2 spike) | | | |
| delivery_module builds as a dep (pinned v0.1.1 + follows) | | | ✅ (#1 build green) | |
| radio_module + radio_ui compile (nix build) | ✅ (#1 2026-06-10) | | | |
| radio_ui QML loads + tab/form elements instantiate (integration-test) | ✅ (#1/#7 2026-06-10; NB expectTexts proves existence, not visible render) | | | |
| logos.callModule works in standalone app (real returns) | ✅ (#7 probe 2026-06-10) | | | |
| startStream mints card + spawns MediaMTX, stopStream tears down, path unique | ✅ (#3 2026-06-10, direct-test ALL PASS) | | | |
| getStreamStatus: waiting (no pub) → live (after ffmpeg push) | ✅ (#4 2026-06-10, direct-test ALL PASS) | | | |
| ingestAnnounce: base64 decode + parse + self-echo/malformed filter | ✅ (#5 2026-06-10, direct-test) | | | |
| announce schema + gating (not_live → gate passes when live) | ✅ (#6 2026-06-10, direct-test) | | | |
| play (ffplay) → playing, stop → stopped | ✅ (#9 2026-06-10, direct-test) | | | |
| Listen-tab UI elements instantiate | ✅ (#9 2026-06-10, integration-test) | | | |
| error UX: failed Start surfaces a visible banner | ✅ (#15 2026-06-10, integration-test drives real failure) | | | |
| delivery_module wiring (createNode/subscribe/onEvent) | | | ✅ (#5 builds + module loads) | |
| live delivery_module send/receive round-trip | | | | ⚠️ needs AppImage (2 nodes; logoscore gates returns) |
| radio_module loads + dispatches ping (logoscore, isolated dir) | ✅ (#1 2026-06-10: registry connect + "Method call successful", same as canonical capability_module) | | | |
| Q_INVOKABLE JSON return value readback | | | | ⚠️ blocked in bare logoscore — capability handshake fails for ALL modules (capability_module.requestModule also returns `false`); needs AppImage |
| initLogos = Q_INVOKABLE not override | ✅ (loads in logoscore; canonical capability_module uses identical signature — capability_module_plugin.h:24) | | | |
| tutorial-v3 scaffold is current | ✅ (upstream, updated 2026-06-09) | | | |

## Silent failure modes to guard (enumerate before coding)
- Empty/invalid `metadata.json` → module silently not discovered. (#1)
- Missing `variant` file / wrong `view` path → UI silently blank. (#1, #7)
- `delivery_module` absent → `subscribe` no-ops; no error. (#5, #15)
- QML uses blocked import (`QtGraphicalEffects`, `FileDialog`, network URL) → blank screen, no log. (#7–#9)
- MediaMTX port already in use → start fails quietly. (#2, #15)
- `height:` binding inside a layout silently overridden (use `implicitHeight`). (#8)

## Reused patterns / skills (don't re-derive)
`logos-module-builder-scaffold` · `builder-core-module-src-layout` · `git-init-gitignore-first` ·
`delivery-module-messaging` · `delivery-module-mp-guard-resubscribe` · `ffplay-subprocess-player` ·
`qml-sandbox-restrictions` · `qml-callmodule-reentrancy-guard` · `qml-callmoduleparse-double-json` ·
`builder-lgx-install-recipe` · `basecamp-security-patterns` · scorched-earth `game_plugin.cpp` (delivery init) ·
soulseek `PlayerManager.h` / `PlayerBar.qml` (ffplay). Memory: QML layout `implicitHeight` bug.

## Headless-testing strategy (summary) — revised after 2026-06-10 trial
- **UI (`radio_ui`)** — ✅ **WORKING.** `tests/ui-tests.mjs` (v3 framework) → `nix build .#integration-test`
  loads the plugin in the standalone app and asserts rendered text. First test green (both tabs render).
  Extend per-issue (#7/#8/#9/#14) with mocked backend states.
- **Core (`radio_module`)** — **use `logoscore`, NOT the builder's `#unit-tests`.** Finding (2026-06-10):
  the builder's `logos_test()` framework (`LOGOS_ASSERT`/`mockCFunction`) targets the tutorial's
  **`_impl` module style** (plain class wrapping a C lib). Our module is **`_plugin`/QObject** style, so
  `#unit-tests` doesn't fit (the auto-detected `tests/CMakeLists.txt` fails the `logos_test` contract).
  - **Tier 2 (primary proof):** `tests/run-headless-tests.sh` installs the built `.so` into an **isolated
    temp `--modules-dir`** (never the shared Basecamp dir) with a `-dev` manifest variant + RPATH patch
    (`logoscore-headless-testing` skill), then `logoscore -c "radio_module.ping()"`. This loads the real
    plugin → fires `initLogos` → the meaningful runtime proof. Network tests (#5) XFAIL when
    `delivery_module` absent (logged, never silent-pass).
  - **Tier 1 (in-process, WORKING):** `tests/run-direct-test.sh` builds `tests/direct_test.cpp` against the
    plugin + `liblogos_sdk.a` and instantiates `RadioModulePlugin` directly — **no IPC/capability layer**, so
    it can read real return values and observe side effects (the only way to prove side-effectful methods
    headlessly). Auto-derives Qt/SDK/SSL paths (ldd + `nix develop` env). This is where #3 was proven and
    where #4/#9/#13 logic gets verified. The builder's `#unit-tests` is NOT used (it targets the `_impl`
    module style; raw Qt::Test scaffolding was removed).
