# radio-basecamp

Decentralized **audio broadcast** module for [Logos Basecamp](https://github.com/logos-co/logos-app).
A host broadcasts a stream; listeners **discover it over LogosMessaging by topic** — no central
index, no account — and play it. The differentiator is **discovery, not delivery**.

> Status (2026-06-10): **P0 vertical slice complete** — backend (origin, discovery, playback) and
> the Stream + Listen UI are built and tested. The remaining step is the live cross-machine demo
> (the `delivery_module` network round-trip). v1 is **audio-first**. Implementation tracked in
> [`docs/plans/radio-implementation.md`](docs/plans/radio-implementation.md).

---

## What it does

- **Broadcast** — point OBS at a generated ingest URL; the module runs the origin and announces.
- **Discover** — listeners subscribe to a well-known topic and see live stations appear (heartbeat).
- **Listen** — tap a station to play its stream via `ffplay`. No peer connections — a plain pull from the origin.
- **Sovereign** — no platform, no directory server, no account. Private streams via a shared topic string.

## Quick start

### Broadcasting (host)
1. Open **radio** → **Stream** tab → name your station → **Public**/**Private** → **Start**.
2. The module shows an **OBS setup card** (WHIP / RTMP / SRT). Copy the values into OBS and
   **Start Streaming** in OBS. → **Full guide: [`docs/CONNECTING-OBS.md`](docs/CONNECTING-OBS.md)**
3. When the status light turns **🔴 Live**, your station is announced over LogosMessaging.

### Listening
1. Open **radio** → **Listen** tab. It subscribes to the directory topic; live stations appear
   (name · host · uptime) as their heartbeats arrive.
2. **Tap a station** to play it. Use the **Stop** bar to stop.
3. For a private/unlisted station, paste its topic into **+ Add a private topic**.

Dead stations drop off the list automatically (45 s TTL = 3 missed heartbeats).

## How it works

```
Host                       LogosMessaging topic            Listener
 | OBS → MediaMTX (WHIP/RTMP/SRT)    |                         |
 | MediaMTX serves HLS .m3u8         |                         |
 | announce(name,url,…) heartbeat -->|------------------------>| discovers station
 |                                   |   (15s re-announce)     |
 | <===== HTTP: listener's ffplay pulls HLS from MediaMTX =====>|
```

Two modules (tutorial-v3 canonical: core + QML UI):

| Module | Type | Role |
|--------|------|------|
| `radio_module` | `core` | MediaMTX origin control, ingest minting, status polling, `ffplay` playback, `delivery_module` discovery, heartbeat/TTL |
| `radio_ui` | `ui_qml` (QML-only) | Two tabs — Stream / Listen — calling `radio_module` via the `logos` bridge |

**Key platform facts** (see [`docs/BRIEF.md`](docs/BRIEF.md) §Feasibility): Qt Multimedia isn't in
the AppImage (playback uses `ffplay`); the QML sandbox blocks network/subprocess (all I/O is in the
core module); "Waku" is now `delivery_module` (LogosMessaging), which has no history query, so
discovery is heartbeat-only.

## Dependencies

| Module | Installed name | Repo | Role |
|--------|----------------|------|------|
| **radio** (this) | `radio_module` | this repo | core logic |
| **radio-ui** (this) | `radio_ui` | this repo | QML UI |
| **delivery** | `delivery_module` | [logos-delivery-module](https://github.com/logos-co/logos-delivery-module) (pinned v0.1.1) | LogosMessaging announce/subscribe |

External (host-side, system / bundled): **OBS Studio** (capture), **MediaMTX** (origin, bundled via
nixpkgs), **ffmpeg/ffplay** (playback, system).

## Build

```bash
git add -A                       # Nix only sees tracked files
cd radio_module && nix build     # → result/lib/radio_module_plugin.so
cd ../radio_ui   && nix run .     # launches the UI in logos-standalone-app
```

## Test (headless)

```bash
# Core — in-process harness (instantiates the plugin; proves start/spawn/mint/status/play/announce/TTL)
radio_module/tests/run-direct-test.sh
# Core — logoscore load/dispatch smoke
radio_module/tests/run-headless-tests.sh
# UI — QML loads + elements instantiate
cd radio_ui && nix build .#integration-test -L
```

See [`docs/plans/radio-implementation.md`](docs/plans/radio-implementation.md) (evidence matrix)
for exactly what each test proves and what still needs the running AppImage.

## Configuration (env overrides)

| Var | Default | Purpose |
|-----|---------|---------|
| `RADIO_RTMP_PORT` / `RADIO_WHIP_PORT` / `RADIO_SRT_PORT` / `RADIO_HLS_PORT` / `RADIO_API_PORT` | 1935 / 8889 / 8890 / 8888 / 9997 | Origin ports |
| `RADIO_DIRECTORY_TOPIC` | `/radio-basecamp/1/directory/json` | Public discovery topic |
| `RADIO_HEARTBEAT_MS` | 15000 | Re-announce interval |
| `RADIO_TTL_MS` | 45000 | Listener drops a station after this without a heartbeat |
| `RADIO_MEDIAMTX_BIN` / `RADIO_FFPLAY_BIN` | `mediamtx` / `ffplay` | Binary paths |

## License

TBD.
