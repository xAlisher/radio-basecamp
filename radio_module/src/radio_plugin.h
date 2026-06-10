#ifndef RADIO_MODULE_PLUGIN_H
#define RADIO_MODULE_PLUGIN_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QSet>
#include <QMap>
#include <QPair>
#include <QJsonObject>
#include <QTimer>
#include "radio_interface.h"

class LogosAPI;
class LogosAPIClient;
class LogosObject;
class QProcess;

/**
 * @brief radio_module plugin. See radio_interface.h for the API contract.
 *
 * v0.1.0 scaffold: method bodies are stubs returning {"ok":false,"error":"not_implemented"}.
 * Each issue in docs/plans/radio-implementation.md fills one in.
 */
class RadioModulePlugin : public QObject, public RadioModuleInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID RadioModuleInterface_iid FILE "metadata.json")
    Q_INTERFACES(RadioModuleInterface PluginInterface)

public:
    explicit RadioModulePlugin(QObject* parent = nullptr);
    ~RadioModulePlugin() override;

    // PluginInterface
    QString name() const override { return "radio_module"; }
    QString version() const override { return "0.1.0"; }
    // initLogos is NOT a PluginInterface virtual (it's a commented-out TODO in interface.h);
    // the host calls it via the meta-object system, so declare it Q_INVOKABLE, not override.
    Q_INVOKABLE void initLogos(LogosAPI* api);

    // RadioModuleInterface
    Q_INVOKABLE QString ping() override;
    Q_INVOKABLE QString startStream(const QString& configJson) override;
    Q_INVOKABLE QString stopStream() override;
    Q_INVOKABLE QString regenerateKey() override;
    Q_INVOKABLE QString regenerateOnion() override;
    Q_INVOKABLE QString getStreamStatus() override;
    Q_INVOKABLE QString getStreamCard() override;
    Q_INVOKABLE QString getDeliveryStatus() override;
    Q_INVOKABLE QString startDiscovery() override;
    Q_INVOKABLE QString addTopic(const QString& topic) override;
    Q_INVOKABLE QString getStations() override;
    Q_INVOKABLE QString play(const QString& hlsUrl, const QString& stationName) override;
    Q_INVOKABLE QString stop() override;
    Q_INVOKABLE QString setVolume(int percent) override;
    Q_INVOKABLE QString setListenBuffer(int seconds) override;
    Q_INVOKABLE QString getPlayerStatus() override;

    // Test/IPC seam (#5): decode + ingest a station announce. Called by the delivery_module
    // messageReceived handler and directly by tests/direct_test.cpp. Not part of the IPC API.
    void ingestAnnounce(const QString& base64Payload);

    // #6 host announce. buildAnnouncePayload is a pure test seam; announceOnce gates on live
    // status then publishes (called by the #10 heartbeat timer and by tests). Not IPC API.
    QString buildAnnouncePayload(int seq) const;
    QString announceOnce();
    int     announceAttemptCount() const { return m_announceAttempts; }  // #10 test seam

    // Tor onion mode test seams (epic: hide streamer IP). Configure onion state without spawning
    // tor, and inspect ffplay command routing. Not IPC API.
    void        configureOnionForTest(const QString& onion);          // privacy=onion + m_onion
    QStringList playerCommandForTest(const QString& url) const;       // [program, args…]; torsocks for .onion

signals:
    void eventResponse(const QString& eventName, const QVariantList& args);

