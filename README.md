# radio-basecamp

Decentralized **audio broadcast** module for [Logos Basecamp](https://github.com/logos-co/logos-app).
A host broadcasts a stream; listeners **discover it over LogosMessaging by topic** — no central
index, no account — and play it. The differentiator is **discovery, not delivery**.

> Status: **specs resolved, scaffolded** (2026-06-09). Implementation tracked in
> [`docs/plans/radio-implementation.md`](docs/plans/radio-implementation.md). Audio-first v1.

---

## What it does

- **Broadcast** — point OBS at a generated ingest URL; the module runs the origin and announces.
- **Discover** — listeners subscribe to a well-known topic and see live stations appear (heartbeat).
- **Listen** — tap a station to play its stream. No peer connections — a plain pull from the origin.
- **Sovereign** — no platform, no directory server, no account. Private streams via a shared topic string.

## How it works

```
Host                       LogosMessaging topic            Listener
 | OBS → MediaMTX (WHIP/RTMP)        |                         |
 | MediaMTX serves HLS .m3u8         |                         |
 | announce(name,url,…) heartbeat -->|------------------------>| discovers station
 |                                   |                         |
 | <===== HTTP: listener's ffplay pulls HLS from MediaMTX =====>|
```

Two modules (tutorial-v3 canonical: core + QML UI):

| Module | Type | Role |
|--------|------|------|
| `radio_module` | `core` | MediaMTX origin control, `ffplay` playback, `delivery_module` discovery, heartbeat/TTL |
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
| **delivery** | `delivery_module` | [logos-delivery-module](https://github.com/logos-co/logos-delivery-module) | LogosMessaging announce/subscribe |

External (host-side, not bundled): **OBS Studio** (capture), **MediaMTX** (origin), **ffmpeg/ffplay** (playback, system).

## Build

```bash
git add -A                       # Nix only sees tracked files
cd radio_module && nix build     # → result/lib/radio_module_plugin.so
cd ../radio_ui   && nix run .     # launches the UI in logos-standalone-app
```

## Test (headless)

```bash
radio_module/tests/run-headless-tests.sh          # logoscore integration (Tier 2)
cd radio_module && cmake -B build -DRADIO_BUILD_TESTS=ON && ctest --test-dir build   # Qt::Test (Tier 1)
cd radio_ui && nix build .#integration-test -L     # QML UI test (ui-tests.mjs)
```

## License

TBD.
