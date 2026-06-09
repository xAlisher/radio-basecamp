#ifndef RADIO_MODULE_PLUGIN_H
#define RADIO_MODULE_PLUGIN_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include "radio_interface.h"

class LogosAPI;
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
    Q_INVOKABLE QString pause() override;
    Q_INVOKABLE QString resume() override;
    Q_INVOKABLE QString stop() override;
    Q_INVOKABLE QString setVolume(int percent) override;
    Q_INVOKABLE QString getPlayerStatus() override;

signals:
    void eventResponse(const QString& eventName, const QVariantList& args);

private:
    // --- Origin (#2 spawn + #3 mint) ---
    QString  writeMediaMtxConfig() const;  // returns config path, or "" on failure
    bool     spawnMediaMtx(const QString& configPath);
    void     killMediaMtx();
    QString  lanIp() const;                // first non-loopback IPv4, else 127.0.0.1
    static QString randomHex(int bytes);
    int      port(const char* envVar, int fallback) const;

    LogosAPI* m_logosAPI = nullptr;

    QProcess* m_mediamtx = nullptr;
    QString   m_streamName, m_visibility, m_description;
    QString   m_path;        // random MediaMTX path = OBS stream key (v1; real publish auth → #18)
    QString   m_runtimeDir;  // per-stream temp dir holding mediamtx.yml
    // Issue #9: PlayerManager; Issue #5: delivery client + station cache.
};

#endif // RADIO_MODULE_PLUGIN_H