private:
    // --- Origin (#2 spawn + #3 mint) ---
    QString  writeMediaMtxConfig() const;  // returns config path, or "" on failure
    QString  spawnMediaMtx(const QString& configPath);  // "" ok, else an error code (#15)
    void     killMediaMtx();
    // #11 — OBS card builder + stream-state persistence (survives a Basecamp restart)
    QJsonObject buildCard() const;
    QString  stateFile() const;
    QString  persistentHsDir() const;  // #17 persistent Tor HiddenServiceDir → stable .onion
    void     saveStreamState() const;
    void     clearStreamState() const;
    void     resumeStreamIfPersisted();
    QString  lanIp() const;                // first non-loopback IPv4, else 127.0.0.1
    static QString randomHex(int bytes);
    int      port(const char* envVar, int fallback) const;
    // Synchronous localhost GET to the MediaMTX API (QTcpSocket — no event-loop reentrancy).
    // Returns HTTP status code (-1 on connect/read failure); body in bodyOut.
    int      httpGet(int apiPort, const QString& path, QString& bodyOut) const;
    QString  streamState();                // #4 poll, shared by getStreamStatus + announce gating
    // --- Discovery (#5/#6) ---
    QString  directoryTopic() const;       // well-known public directory topic
    bool     subscribeTopic(const QString& topic);
    bool     ensureDeliveryNode();         // idempotent delivery_module getClient + createNode + start
    void     checkDeliveryHealth();        // poll delivery_module node reachability for the status pill
    void     killPlayer();                 // stop + reap the ffplay process (#9)
    QString  startFfplay();                // (re)launch ffplay on m_playingUrl; "" ok else error code
    // --- Tor onion mode (epic: hide the streamer's IP) ---
    // Two independent tor processes so host (HiddenService) and listener (SOCKS) lifecycles can't
    // tear each other down (Senty ISSUE-2). Host runs SocksPort 0; listener owns the SOCKS port.
    QString  ensureTorHost();    // hidden service for MediaMTX; "" ok else an error code
    QString  ensureTorListen();  // SOCKS proxy for .onion playback; "" ok else an error code
    // Shared spawn: write torrc into dir (0700), start, verify it didn't immediately die (ISSUE-3).
    bool     startTorProc(QProcess*& proc, const QString& dir, const QString& cfg, QString& errOut);
    void     killTorHost();
    void     killTorListen();
    void     pollOnionStatus();  // read .onion hostname + detect descriptor publish (bounded)
    // ffplay invocation, routed through torsocks for .onion hosts. Returns {program, args}.
    QPair<QString, QStringList> buildPlayerCommand(const QString& url) const;
    int      torSocksPort() const { return port("RADIO_TOR_SOCKS_PORT", 9050); }

    // NB: do NOT declare a LogosAPI* member — initLogos must set the base PluginInterface::logosAPI,
    // which ModuleProxy reads for cross-module IPC (skills: logosapi-member-no-redeclare, initlogos-no-override).

    QProcess* m_mediamtx = nullptr;
    QString   m_streamName, m_visibility, m_description;
    QString   m_path;        // public stream id (in HLS URL + announce) — NOT a secret
    QString   m_streamKey;   // #18 secret publish credential (MediaMTX auth); never announced
    QString   m_runtimeDir;  // per-stream temp dir holding mediamtx.yml
    QString   m_lastStreamState;  // for streamStatusChanged edge detection (#4)
    // Host announce (#6) + heartbeat (#10)
    qint64    m_startedAt = 0;
    int       m_announceSeq = 0;
    int       m_announceAttempts = 0;
    QString   m_announceTopic, m_hostLabel;
    QTimer    m_heartbeat;

    // Discovery (#5)
    LogosAPIClient* m_delivery = nullptr;
    LogosObject*    m_deliveryObj = nullptr;
    bool            m_deliveryNodeUp = false;
    bool            m_deliveryReachable = false;  // node answered getNodeInfo → pill green
    bool            m_discovering = false;
    QString         m_deliveryPeerId;
    QTimer          m_deliveryHealth;   // periodic delivery_module reachability check
    QSet<QString>   m_subscribedTopics;
    QMap<QString, QJsonObject> m_stations;  // keyed by path; value carries "_lastSeen" ms (TTL → #11)

    // Listener playback (#9) — ffplay subprocess (Qt Multimedia absent; skill ffplay-subprocess-player).
    QProcess* m_player = nullptr;
    QString   m_playingStation, m_playingUrl;
    int       m_volume = 75;      // #13 0–100; applied via ffplay -volume
    int       m_listenBufferSec = 8;  // #17 listener jitter buffer (ffplay -live_start_index/-infbuf)

    // Tor onion mode — separate host (HiddenService) + listener (SOCKS) processes (Senty ISSUE-2)
    QProcess* m_torHost = nullptr;     // SocksPort 0 + HiddenService (hosting in onion mode)
    QProcess* m_torListen = nullptr;   // SocksPort (playing a .onion)
    int       m_listenSocksPort = 0;   // the SOCKS port the listener tor actually bound (retry-picked)
    QString   m_privacy = QStringLiteral("public");  // "public" | "onion"
    QString   m_onion;            // .onion host once the hidden-service hostname is known
    QString   m_torHostDir;       // host tor DataDirectory + HiddenServiceDir root (temp)
    QString   m_torListenDir;     // listener tor DataDirectory (temp)
    bool      m_onionReady = false;  // descriptor published → reachable by listeners
    QString   m_onionError;          // non-empty → onion setup failed/timed out (surfaced to the UI)
    int       m_onionPollTicks = 0;  // bounded descriptor-publish poll (Senty ISSUE-3)
    int       m_onionBootstrapTick = 0;  // tick when tor hit 100% — grace fallback for readiness
    QTimer    m_onionPublishPoll;    // polls tor.log for the descriptor upload
};

#endif // RADIO_MODULE_PLUGIN_H
