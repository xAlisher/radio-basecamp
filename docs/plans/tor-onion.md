# Epic: Tor onion-service mode — hide the streamer's IP

**Goal:** publish a `.onion` stream URL (no IP in the announce) so listeners play it over Tor —
hiding **both** the host's and listeners' IPs **and** traversing NAT with no port-forwarding. Audio-first
(low bitrate) fits Tor's bandwidth. **Onion is the DEFAULT** (internet radio shouldn't be LAN-only or
leak the host IP); **Direct (LAN)** is the opt-in for local/low-latency use, labelled as IP-exposing.
Closes the gap in [`docs/BRIEF.md` §Privacy](../BRIEF.md) and the README Privacy section.

## Why Tor (vs Tailscale / Cloudflare)
Smallest change for this module: discovery is already *a URL on Waku*, so onion mode is mostly a **URL
swap + a SOCKS-routed `ffplay`**. No account, no domain, no CDN — fits the "no platform" ethos. Tailscale
suits private/among-friends streams; Cloudflare is discouraged for media + centralizes trust. (Research:
BRIEF §Privacy, refs below.)

## Design

```
Host (onion mode)                         Tor network                 Listener
 OBS → MediaMTX (HLS :8888)                    |                          |
 tor: HiddenService 80 → 127.0.0.1:8888  --> <hash>.onion                 |
 announce streamUrl = http://<hash>.onion/<path>/index.m3u8 ----(Waku)--->| discovers
 |<==== ffplay pulls HLS via torsocks → Tor SOCKS :9050 → rendezvous =====>| plays
```

- **Two independent `tor` processes** (Senty ISSUE-2) so host + listener lifecycles can't tear each
  other down: a **host tor** (`SocksPort 0` + `HiddenServiceDir` mapping `80 → 127.0.0.1:<HLS port>`)
  and a **listener tor** (`SocksPort`, for playing a `.onion`). `torsocks` is locked onto the listener
  tor's SOCKS port via `TORSOCKS_TOR_PORT`.
- Host reads `<hsdir>/hostname` → the `.onion`; `buildAnnouncePayload` uses it **instead of** `lanIp()`,
  and never falls back to `lanIp()` in onion mode. Announce is gated on descriptor publish.
- Listener: `startFfplay` detects a `.onion` host (canonical `isOnionUrl`) → wraps `ffplay` with
  `torsocks`. The `play()` http/https allow-list is unchanged (`.onion` is http).
- `startStream` config takes `"privacy": "onion" | "public"`, **default `onion`**.

## Issues

| # | Epic | Title | Priority | Status |
|---|------|-------|----------|--------|
| T1 | Spike | Validate: tor HiddenService + `torsocks` fetch end-to-end | P0 | ✅ done (fetched over Tor) |
| T2 | Host | `ensureTor()` — spawn/lifecycle a tor daemon (SocksPort), `dieWithParent`, env override | P0 | ✅ impl |
| T3 | Host | Onion mode — HiddenService for MediaMTX, read `.onion`, wait-for-descriptor state | P0 | ✅ impl (async poll) |
| T4 | Host | Announce the `.onion` URL in onion mode — **no IP** in payload, card, or logs | P0 | ✅ impl + test |
| T5 | Listen | Route `ffplay` via `torsocks` for `.onion` URLs (keep http/https allow-list) | P0 | ✅ impl + test |
| T6 | Build | `tor` + `torsocks` nix runtime deps + `RADIO_TOR_BIN` / `RADIO_TORSOCKS_BIN` overrides | P0 | ✅ impl (builds) |
| T7 | UI | Privacy toggle (Public/Onion), show `.onion`/"hidden", 🧅 listen badge, "Connecting over Tor…" | P1 | ✅ impl + test |
| T8 | Test | direct-test: onion announce carries `.onion` (no IP); play routes `.onion` via torsocks; tor lifecycle | P1 | ✅ done (23/23) |
| T9 | Harden | No IP leak; split host/listener tor; canonical onion flag; torsocks port-lock; secret cleanup; timeout UX | P2 | ✅ Senty 5-round audit → CLEAN (11 fixes) |
| T10 | Docs | BRIEF/README: onion mode shipped — usage + residual trade-offs (latency, both need tor) | P2 | — |

> **Runtime-verification pending:** the spike proved the tor+onion+fetch chain and the direct-test proves
> the module logic (announce URL, torsocks routing). The *full* in-AppImage flow (onion broadcast →
> listener plays over Tor) still needs a live run — deferred so as not to disturb the running demo.

**P0 vertical slice (T1–T6) = a working onion broadcast a separate listener plays with no IP exposed.**

## Tests
- `tests/run-direct-test.sh` additions (T8): onion-mode `buildAnnouncePayload` yields a `.onion`
  `streamUrl` and **never** contains an RFC1918/`lanIp()` literal; `startFfplay` for a `.onion` URL
  builds a `torsocks ffplay …` argv; `play()` still rejects non-http(s).
- Spike harness (T1) proves the tor+onion+torsocks chain outside the module.
- UI test (T7): the privacy toggle renders and flips the announced-URL display.

## Trade-offs / non-goals
- +Latency and lower bandwidth vs direct — acceptable for audio, not for video.
- Both host and listener need a `tor` daemon (bundled). First-connect is slow (descriptor publish +
  rendezvous). Not anonymity against a global passive adversary — it hides the *IP*, not that you're
  using radio-basecamp.

## References
- onion-livestreaming (HLS over Tor): https://github.com/meetkool/onion-livestreaming
- Tor SOCKS extensions: https://spec.torproject.org/socks-extensions.html
- Cloudflare Tunnels discouraged for streaming: https://www.xda-developers.com/cloudflare-tunnels-are-great-but-never-use-them-for-media-streaming/
