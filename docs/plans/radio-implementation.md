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

**#3 — Ingest URL + stream-key minting.** `startStream(name, visibility, description)` returns
`{whipUrl, rtmpUrl, srtUrl, streamKey, path}`. Random per-stream path/key.
- **Headless test:** assert returned JSON has all fields; key is non-empty + unique across two calls.

**#4 — MediaMTX status polling.** `getStreamStatus()` → `{state: idle|waiting|receiving|live, hlsUrl}`
by polling MediaMTX HTTP API in C++ (`QNetworkAccessManager`). Emits `streamStatusChanged` event.
- **Headless test:** with no OBS connected, status is `waiting`; stub/mock the HTTP layer in `tests/mocks/`
  to return a publisher-present payload → assert `receiving`/`live`.

### Epic C — Discovery: announce + subscribe  (P0)

**#5 — `delivery_module` init + topic plumbing.** Wire `createNode/start/subscribe` (config
`{"mode":"Core","relay":true,...}`), `messageReceived` handler with single-`base64` decode,
self-echo filter via a sent-seq Set. Topic helpers (`/radio-basecamp/1/<channel>/json`).
- Reuse: `delivery-module-messaging`, `delivery-module-mp-guard-resubscribe`, scorched-earth `game_plugin.cpp`.
- **Headless test:** `tests/run-headless-tests.sh` (Tier-2, network) — two `logoscore` instances,
  A subscribes + B sends on the same topic, assert A's `messageReceived` payload round-trips. If
  delivery_module absent in target AppImage, mark XFAIL with a logged skip (no silent pass).

**#6 — Announce schema + publish.** Host announce payload `{v, name, host, streamUrl, visibility,
description, startedAt, seq}` published on the directory topic (public) or private topic. Announce
begins only when status == `receiving`/`live` (#4), stops on `stopStream`.
- **Headless test:** assert published payload validates against schema; announce is gated on stream state.

### Epic D — Stream tab UI  (P0→P1)

**#7 — Stream tab: setup card + start/stop.** QML: name field, Public/Private toggle, optional
description, Start/Stop. Calls `startStream`/`stopStream`. OBS setup card with copy-to-clipboard
(no `Qt.openUrlExternally` — sandbox; use clip helper per `qml-sandbox-restrictions`).
- **Headless UI test (`radio_ui/tests/ui-tests.mjs`):** load UI, `expectTexts(["Stream","Start"])`;
  after a mocked start, assert the OBS card text (server URL / stream key labels) renders.

**#8 — Live status light.** QML polls `getStreamStatus()` (1–2s timer); renders Waiting → Receiving
→ 🔴 Live. Layout: use `implicitHeight`, not `height`, inside layouts (known QML layout bug).
- **Headless UI test:** drive status via mocked backend states → assert the three label transitions.

### Epic E — Listen tab + playback  (P0→P1)

**#9 — Listen tab: directory render + tap-to-play.** Subscribe to directory topic on tab open;
`getStations()` returns the live list; render name/host/uptime. Tap → `play(hlsUrl)` → `ffplay`.
- Reuse: `ffplay-subprocess-player` (PlayerManager), soulseek `PlayerBar.qml`.
- **Headless test (core):** `play(<local test m3u8 or audio>)` → `getPlayerStatus()` reports `playing`;
  `stop()` → `stopped`. **UI test:** seed mocked stations → assert rows render; tap → backend `play` called.

### Epic F — Liveness  (P1)

**#10 — Heartbeat re-announce (15s).** Host re-publishes announce every 15s while live (`QTimer`).
- **Headless test:** observe ≥2 announces within 35s on the topic (Tier-2) or assert timer cadence via injectable clock.

**#11 — TTL expiry (45s).** Listener drops stations not heard within 45s; emits `stationsChanged`.
- **Headless test:** inject an announce, advance mock clock >45s without re-announce → station removed from `getStations()`.

**#12 — `+ Add topic` (private streams).** Field subscribes to an arbitrary topic; unlisted stations
join the directory view; de-dupe across topics.
- **Headless UI test:** enter a topic → assert subscribe called with it; mocked station on that topic renders.

### Epic G — Player controls + polish  (P1)

**#13 — Player controls.** pause/resume (`SIGSTOP`/`SIGCONT`), stop, volume, now-playing bar.
- **Headless test:** play→pause→resume→stop sequence asserts each `getPlayerStatus()` state.

**#14 — Stream/Listen empty + transitional states.** "No live stations yet", "Waiting for OBS…",
connecting spinners.
- **Headless UI test:** assert empty-state copy with no stations / no stream.

### Epic H — Hardening & ship  (P2)

**#15 — Error UX & silent-failure guards.** Surface: MediaMTX failed to start, ffplay missing,
delivery_module absent, port in use. Each maps to a visible message (no silent dead-ends).
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
| radio_ui loads + renders both tabs (integration-test) | ✅ (#1 2026-06-10, runtime: plugin loaded, expectTexts passed) | | | |
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
  - **Tier 1 (optional):** standalone Qt::Test exe with SDK includes wired manually, if fast offline unit
    coverage is wanted later. The current `tests/CMakeLists.txt` + `tests/test_radio.cpp` are placeholders
    for this — they are NOT the builder `#unit-tests` path and need include wiring before they run.
