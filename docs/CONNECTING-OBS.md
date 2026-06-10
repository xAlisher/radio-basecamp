# Connecting OBS to radio-basecamp

radio-basecamp never captures your mic or screen — **OBS does the capture and pushes the stream
to the module's built-in origin (MediaMTX)**. You point OBS at the URL the module gives you, hit
*Start Streaming* in OBS, and your station goes live for listeners to discover.

> v1 is **audio-first**: listeners hear your stream. You can still send video from OBS (it's carried
> by the origin), but the v1 listener plays audio only.

---

## 1. Start the origin in the module

1. Open **radio** in Logos Basecamp → **Stream** tab.
2. Type a **station name** (what listeners see) and pick **Public** (announced to the directory)
   or **Private** (announced on a per-stream topic you share out of band).
3. Press **Start**. The module launches the origin and shows an **OBS setup card** with three values:

   | Field | Example | Use |
   |-------|---------|-----|
   | **WHIP URL** | `http://192.168.1.50:8889/ab12…/whip` | Preferred — OBS 30+ WebRTC output, sub-second |
   | **RTMP Server** + **Stream Key** | `rtmp://192.168.1.50:1935` + `ab12…` | Broadest compatibility, ~2–5 s |
   | **SRT URL** | `srt://192.168.1.50:8890?streamid=publish:ab12…` | Lossy / rural uplinks |

   Use the **Copy** buttons — don't retype these.

The status light shows **Waiting for OBS…** until OBS connects.

---

## 2. Point OBS at it — pick ONE ingest

### Option A — WHIP (recommended, OBS 30+)

1. OBS → **Settings → Stream**.
2. **Service:** `WHIP`.
3. **Server:** paste the **WHIP URL** from the card.
4. **Bearer Token:** leave blank (v1 has no publish auth).
5. **OK** → **Start Streaming**.

Sub-second glass-to-origin. Best choice if your OBS is version 30 or newer.

### Option B — RTMP (works everywhere)

1. OBS → **Settings → Stream**.
2. **Service:** `Custom…`.
3. **Server:** paste the **RTMP Server** (`rtmp://<host>:1935`).
4. **Stream Key:** paste the **Stream Key**.
5. **OK** → **Start Streaming**.

~2–5 s latency, no plugins, universally supported.

### Option C — SRT (lossy uplinks)

1. OBS → **Settings → Stream** → **Service:** `Custom…`.
2. **Server:** paste the whole **SRT URL** (it already contains the stream id). Leave **Stream Key** empty.
3. **OK** → **Start Streaming**.

Survives packet loss far better than RTMP — good for rural / mobile links.

---

## 3. Recommended OBS output settings (audio-first)

OBS → **Settings → Output** (set *Output Mode: Advanced*) and **Settings → Audio**:

- **Audio Bitrate:** 128–160 kbps (this is what listeners hear — don't skimp here).
- **Keyframe Interval:** **2 s** (required for clean HLS segmenting; 0/auto can cause stutter).
- **Video Bitrate:** 800–2500 kbps is plenty (video is carried but not shown to v1 listeners).
- **Encoder:** x264 `veryfast` (or hardware NVENC/QSV), **Profile:** `main`, **Tune:** `zerolatency` for WHIP/RTMP.
- **Sample Rate:** 48 kHz.

---

## 4. Confirm you're live

Back in the **Stream** tab, the status light moves:

```
Waiting for OBS…  →  Receiving stream…  →  🔴 Live (announcing)
```

Once it's **🔴 Live**, the module begins announcing your station over LogosMessaging
(every 15 s). Listeners on the directory topic (or your private topic) see it appear and can tap
to play. Press **Stop** to end the broadcast and the announce.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Status stuck on **Waiting for OBS…** | OBS isn't connected. Re-check Service/Server/Key; confirm OBS says "Streaming". |
| OBS "Failed to connect to server" | Wrong URL/port, or a firewall. The host and OBS must reach each other; if OBS is on another machine, use the LAN IP shown (not `127.0.0.1`). |
| Listeners on another network can't connect | v1 serves from your origin directly — listeners must be able to reach the host's IP/ports. NAT/relay is Phase 2. |
| Audio only, no video for listeners | Expected in v1 (audio-first). Video display is a later phase. |
| Private stream not visible | Private streams announce on `/radio-basecamp/1/<path>/json`; the listener must **+ Add topic** with that string (share it out of band). |

Ports used by the origin: **1935** (RTMP in), **8889** (WHIP/WebRTC in), **8890** (SRT in),
**8888** (HLS out to listeners), **9997** (local status API). They must be free on the host.
