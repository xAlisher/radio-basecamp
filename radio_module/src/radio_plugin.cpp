#include "radio_plugin.h"
#include "logos_api.h"
#include <QDebug>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QNetworkInterface>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTcpSocket>
#include <QJsonArray>

// Uniform JSON return shape so the QML bridge is stable. Implemented per-issue
// (see docs/plans/radio-implementation.md); remaining methods are stubs.

namespace {
QString ok(const QString& extra = QString())
{
    return extra.isEmpty() ? QStringLiteral("{\"ok\":true}")
                           : QStringLiteral("{\"ok\":true,%1}").arg(extra);
}
QString err(const QString& code)
{
    return QStringLiteral("{\"ok\":false,\"error\":\"%1\"}").arg(code);
}
QString notImplemented(const QString& method)
{
    return QStringLiteral("{\"ok\":false,\"error\":\"not_implemented\",\"method\":\"%1\"}").arg(method);
}
} // namespace

RadioModulePlugin::RadioModulePlugin(QObject* parent) : QObject(parent)
{
    qDebug() << "RadioModulePlugin: constructed";
}

RadioModulePlugin::~RadioModulePlugin()
{
    qDebug() << "RadioModulePlugin: destroyed";
    killMediaMtx();  // never leak the origin process
}

void RadioModulePlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;  // base PluginInterface member — ModuleProxy reads this for IPC. Do NOT shadow it.
    qDebug() << "RadioModulePlugin: initLogos";
    // Issue #5: pre-init delivery_module client here (eager init; skill ipc-client-eager-init).
    emit eventResponse("initialized", QVariantList() << "radio_module" << "0.1.0");
}

QString RadioModulePlugin::ping() { return ok("\"version\":\"0.1.0\""); }

// ---------------------------------------------------------------------------
// #2 spawn + #3 mint — start/stop the MediaMTX origin and return the OBS card.
// ---------------------------------------------------------------------------

int RadioModulePlugin::port(const char* envVar, int fallback) const
{
    bool ok = false;
    const int v = qEnvironmentVariableIntValue(envVar, &ok);
    return ok && v > 0 ? v : fallback;
}

QString RadioModulePlugin::randomHex(int bytes)
{
    QByteArray b(bytes, Qt::Uninitialized);
    for (int i = 0; i < bytes; ++i)
        b[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    return QString::fromLatin1(b.toHex());
}

QString RadioModulePlugin::lanIp() const
{
    for (const QHostAddress& a : QNetworkInterface::allAddresses()) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
            return a.toString();
    }
    return QStringLiteral("127.0.0.1");
}

