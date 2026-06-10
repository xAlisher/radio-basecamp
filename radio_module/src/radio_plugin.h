#ifndef RADIO_MODULE_PLUGIN_H
#define RADIO_MODULE_PLUGIN_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QSet>
#include <QMap>
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
    Q_INVOKABLE QString getStreamStatus() override;
    Q_INVOKABLE QString startDiscovery() override;
    Q_INVOKABLE QString addTopic(const QString& topic) override;
    Q_INVOKABLE QString getStations() override;
    Q_INVOKABLE QString play(const QString& hlsUrl, const QString& stationName) override;
    Q_INVOKABLE QString stop() override;
    Q_INVOKABLE QString setVolume(int percent) override;
    Q_INVOKABLE QString getPlayerStatus() override;

    // Test/IPC seam (#5): decode + ingest a station announce. Called by the delivery_module
    // messageReceived handler and directly by tests/direct_test.cpp. Not part of the IPC API.
    void ingestAnnounce(const QString& base64Payload);

    // #6 host announce. buildAnnouncePayload is a pure test seam; announceOnce gates on live
    // status then publishes (called by the #10 heartbeat timer and by tests). Not IPC API.
    QString buildAnnouncePayload(int seq) const;
    QString announceOnce();
    int     announceAttemptCount() const { return m_announceAttempts; }  // #10 test seam

signals:
    void eventResponse(const QString& eventName, const QVariantList& args);

private:
    // --- Origin (#2 spawn + #3 mint) ---
    QString  writeMediaMtxConfig() const;  // returns config path, or "" on failure
    QString  spawnMediaMtx(const QString& configPath);  // "" ok, else an error code (#15)
    void     killMediaMtx();
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
    void     killPlayer();                 // stop + reap the ffplay process (#9)
    QString  startFfplay();                // (re)launch ffplay on m_playingUrl; "" ok else error code

    // NB: do NOT declare a LogosAPI* member — initLogos must set the base PluginInterface::logosAPI,
    // which ModuleProxy reads for cross-module IPC (skills: logosapi-member-no-redeclare, initlogos-no-override).

    QProcess* m_mediamtx = nullptr;
    QString   m_streamName, m_visibility, m_description;
    QString   m_path;        // random MediaMTX path = OBS stream key (v1; real publish auth → #18)
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
    bool            m_discovering = false;
    QSet<QString>   m_subscribedTopics;
    QMap<QString, QJsonObject> m_stations;  // keyed by path; value carries "_lastSeen" ms (TTL → #11)

    // Listener playback (#9) — ffplay subprocess (Qt Multimedia absent; skill ffplay-subprocess-player).
    QProcess* m_player = nullptr;
    QString   m_playingStation, m_playingUrl;
    int       m_volume = 75;      // #13 0–100; applied via ffplay -volume
};

#endif // RADIO_MODULE_PLUGIN_H
