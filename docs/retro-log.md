# Retro Log

Raw captures, reshuffled into PROJECT_KNOWLEDGE.md / skills at `/retro`.

## Week of 2026-06-10 — Tor onion epic (merged 223c5ff)

### Wins
- [project] Onion radio works end-to-end across two machines (OBS → MediaMTX → Tor HS → discovery →
  buffered torsocks playback) with no IP exposure. Listener confirmed "arrived, no chops".
- [process] **Diagnostic-file-over-swallowed-stderr** cracked two opaque failures: writing the spawned
  binary's output to `/tmp/radio_module/tor-fail.log` revealed the `evutil_secure_rng_add_bytes`
  symbol error; inspecting tor's `hs.log` + bootstrap revealed the onion was published while the UI
  said "publishing". logos_host swallows child stderr (#163) — always persist it to a file.
- [process] A/B'd ffplay/ffprobe flags + raw `curl -D -` over Tor to isolate the exact cause (Secure
  cookie) instead of guessing — confirmed the fix pulled 40s of audio at 3-20x before shipping.

### Fails
- [project] Mislabeled the listener failure as `tor_port_in_use` and built a port-retry that could
  never help. Wrong action: the immediate-exit catch-all assumed a port conflict. Root cause: the apt
  `/usr/sbin/tor` child inherited the AppImage's `LD_LIBRARY_PATH` → loaded the wrong libevent → died
  on a missing symbol. Only system (non-nix) binaries hit this, so it was invisible on wild (nix tor).
  → skill `appimage-child-ld-library-path` (critical).
- [project] Moving the Tor `HiddenServiceDir` to a persistent path regressed `pollOnionStatus`, which
  still read the hostname from the old temp path → `m_onion` stayed empty → readiness never checked →
  false `publish_timeout` + the heartbeat announced no/stale onion (a second cause of "no sound"). Root
  cause: moved a file's location without updating its reader. → PROJECT_KNOWLEDGE.
- [project] MediaMTX gates HLS behind a `Secure` cookieCheck cookie; ffmpeg won't return a Secure
  cookie over the `http://` onion → 302 loop → silent no-audio. Not variant-specific (both lowLatency
  and mpegts). Fix: ffplay `-cookies "cookieCheck=1; path=/"`. → PROJECT_KNOWLEDGE.
- [process] Ran the public-stream direct test repeatedly on the live demo host (mediamtx respawn races
  under load) → noisy flakes on heartbeat/regenerateKey. Root cause: no XDG isolation + competing for
  ports with the running demo. Fixed test isolation (`XDG_DATA_HOME=$(mktemp -d)`); flakes are the
  respawn timing, not regressions (clean run is ALL PASS).

### Skills touched
- Extracted `appimage-child-ld-library-path` (basecamp-skills, ops/critical).
- Module lessons (cookieCheck, persistent HS dir + hostname reader, reuse-on-start) → PROJECT_KNOWLEDGE.

## Week of 2026-06-10 — synthesized (no inline /log captures this run)

### Wins
- [process] Unbuffered `fprintf(stderr)+fflush` markers cracked a crash that `qDebug` + swallowed
  ui-host stderr hid — pinpointed the exact line (`new LogosModules(api)`). Reach for this on any
  load-time crash, not gdb first.
- [process] User's leads ("hackyguru / fryorcracken", "check upstream") routed straight to the
  canonical `logos-delivery-demo` + upstream #31, which was the root cause. Following named people →
  their repos/issues beat re-deriving.
- [process] Isolated dual-AppImage run (separate binary + `XDG_DATA_HOME`) let radio run on the
  working older release without touching the other agent's 295 setup. Reusable ops pattern.
- [project] Cross-machine thesis demo works on `pre-release-1dc1c08-268`: separate listener discovers
  + plays over LogosMessaging, no central index.

### Fails
- [process] Chose `core + QML` architecture without reading delivery_module's OPEN issues; #31
  (core-can't-consume-delivery) was knowable on day one. Root cause: platform-state-check read the
  dep's API/headers but never `gh issue list` on the dep. → fieldcraft: platform-state-check now must
  scan the dep's open issues. (memory: feedback_check_dependency_open_issues)
- [process] Recommended AGAINST the ui_qml-C++-backend at the start as "needless complexity" — it's
  the ONLY supported delivery-consumption path. Root cause: dismissed the heavier tutorial option
  without checking whether the lighter one was supported for the deps in play.
- [process] Long crash-spiral: tried builder pin → SDK → delivery tag → call-pattern → defer-timing
  before adding logging / checking upstream. The two moves that worked (stderr markers, `gh issue
  list`) came only after the user prompted them. Root cause: kept hypothesizing fixes instead of
  isolating first.
- [project] `pkill -f 'LogosBasecamp.elf'` self-matched the agent's own shell command line and killed
  the shell mid-script (repeated silent exit 1). Root cause: `-f` matches the pkill invocation itself.
  Fix: kill by PID excluding `$$`. (→ PROJECT_KNOWLEDGE Operational gotchas)

### Skills touched
- Extracted `delivery-core-consume-crash` (basecamp-skills, integration/critical).
- Applied `logos-cpp-generator-typed-calls` (typed LogosModules) — still correct; the core-vs-ui_qml
  host is the missing caveat, captured in the new recipe's `## See also`.