QString RadioModulePlugin::writeMediaMtxConfig() const
{
    // Per-stream runtime dir under temp (module is sandboxed; temp is writable).
    QFile cfg(m_runtimeDir + "/mediamtx.yml");
    if (!QDir().mkpath(m_runtimeDir) || !cfg.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();

    // `paths: all_others` is REQUIRED — an empty config rejects arbitrary paths (#2 spike).
    // v1 has no publish auth: the unguessable random path is the access control (real auth → #18).
    QTextStream s(&cfg);
    s << "rtmpAddress: :"   << port("RADIO_RTMP_PORT", 1935) << "\n"
      << "hlsAddress: :"    << port("RADIO_HLS_PORT",  8888) << "\n"
      << "webrtcAddress: :" << port("RADIO_WHIP_PORT", 8889) << "\n"
      << "srtAddress: :"    << port("RADIO_SRT_PORT",  8890) << "\n"
      << "apiAddress: :"    << port("RADIO_API_PORT",  9997) << "\n"
      << "api: yes\n"
      << "hls: yes\n"
      << "hlsVariant: lowLatency\n"
      << "webrtc: yes\n"   // WHIP ingest endpoint (OBS 30+)
      << "srt: yes\n"
      << "rtsp: no\n"
      << "paths:\n"
      << "  all_others:\n";
    return cfg.fileName();
}

bool RadioModulePlugin::spawnMediaMtx(const QString& configPath)
{
    killMediaMtx();
    const QString bin = qEnvironmentVariable("RADIO_MEDIAMTX_BIN", QStringLiteral("mediamtx"));
    m_mediamtx = new QProcess(this);
    m_mediamtx->setProcessChannelMode(QProcess::MergedChannels);
    m_mediamtx->start(bin, QStringList() << configPath);
    if (!m_mediamtx->waitForStarted(5000)) {
        qWarning() << "RadioModulePlugin: mediamtx failed to start:" << m_mediamtx->errorString();
        killMediaMtx();
        return false;
    }
    // Catch an immediate crash (e.g. port in use) — #15 surfaces this to the UI.
    if (m_mediamtx->waitForFinished(400)) {
        qWarning() << "RadioModulePlugin: mediamtx exited immediately:" << m_mediamtx->readAll();
        killMediaMtx();
        return false;
    }
    return true;
}

void RadioModulePlugin::killMediaMtx()
{
    if (!m_mediamtx) return;
    m_mediamtx->terminate();
    if (!m_mediamtx->waitForFinished(3000))
        m_mediamtx->kill();
    m_mediamtx->deleteLater();
    m_mediamtx = nullptr;
}

QString RadioModulePlugin::startStream(const QString& configJson)
{
    if (m_mediamtx) return err("already_streaming");

    const QJsonObject cfg = QJsonDocument::fromJson(configJson.toUtf8()).object();
    m_streamName  = cfg.value("name").toString();
    if (m_streamName.isEmpty()) return err("name_required");
    m_visibility  = cfg.value("visibility").toString(QStringLiteral("public"));
    m_description = cfg.value("description").toString();

    m_path       = randomHex(8);  // 16 hex chars — stream id + OBS stream key (v1)
    m_runtimeDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                   + "/radio_module/" + m_path;

    const QString configPath = writeMediaMtxConfig();
    if (configPath.isEmpty())     return err("config_write_failed");
    if (!spawnMediaMtx(configPath)) return err("mediamtx_spawn_failed");

    const QString ip = lanIp();
    const int hls = port("RADIO_HLS_PORT", 8888), whip = port("RADIO_WHIP_PORT", 8889),
              rtmp = port("RADIO_RTMP_PORT", 1935), srt = port("RADIO_SRT_PORT", 8890);

    QJsonObject card{
        {"ok", true},
        {"path", m_path},
        {"streamKey", m_path},  // OBS RTMP "Stream Key" (== path in v1)
        {"whipUrl", QStringLiteral("http://%1:%2/%3/whip").arg(ip).arg(whip).arg(m_path)},
        {"rtmpUrl", QStringLiteral("rtmp://%1:%2").arg(ip).arg(rtmp)},  // OBS RTMP "Server"
        {"srtUrl",  QStringLiteral("srt://%1:%2?streamid=publish:%3").arg(ip).arg(srt).arg(m_path)},
        {"hlsUrl",  QStringLiteral("http://%1:%2/%3/index.m3u8").arg(ip).arg(hls).arg(m_path)},
    };
    qDebug() << "RadioModulePlugin: stream started, path" << m_path;
    emit eventResponse("streamStarted", QVariantList() << m_path);
    return QString::fromUtf8(QJsonDocument(card).toJson(QJsonDocument::Compact));
}

QString RadioModulePlugin::stopStream()
{
    if (!m_mediamtx) return err("not_streaming");
    killMediaMtx();
    if (!m_runtimeDir.isEmpty()) QDir(m_runtimeDir).removeRecursively();
    qDebug() << "RadioModulePlugin: stream stopped";
    emit eventResponse("streamStopped", QVariantList() << m_path);
    m_path.clear(); m_streamName.clear(); m_lastStreamState.clear();
    return ok();
}

// ---------------------------------------------------------------------------
// #4 — poll MediaMTX for live status.
// ---------------------------------------------------------------------------

int RadioModulePlugin::httpGet(int apiPort, const QString& path, QString& bodyOut) const
{
    QTcpSocket sock;
    sock.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(apiPort));
    if (!sock.waitForConnected(800)) return -1;
    sock.write(QStringLiteral("GET %1 HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
                   .arg(path).toUtf8());
    if (!sock.waitForBytesWritten(800)) return -1;
    QByteArray raw;
    while (sock.state() == QAbstractSocket::ConnectedState && sock.waitForReadyRead(800))
        raw += sock.readAll();
    raw += sock.readAll();
    const int sep = raw.indexOf("\r\n\r\n");
    bodyOut = sep >= 0 ? QString::fromUtf8(raw.mid(sep + 4)) : QString();
    // status line: "HTTP/1.0 200 OK"
    const QByteArray status = raw.left(raw.indexOf("\r\n"));
    const auto parts = status.split(' ');
    return parts.size() >= 2 ? parts[1].toInt() : -1;
}

QString RadioModulePlugin::getStreamStatus()
{
    QString state = QStringLiteral("idle");
    if (m_mediamtx) {
        QString body;
        const int code = httpGet(port("RADIO_API_PORT", 9997),
                                 QStringLiteral("/v3/paths/get/%1").arg(m_path), body);
        if (code != 200) {
            state = QStringLiteral("waiting");  // path not created yet → OBS not connected
        } else {
            const QJsonObject p = QJsonDocument::fromJson(body.toUtf8()).object();
            const bool ready = p.value("ready").toBool();
            const bool hasSource = !p.value("source").isNull() && p.value("source").isObject();
            const bool hasTracks = !p.value("tracks").toArray().isEmpty();
            state = (ready && hasTracks) ? QStringLiteral("live")
                  : hasSource           ? QStringLiteral("receiving")
                                        : QStringLiteral("waiting");
        }
    }

    QJsonObject r{{"ok", true}, {"state", state}};
    if (m_mediamtx)
        r["hlsUrl"] = QStringLiteral("http://%1:%2/%3/index.m3u8")
                          .arg(lanIp()).arg(port("RADIO_HLS_PORT", 8888)).arg(m_path);
    if (state != m_lastStreamState) {
        m_lastStreamState = state;
        emit eventResponse("streamStatusChanged", QVariantList() << state);
    }
    return QString::fromUtf8(QJsonDocument(r).toJson(QJsonDocument::Compact));
}

QString RadioModulePlugin::startDiscovery()             { return notImplemented("startDiscovery"); }     // #5
QString RadioModulePlugin::addTopic(const QString&)     { return notImplemented("addTopic"); }           // #12
QString RadioModulePlugin::getStations()               { return notImplemented("getStations"); }         // #9
QString RadioModulePlugin::play(const QString&, const QString&) { return notImplemented("play"); }       // #9
QString RadioModulePlugin::pause()                      { return notImplemented("pause"); }              // #13
QString RadioModulePlugin::resume()                     { return notImplemented("resume"); }             // #13
QString RadioModulePlugin::stop()                       { return notImplemented("stop"); }               // #13
QString RadioModulePlugin::setVolume(int)               { return notImplemented("setVolume"); }          // #13
QString RadioModulePlugin::getPlayerStatus()            { return notImplemented("getPlayerStatus"); }    // #9
