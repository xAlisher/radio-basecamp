# radio-basecamp — module instructions

Decentralized audio broadcast with LogosMessaging discovery. Two modules: `radio_module`
(core — origin/discovery/playback logic) + `radio_ui` (QML-only — two-tab UI).

**Read first:** [`docs/BRIEF.md`](docs/BRIEF.md) (feasibility insights are binding) and
[`docs/plans/radio-implementation.md`](docs/plans/radio-implementation.md) (epics, issues, priorities, headless tests).

## Hard constraints (don't re-derive — see BRIEF §Feasibility)
- **No Qt Multimedia** in the AppImage → playback is `ffplay`/`ffprobe` via `QProcess` (`ffplay-subprocess-player`).
- **QML sandbox**: no network, no fs outside module dir, no `QtGraphicalEffects`/`FileDialog` (`qml-sandbox-restrictions`).
  All network/subprocess I/O lives in `radio_module` (C++), never in QML.
- **Discovery = `delivery_module`** (LogosMessaging, ex-Waku): `subscribe`/`send` + `messageReceived`,
  single-`base64` decode, no Store/history → heartbeat-only (`delivery-module-messaging`).
- Inside QML layouts use `implicitHeight`, never `height` (silently overridden).

## Architecture (tutorial-v3 canonical: core + QML UI)
`radio_module` (`mkLogosModule`) ← logic & state. `radio_ui` (`mkLogosQmlModule`) ← view, calls
it via `logos.callModule("radio_module", method, [args])`. Every backend method returns a JSON string.

## Build / test
```bash
cd radio_module && nix build           # core plugin
cd radio_ui     && nix build && nix run .   # QML UI in standalone app
# headless: radio_module/tests/run-headless-tests.sh (logoscore)  ·  nix build .#integration-test (UI)
```
Nix only sees git-tracked files — `git add -A` before building.

## Process / fieldcraft
- Universal protocols: `~/fieldcraft/protocols/` (plan-first, scope-freeze, audit-before-merge).
- Platform skills: `~/basecamp/basecamp-skills/skills/` — read the index sheet for the phase first.
- ≤~200-line diffs per issue; one issue at a time, in priority order; extract a skill after each.

## Issues (see docs/plans/radio-implementation.md for detail)
| # | Epic | Title | Priority | Status |
|---|------|-------|----------|--------|
| 1 | Scaffold | Scaffold both modules, buildable | P0 | done |
| 2 | Origin | SPIKE: bundle + spawn MediaMTX | P0 | done |
| 3 | Origin | Ingest URL + stream-key minting | P0 | done |
| 4 | Origin | MediaMTX status polling | P0 | done |
| 5 | Discovery | delivery_module init + topic plumbing | P0 | todo |
| 6 | Discovery | Announce schema + publish | P0 | todo |
| 7 | Stream UI | Setup card + start/stop | P0 | todo |
| 8 | Stream UI | Live status light | P0 | todo |
| 9 | Listen | Directory render + tap-to-play | P0 | todo |
| 10 | Liveness | Heartbeat re-announce (15s) | P1 | todo |
| 11 | Liveness | TTL expiry (45s) | P1 | todo |
| 12 | Liveness | + Add topic (private streams) | P1 | todo |
| 13 | Player | Player controls (pause/resume/vol) | P1 | todo |
| 14 | Polish | Empty + transitional states | P1 | todo |
| 15 | Harden | Error UX & silent-failure guards | P2 | todo |
| 16 | Ship | LGX packaging + install | P2 | todo |
| 17 | Ship | README + user docs | P2 | todo |
| 18 | Ship | Security pass | P2 | todo |

**P0 vertical slice (#1–#9) = the thesis demo:** a host broadcasts, a separate listener
discovers-and-plays over LogosMessaging with no central index.
