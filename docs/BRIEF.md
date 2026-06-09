# radio-basecamp — Decentralized A/V Broadcast Module

> Context doc for the module. Basecamp module, QML + C++ (Qt 6), LogosMessaging discovery.
> **Status: specs resolved 2026-06-09. Cleared to build in priority order (see `plans/radio-implementation.md`).**

## What this is

A Basecamp module for live **audio** broadcast with **decentralized discovery**. A host
broadcasts a stream; listeners discover it over a LogosMessaging topic — no central index —
and play it. Pure broadcast-and-listen. **No tokens, no incentives, no peer-to-peer media
swarm in v1.**

The differentiator is **discovery, not delivery.** Anyone can run an HLS server; what's ours
is that streams are found over LogosMessaging by topic, with no platform, no central directory,
no account. The media itself is served by a normal origin (MediaMTX). Swarm-based
peer-assisted delivery is a Phase-2 scaling optimization (see Appendix in the original brief).

---

## ⚠️ Feasibility Insights (2026-06-09) — corrections to the original brief

Research against the live AppImage, basecamp-skills, the `logos-delivery-module` source, and
the upstream `logos-module-builder` / `logos-tutorial` **tutorial-v3** (both updated 2026-06-09)
overturned three assumptions in the original brief. **These are binding for v1.**

### 1. Playback: ❌ Qt Multimedia → ✅ `ffplay` subprocess
`libQt6Multimedia.so` is **not bundled in the Logos Basecamp AppImage** — confirmed by FUSE
mount inspection (`ffplay-subprocess-player` skill). Any module importing `QtMultimedia` /
`QMediaPlayer` fails **silently** at runtime. System `ffplay`/`ffprobe` (from ffmpeg) ARE at
`/usr/bin/`. `ffplay` plays an HLS `.m3u8` URL natively. → **Playback = `ffplay -nodisp -autoexit <m3u8>`
spawned via `QProcess`**, pause/resume via `SIGSTOP`/`SIGCONT`. Proven in `soulseek-basecamp/src/core/PlayerManager.h`.

### 2. The QML sandbox forces all I/O into C++
Logos `ui_qml` plugins run sandboxed: **no network access, no filesystem outside the module
dir, no `data:` URIs** (`qml-sandbox-restrictions` skill). Therefore HLS playback, MediaMTX
HTTP status polling, subprocess control, and base64 must all live in **C++**, not QML.

### 3. "Waku" is now **LogosMessaging** → the `delivery_module` core module
Discovery is **not** nim-libp2p direct. It goes through `delivery_module` (repo
`logos-co/logos-delivery-module`, wraps `liblogosdelivery` = LogosMessaging, ex-Waku; updated
2026-06-09, RLN spam-protection built in). API (confirmed from source +
`delivery-module-messaging` skill): `createNode / start / subscribe / send / unsubscribe /
getNodeInfo`, event `messageReceived` with `data[0]=hash, data[1]=contentTopic, data[2]=base64(payload), data[3]=ts_ns`.
**There is no Store/history query method** → discovery is **heartbeat-only** (a station appears
within one heartbeat interval; it cannot be back-queried on launch). The old `logos-waku-module`
(March 2026) is superseded — do not use it.

### Architecture (per tutorial-v3 canonical pattern: core + QML UI)
The brief's 4-package layout (`origin/discovery/player/ui`) collapses into **two modules**:

| Module | Type | Responsibility |
|--------|------|----------------|
| **`radio_module`** | `core` (`mkLogosModule`) | All logic: spawn/manage MediaMTX, spawn/control `ffplay`, call `delivery_module` for announce/subscribe, poll MediaMTX HTTP status, heartbeat + TTL. Headless-testable via `logoscore`. |
| **`radio_ui`** | `ui_qml` QML-only (`mkLogosQmlModule`) | Two-tab Stream/Listen UI. Calls `radio_module` via `logos.callModule()`. No compilation, sandboxed. Headless UI test via `tests/ui-tests.mjs`. |

This mirrors tutorial Part 1 (core) + Part 2 (QML UI) and every existing basecamp module
(beacon/stash/keeper/cord). The heavier "C++ backend inside the UI" shape (tutorial Part 3,
`.rep` Qt Remote Objects) is **not** used — we don't need UI-backend process isolation.

### v1 scope: **audio-first**
`ffplay -nodisp` plays audio cleanly and fully in-sandbox. Video *display* cannot embed in the
QML view (would need an external ffplay window or frame-piping). The host's OBS can still push
A/V to MediaMTX; **v1 listeners play audio.** Video display is deferred to a later phase.

---

## Resolved open questions (was §6 of original brief)

| # | Question | Resolution |
|---|----------|-----------|
| 1 | HLS vs WHEP for listener output | **HLS, ~10s.** No per-listener origin state; `ffplay` plays `.m3u8` directly. WHEP deferred. |
| 2 | Directory topic string + namespacing | **`/radio-basecamp/1/directory/json`** (follows delivery_module content-topic convention `/<module>/1/<channel>/<format>`). Private/unlisted streams use `/radio-basecamp/1/<random-id>/json`, shared out-of-band. Per-community sub-directories: `/radio-basecamp/1/dir-<community>/json`. |
| 3 | Heartbeat interval + announce TTL | **Heartbeat every 15s; TTL 45s** (3 missed beats → station dropped from listener directory). Messages are tiny, so 15s is cheap; station appears ≤15s, drops ≤45s after going dark. |

---

## UX — two tabs: Stream + Listen
*(unchanged from original brief — summarized)*

- **Stream tab:** name + Public/Private + optional description → **Start**. Module starts
  MediaMTX, mints the WHIP ingest URL + stream key, shows an **OBS setup card** (server URL,
  key, copy buttons). Status light polled from MediaMTX: *Waiting for OBS → Receiving → 🔴 Live
  (announcing)*. The heartbeat announce begins only once the origin is actually receiving.
  **Stop** ends MediaMTX + announce. The module never captures media — OBS does.
- **Listen tab:** subscribes to the directory topic on open; live stations render (name, host,
  uptime) as their heartbeats arrive. Tap a station → `ffplay` plays its HLS URL. **+ Add topic**
  field at the bottom subscribes to a private topic so its unlisted stations join the view.
  Stale stations drop off via TTL.

## Stack (corrected)

| Layer | Choice | Notes |
|---|---|---|
| Host capture | **OBS Studio** (host-operated) | Module never touches mic/cam. OBS → MediaMTX via WHIP/SRT/RTMP. |
| Origin server | **MediaMTX** | Spawned + managed by `radio_module` via `QProcess`. Ingest → HLS out. See Issue #2 spike for bundling. |
| Discovery | **`delivery_module`** (LogosMessaging) | IPC from `radio_module`. Heartbeat announce + subscribe. |
| Playback | **`ffplay` / `ffprobe`** subprocess | `QProcess`, HLS `.m3u8`. Audio-first v1. NOT Qt Multimedia. |
| UI | **QML** (`radio_ui`, sandboxed) | Two tabs; calls `radio_module` via `logos.callModule()`. |

## Non-goals (v1)
No mic/camera capture (OBS only). No P2P media swarm (MediaMTX serves directly — Phase 2). No
incentive/token layer. No transcoding marketplace, no DRM. **No video display in the listener
(audio-first v1).**

## Constraints
- Live latency ~10s (HLS). Fine for broadcast radio.
- **Origin uplink is the scaling limit** — MediaMTX serves every listener directly; host
  bandwidth caps the audience. This is the wall Phase 2 (swarm) removes. Don't pre-optimize.
- Discovery liveness depends on heartbeat/TTL (no Store to fall back on).
