#ifndef RADIO_MODULE_INTERFACE_H
#define RADIO_MODULE_INTERFACE_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include "interface.h"

/**
 * @brief radio_module — decentralized audio broadcast (origin + discovery).
 *
 * All real work lives here (the QML UI is sandboxed and cannot do network/subprocess I/O):
 *   - Origin:    spawn/manage MediaMTX via QProcess, mint ingest URLs, poll HLS status.
 *   - Discovery: announce/subscribe over `delivery_module` (LogosMessaging), heartbeat + TTL.
 *   - Playback:  spawn/control `ffplay` for HLS .m3u8 (audio-first v1).
 *
 * Every method returns a JSON string (so the QML bridge gets a stable shape). Async results
 * and live changes are delivered via `eventResponse(eventName, args)`.
 *
 * API contract — implementation tracked in docs/plans/radio-implementation.md (issues #2–#13).
 */
class RadioModuleInterface : public PluginInterface
{
public:
    virtual ~RadioModuleInterface() = default;

    // --- Health / scaffold (#1) ---
    /** Liveness check. @return {"ok":true,"version":"0.1.0"} */
    Q_INVOKABLE virtual QString ping() = 0;

    // --- Stream / origin (host side) — Epic B/C/D ---
    /** Start the origin + mint ingest. @param configJson {name, visibility:"public"|"private", description?}
     *  @return {ok, path, whipUrl, rtmpUrl, srtUrl, streamKey} (#2 #3) */
    Q_INVOKABLE virtual QString startStream(const QString& configJson) = 0;
    /** Stop origin + announce. @return {ok} (#2) */
    Q_INVOKABLE virtual QString stopStream() = 0;
    /** Rotate the secret publish key (#17). Rewrites MediaMTX auth + restarts it; the public path /
     *  .onion are unchanged (listeners unaffected), OBS must re-enter the new key. @return new card. */
    Q_INVOKABLE virtual QString regenerateKey() = 0;
    /** Rotate the Tor hidden-service identity → a NEW .onion (#17). Listeners rediscover via the
     *  heartbeat. Onion mode only. @return {ok}. */
    Q_INVOKABLE virtual QString regenerateOnion() = 0;
    /** Poll MediaMTX. @return {state:"idle"|"waiting"|"receiving"|"live", hlsUrl, privacy, onion, onionReady} (#4).
     *  Also emits `streamStatusChanged`. */
    Q_INVOKABLE virtual QString getStreamStatus() = 0;
    /** The OBS ingest card for the active stream, so the UI rehydrates after a restart (#11).
     *  @return the same shape as startStream, or {ok:false,error:"not_streaming"}. */
    Q_INVOKABLE virtual QString getStreamCard() = 0;

    // --- Discovery — Epic C/F ---
    /** delivery_module node status for the header pill. @return {ok, state:"offline"|"ready"|"connected", peerId} */
    Q_INVOKABLE virtual QString getDeliveryStatus() = 0;
    /** Subscribe to the well-known directory topic and start collecting heartbeats (#5). @return {ok} */
    Q_INVOKABLE virtual QString startDiscovery() = 0;
    /** Subscribe to an additional (private/unlisted) topic (#12). @return {ok} */
    Q_INVOKABLE virtual QString addTopic(const QString& topic) = 0;
    /** Live stations after TTL pruning (#9 #11). @return {ok, stations:[{name,host,streamUrl,uptime,topic}]} */
    Q_INVOKABLE virtual QString getStations() = 0;

    // --- Playback (listener side) — Epic E/G ---
    /** Play an HLS .m3u8 via ffplay (#9). @return {ok} */
    Q_INVOKABLE virtual QString play(const QString& hlsUrl, const QString& stationName) = 0;
    Q_INVOKABLE virtual QString stop() = 0;             // #9
    // No pause for live radio — pausing a live stream is just stop (the live edge moves on and
    // MediaMTX rotates the HLS segments away). Controls are Play / Stop / Volume.
    Q_INVOKABLE virtual QString setVolume(int percent) = 0; // #13
    /** Listener jitter buffer in seconds (#17). Deeper = smoother over Tor, higher start latency.
     *  Re-applies to the live player immediately. @return {ok, bufferSec} */
    Q_INVOKABLE virtual QString setListenBuffer(int seconds) = 0;
    /** @return {state:"stopped"|"playing", station, volume, bufferSec} (#9 #13 #17) */
    Q_INVOKABLE virtual QString getPlayerStatus() = 0;
};

#define RadioModuleInterface_iid "org.logos.RadioModuleInterface"
Q_DECLARE_INTERFACE(RadioModuleInterface, RadioModuleInterface_iid)

#endif // RADIO_MODULE_INTERFACE_H
