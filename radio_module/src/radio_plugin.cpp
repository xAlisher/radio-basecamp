#include "radio_plugin.h"
#include "logos_api.h"
#include "logos_api_client.h"
#include "logos_object.h"
#include <QDebug>
#include <QProcess>
#include <QProcessEnvironment>
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
#include <QSysInfo>
#include <QUrl>
#include <QRegularExpression>
#include <QFileInfo>
#include <csignal>
#include <sys/prctl.h>
#include <dlfcn.h>

namespace {
// Make a spawned child receive SIGKILL if THIS process (logos_host) dies — otherwise a
// kill -9 of the module (e.g. during relaunch) orphans mediamtx/ffplay, leaking the ports.
void dieWithParent(QProcess* p) { p->setChildProcessModifier([]{ ::prctl(PR_SET_PDEATHSIG, SIGKILL); }); }

// Canonical .onion test (Senty FINDING-1): QUrl::host() is lowercased, but strip a trailing FQDN
// dot too — otherwise "Abc.onion." dodges endsWith(".onion") and would play OUTSIDE Tor → IP leak.
bool isOnionUrl(const QString& url) {
    QString h = QUrl(url).host().toLower();
    while (h.endsWith(QLatin1Char('.'))) h.chop(1);
    return h.endsWith(QLatin1String(".onion"));
}

// Directory of this plugin .so (so the module can find binaries bundled alongside it).
QString moduleDir() {
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&isOnionUrl), &info) && info.dli_fname)
        return QFileInfo(QString::fromUtf8(info.dli_fname)).absolutePath();
    return QString();
}
// Resolve a runtime helper binary: explicit env override → bundled (next to the .so, in bin/ or the
// module dir) → bare name on PATH. Lets the .lgx ship self-contained without a system install.
QString resolveBin(const QString& name, const char* envVar) {
    const QString env = qEnvironmentVariable(envVar);
    if (!env.isEmpty()) return env;
    const QString d = moduleDir();
    if (!d.isEmpty())
        for (const QString& c : { d + "/bin/" + name, d + "/" + name })
            if (QFileInfo(c).isExecutable()) return c;
    return name;  // fall back to PATH
}
// Environment for spawned SYSTEM binaries (tor/mediamtx/ffplay). The Basecamp AppImage exports
// LD_LIBRARY_PATH/LD_PRELOAD pointing at its bundled libs; a child like the apt `tor` then loads the
// wrong libevent and dies ("undefined symbol: evutil_secure_rng_add_bytes"). Drop them so system
// binaries resolve against the system ld cache; nix binaries use their own $ORIGIN/rpath regardless.
QProcessEnvironment cleanSpawnEnv() {
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    e.remove(QStringLiteral("LD_LIBRARY_PATH"));
    e.remove(QStringLiteral("LD_PRELOAD"));
    return e;
}
}

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
    // #10 heartbeat: re-announce on a fixed interval while streaming.
    connect(&m_heartbeat, &QTimer::timeout, this, [this]{ announceOnce(); });
    // Status pill: poll delivery_module reachability.
    connect(&m_deliveryHealth, &QTimer::timeout, this, [this]{ checkDeliveryHealth(); });
    // Onion mode: poll for the hidden-service hostname + descriptor publish.
    connect(&m_onionPublishPoll, &QTimer::timeout, this, [this]{ pollOnionStatus(); });
}

RadioModulePlugin::~RadioModulePlugin()
{
    qDebug() << "RadioModulePlugin: destroyed";
    killMediaMtx();  // never leak the origin process
    killPlayer();
    killTorHost();
    killTorListen();
}

void RadioModulePlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;  // base PluginInterface member — ModuleProxy reads this for IPC. Do NOT shadow it.
    qDebug() << "RadioModulePlugin: initLogos";
    // Start the delivery-health poll deferred (skill ipc-client-eager-init: don't getClient in initLogos directly).
    QTimer::singleShot(2500, this, [this]{ checkDeliveryHealth(); m_deliveryHealth.start(5000); });
    // #11 — if a stream was active before a restart, re-spawn its origin with the same path/key.
    QTimer::singleShot(1500, this, [this]{ resumeStreamIfPersisted(); });
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
    // #18 auth: HLS read is public; publishing requires the secret key; the local API is
    // localhost-only (verified 2026-06-10). This stops anyone on the topic hijacking the stream.
    QTextStream s(&cfg);
    s << "rtmpAddress: :"   << port("RADIO_RTMP_PORT", 1935) << "\n"
      << "hlsAddress: :"    << port("RADIO_HLS_PORT",  8888) << "\n"
      << "webrtcAddress: :" << port("RADIO_WHIP_PORT", 8889) << "\n"
      << "srtAddress: :"    << port("RADIO_SRT_PORT",  8890) << "\n"
      << "apiAddress: :"    << port("RADIO_API_PORT",  9997) << "\n"
      << "api: yes\n"
      << "hls: yes\n"
      // #17 mpegts (not lowLatency) so listeners fetch whole segments and can buffer over high-RTT
      // Tor; a deep playlist (24 × 1s) lets the listener start up to ~20s behind live to absorb jitter.
      << "hlsVariant: mpegts\n"
      << "hlsSegmentCount: 24\n"
      << "hlsSegmentDuration: 1s\n"
      << "webrtc: yes\n"   // WHIP ingest endpoint (OBS 30+)
      << "srt: yes\n"
      << "rtsp: no\n"
      << "authInternalUsers:\n"
      << "- user: any\n"
      << "  permissions:\n"
      << "  - action: read\n"          // public HLS playback for listeners
      << "- user: any\n"
      << "  ips: ['127.0.0.1', '::1']\n"
      << "  permissions:\n"
      << "  - action: api\n"           // local status polling only
      << "- user: publisher\n"
      << "  pass: " << m_streamKey << "\n"
      << "  permissions:\n"
      << "  - action: publish\n"       // OBS must present the secret key
      << "paths:\n"
      << "  all_others:\n";
    return cfg.fileName();
}

QString RadioModulePlugin::spawnMediaMtx(const QString& configPath)
{
    killMediaMtx();
    const QString bin = resolveBin(QStringLiteral("mediamtx"), "RADIO_MEDIAMTX_BIN");
    m_mediamtx = new QProcess(this);
    m_mediamtx->setProcessChannelMode(QProcess::MergedChannels);
    m_mediamtx->setProcessEnvironment(cleanSpawnEnv());  // system binary — not the AppImage's libs
    dieWithParent(m_mediamtx);   // #15/ops: don't orphan + leak ports on kill -9
    m_mediamtx->start(bin, QStringList() << configPath);
    if (!m_mediamtx->waitForStarted(5000)) {
        const bool notFound = m_mediamtx->error() == QProcess::FailedToStart;
        qWarning() << "RadioModulePlugin: mediamtx failed to start:" << m_mediamtx->errorString();
        killMediaMtx();
        return notFound ? QStringLiteral("mediamtx_not_found") : QStringLiteral("mediamtx_spawn_failed");
    }
    // Immediate exit ⇒ bad config or a port already in use (#15 surfaces this to the UI).
    if (m_mediamtx->waitForFinished(400)) {
        qWarning() << "RadioModulePlugin: mediamtx exited immediately:" << m_mediamtx->readAll();
        killMediaMtx();
        return QStringLiteral("mediamtx_port_or_config");
    }
    return QString();
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
    // Privacy mode (epic: hide streamer IP). Default ONION — internet radio shouldn't be LAN-only and
    // shouldn't leak the host IP; "direct" is the opt-in for local/low-latency use. "onion" → announce
    // a .onion URL (and reach listeners through NAT without port-forwarding) instead of lanIp().
    m_privacy     = cfg.value("privacy").toString(QStringLiteral("onion"));

    // #17 — reuse the persisted publish identity so the stream key/path stay STABLE across restarts
    // (recoverable even if auto-resume raced). Mint fresh only when none exists; "⟳ New" rotates it.
    m_path.clear(); m_streamKey.clear();
    {
        QFile in(stateFile());
        if (in.open(QIODevice::ReadOnly)) {
            const QJsonObject st = QJsonDocument::fromJson(in.readAll()).object(); in.close();
            m_path      = st.value("path").toString();
            m_streamKey = st.value("streamKey").toString();
        }
    }
    if (m_path.isEmpty())      m_path      = randomHex(8);   // 64-bit public stream id
    if (m_streamKey.isEmpty()) m_streamKey = randomHex(16);  // 128-bit secret publish credential (#18)
    m_runtimeDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                   + "/radio_module/" + m_path;
    m_startedAt   = QDateTime::currentMSecsSinceEpoch();
    m_announceSeq = 0;
    // Onion mode must not deanonymize via the machine hostname (Senty FINDING-3) — it ships in
    // every announce and shows in the listener UI. Use a neutral label.
    m_hostLabel   = (m_privacy == "onion") ? QStringLiteral("anonymous") : QSysInfo::machineHostName();
    // Public → directory topic; private → unguessable per-stream topic (shared out-of-band).
    m_announceTopic = (m_visibility == "private")
                      ? QStringLiteral("/radio-basecamp/1/%1/json").arg(m_path)
                      : directoryTopic();

    const QString configPath = writeMediaMtxConfig();
    if (configPath.isEmpty()) return err("config_write_failed");
    // On any failure past here, scrub the runtime dir — mediamtx.yml holds the secret publish
    // password and must not linger on disk after a failed start (Senty round-4 MEDIUM).
    auto abort = [&](const QString& code) { killMediaMtx();
        if (!m_runtimeDir.isEmpty()) QDir(m_runtimeDir).removeRecursively();
        return err(code); };
    const QString spawnErr = spawnMediaMtx(configPath);
    if (!spawnErr.isEmpty()) return abort(spawnErr);

    // Onion mode: bring up a tor hidden service for the HLS port. The .onion + readiness arrive
    // asynchronously (descriptor publish ~30-60s); announceOnce gates on it so no IP is ever sent.
    if (m_privacy == "onion") {
        m_onion.clear(); m_onionReady = false;
        const QString te = ensureTorHost();
        if (!te.isEmpty()) return abort(te);
    }

    m_heartbeat.start(port("RADIO_HEARTBEAT_MS", 15000));  // #10 re-announce while live
    saveStreamState(true);  // #11 persist identity so the stream survives a Basecamp restart
    qDebug() << "RadioModulePlugin: stream started, path" << m_path;
    emit eventResponse("streamStarted", QVariantList() << m_path);
    return QString::fromUtf8(QJsonDocument(buildCard()).toJson(QJsonDocument::Compact));
}

// The OBS ingest card — derived from m_path/m_streamKey/ports, so it can be rebuilt after a restart.
QJsonObject RadioModulePlugin::buildCard() const
{
    // Onion mode: OBS is local to MediaMTX, so the ingest card uses loopback — never expose lanIp()
    // on any surface, including the host's own card (Senty FINDING-2).
    const QString ip = (m_privacy == "onion") ? QStringLiteral("127.0.0.1") : lanIp();
    const int hls = port("RADIO_HLS_PORT", 8888), whip = port("RADIO_WHIP_PORT", 8889),
              rtmp = port("RADIO_RTMP_PORT", 1935), srt = port("RADIO_SRT_PORT", 8890);
    const QString auth = QStringLiteral("user=publisher&pass=%1").arg(m_streamKey);
    return QJsonObject{
        {"ok", true}, {"path", m_path},
        {"streamKey", QStringLiteral("%1?%2").arg(m_path, auth)},  // OBS RTMP "Stream Key" (path + auth)
        {"whipUrl", QStringLiteral("http://%1:%2/%3/whip?%4").arg(ip).arg(whip).arg(m_path).arg(auth)},
        {"rtmpUrl", QStringLiteral("rtmp://%1:%2").arg(ip).arg(rtmp)},  // OBS RTMP "Server"
        {"srtUrl",  QStringLiteral("srt://%1:%2?streamid=publish:%3:publisher:%4").arg(ip).arg(srt).arg(m_path).arg(m_streamKey)},
        {"hlsUrl",  QStringLiteral("http://%1:%2/%3/index.m3u8").arg(ip).arg(hls).arg(m_path)},
        {"name", m_streamName}, {"description", m_description}, {"privacy", m_privacy},
    };
}

// #11 — the UI rehydrates its OBS card from this after a restart (when a stream auto-resumed).
QString RadioModulePlugin::getStreamCard()
{
    if (!m_mediamtx) return err("not_streaming");
    return QString::fromUtf8(QJsonDocument(buildCard()).toJson(QJsonDocument::Compact));
}

// #17 — rotate the secret publish key. Path/.onion are unchanged (listeners keep playing); MediaMTX
// restarts with the new auth so the old OBS key stops working until re-entered.
QString RadioModulePlugin::regenerateKey()
{
    if (!m_mediamtx) return err("not_streaming");
    m_streamKey = randomHex(16);
    killMediaMtx();
    const QString cfgPath = writeMediaMtxConfig();
    if (cfgPath.isEmpty()) return err("config_write_failed");
    const QString se = spawnMediaMtx(cfgPath);
    if (!se.isEmpty()) return err(se);
    saveStreamState(true);
    emit eventResponse("streamKeyRotated", QVariantList() << m_path);
    return QString::fromUtf8(QJsonDocument(buildCard()).toJson(QJsonDocument::Compact));
}

// #17 — rotate the Tor hidden-service identity to a brand-new .onion. Wipes the persistent HS keys
// and restarts the host tor; the new address arrives async and is re-announced via the heartbeat.
QString RadioModulePlugin::regenerateOnion()
{
    if (!m_mediamtx) return err("not_streaming");
    if (m_privacy != QStringLiteral("onion")) return err("not_onion");
    killTorHost();
    QDir(persistentHsDir()).removeRecursively();
    const QString e = ensureTorHost();
    if (!e.isEmpty()) return err(e);
    emit eventResponse("onionRotated", QVariantList() << m_path);
    return ok();
}

// --- #11 stream-state persistence (survives a Basecamp restart) ---
QString RadioModulePlugin::stateFile() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + "/radio_module/station.json";  // per-profile (respects XDG_DATA_HOME)
}

QString RadioModulePlugin::persistentHsDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + "/radio_module/hs";  // #17 stable Tor hidden-service keys (per-profile)
}

void RadioModulePlugin::saveStreamState(bool running) const
{
    const QJsonObject st{
        {"name", m_streamName}, {"visibility", m_visibility}, {"description", m_description},
        {"privacy", m_privacy}, {"path", m_path}, {"streamKey", m_streamKey},
        {"startedAt", m_startedAt}, {"announceTopic", m_announceTopic}, {"hostLabel", m_hostLabel},
        {"running", running}  // #17 false → keep the identity (key) but don't auto-resume on restart
    };
    const QString f = stateFile();
    QDir().mkpath(QFileInfo(f).absolutePath());
    QFile out(f);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out.write(QJsonDocument(st).toJson(QJsonDocument::Compact)); out.close();
    }
}

void RadioModulePlugin::clearStreamState() const { QFile::remove(stateFile()); }

void RadioModulePlugin::resumeStreamIfPersisted()
{
    if (m_mediamtx) return;
    QFile in(stateFile());
    if (!in.open(QIODevice::ReadOnly)) return;
    const QJsonObject st = QJsonDocument::fromJson(in.readAll()).object(); in.close();
    const QString path = st.value("path").toString();
    if (path.isEmpty()) return;
    // #17 — a deliberately-stopped station keeps its identity (key) for reuse, but must NOT auto-resume.
    if (!st.value("running").toBool(true)) return;  // legacy files (no flag) default to running
    m_streamName    = st.value("name").toString();
    m_visibility    = st.value("visibility").toString(QStringLiteral("public"));
    m_description   = st.value("description").toString();
    m_privacy       = st.value("privacy").toString(QStringLiteral("onion"));
    m_path          = path;
    m_streamKey     = st.value("streamKey").toString();
    m_startedAt     = st.value("startedAt").toVariant().toLongLong();
    m_announceTopic = st.value("announceTopic").toString();
    m_hostLabel     = st.value("hostLabel").toString(QStringLiteral("anonymous"));
    m_runtimeDir    = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/radio_module/" + m_path;
    m_announceSeq   = 0;
    // Re-spawn the origin with the SAME path + key so OBS reconnects without reconfiguration.
    const QString configPath = writeMediaMtxConfig();
    if (configPath.isEmpty() || !spawnMediaMtx(configPath).isEmpty()) {
        if (!m_runtimeDir.isEmpty()) QDir(m_runtimeDir).removeRecursively();
        // #17 — KEEP station.json on a transient spawn failure (e.g. ports busy right after restart)
        // so the stream key is never silently lost; a later restart resumes with the same identity.
        m_path.clear(); m_streamKey.clear();
        qWarning() << "RadioModulePlugin: resume spawn failed; persisted profile kept for retry";
        return;
    }
    if (m_privacy == "onion") { m_onion.clear(); m_onionReady = false; ensureTorHost(); }
    m_heartbeat.start(port("RADIO_HEARTBEAT_MS", 15000));
    qDebug() << "RadioModulePlugin: resumed persisted stream" << m_path;
    emit eventResponse("streamStarted", QVariantList() << m_path);
}

QString RadioModulePlugin::stopStream()
{
    if (!m_mediamtx) return err("not_streaming");
    m_heartbeat.stop();
    // #14: tell listeners we're going offline so they drop us at once (not after the 45s TTL).
    if (!m_path.isEmpty() && !m_announceTopic.isEmpty() && ensureDeliveryNode()) {
        const QString bye = QString::fromUtf8(QJsonDocument(QJsonObject{
            {"v", 1}, {"type", "offline"}, {"path", m_path}}).toJson(QJsonDocument::Compact));
        m_delivery->invokeRemoteMethod("delivery_module", "send", m_announceTopic, bye);
    }
    killMediaMtx();
    // Tear down ONLY the hidden-service tor — never the listener SOCKS tor, which may still be in
    // use by an active onion playback session (Senty ISSUE-2).
    if (m_privacy == "onion") killTorHost();
    if (!m_runtimeDir.isEmpty()) QDir(m_runtimeDir).removeRecursively();
    qDebug() << "RadioModulePlugin: stream stopped";
    emit eventResponse("streamStopped", QVariantList() << m_path);
    // #17 — persist the identity as STOPPED: keeps the stream key (so a later Start reuses it / OBS
    // config stays valid) but won't auto-resume on the next Basecamp restart. "⟳ New" rotates the key.
    saveStreamState(false);
    m_path.clear(); m_streamKey.clear(); m_streamName.clear(); m_lastStreamState.clear();
    m_announceTopic.clear(); m_startedAt = 0; m_announceSeq = 0;
    m_privacy = QStringLiteral("public"); m_onion.clear(); m_onionReady = false; m_onionError.clear();
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

QString RadioModulePlugin::streamState()
{
    if (!m_mediamtx) return QStringLiteral("idle");
    QString body;
    const int code = httpGet(port("RADIO_API_PORT", 9997),
                             QStringLiteral("/v3/paths/get/%1").arg(m_path), body);
    if (code != 200) return QStringLiteral("waiting");  // path not created → OBS not connected
    const QJsonObject p = QJsonDocument::fromJson(body.toUtf8()).object();
    const bool ready = p.value("ready").toBool();
    const bool hasSource = !p.value("source").isNull() && p.value("source").isObject();
    const bool hasTracks = !p.value("tracks").toArray().isEmpty();
    return (ready && hasTracks) ? QStringLiteral("live")
         : hasSource           ? QStringLiteral("receiving")
                               : QStringLiteral("waiting");
}

QString RadioModulePlugin::getStreamStatus()
{
    const QString state = streamState();
    QJsonObject r{{"ok", true}, {"state", state}};
    if (m_mediamtx) {
        r["privacy"] = m_privacy;
        if (m_privacy == "onion") {
            // Never surface lanIp() in onion mode — advertise the .onion (once known) or nothing.
            r["onion"] = m_onion;              // "" until the hostname appears
            r["onionReady"] = m_onionReady;    // false until the descriptor is published
            if (!m_onionError.isEmpty()) r["onionError"] = m_onionError;  // e.g. publish_timeout → UI
            if (!m_onion.isEmpty())
                r["hlsUrl"] = QStringLiteral("http://%1/%2/index.m3u8").arg(m_onion, m_path);
        } else {
            r["hlsUrl"] = QStringLiteral("http://%1:%2/%3/index.m3u8")
                              .arg(lanIp()).arg(port("RADIO_HLS_PORT", 8888)).arg(m_path);
        }
    }
    if (state != m_lastStreamState) {
        m_lastStreamState = state;
        emit eventResponse("streamStatusChanged", QVariantList() << state);
    }
    return QString::fromUtf8(QJsonDocument(r).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------------------
// #5 — discovery: init delivery_module, subscribe, receive + decode announces.
// Wiring mirrors the proven scorched-earth pattern (game_plugin.cpp). delivery_module
// base64-encodes once on send, so messageReceived data[2] needs a single decode.
// ---------------------------------------------------------------------------

QString RadioModulePlugin::directoryTopic() const
{
    // delivery_module content-topic convention: /<module>/1/<channel>/<format>
    return qEnvironmentVariable("RADIO_DIRECTORY_TOPIC",
                                QStringLiteral("/radio-basecamp/1/directory/json"));
}

bool RadioModulePlugin::subscribeTopic(const QString& topic)
{
    if (topic.isEmpty() || !m_delivery) return false;
    if (m_subscribedTopics.contains(topic)) return true;
    m_delivery->invokeRemoteMethod("delivery_module", "subscribe", topic);
    m_subscribedTopics.insert(topic);
    return true;
}

bool RadioModulePlugin::ensureDeliveryNode()
{
    if (m_deliveryNodeUp) return true;
    if (!logosAPI) return false;
    m_delivery = logosAPI->getClient("delivery_module");
    if (!m_delivery) return false;
    m_delivery->invokeRemoteMethod("delivery_module", "createNode",
        QStringLiteral("{\"logLevel\":\"INFO\",\"mode\":\"Core\",\"preset\":\"logos.dev\",\"relay\":true}"));
    m_delivery->invokeRemoteMethod("delivery_module", "start");
    m_deliveryNodeUp = true;
    // Cache our peer id once for the status pill (avoids per-poll IPC).
    const QVariant pid = m_delivery->invokeRemoteMethod("delivery_module", "getNodeInfo", QStringLiteral("MyPeerId"));
    const QJsonObject po = QJsonDocument::fromJson(pid.toString().toUtf8()).object();
    if (po.value("success").toBool()) m_deliveryPeerId = po.value("value").toString();
    return true;
}

void RadioModulePlugin::checkDeliveryHealth()
{
    auto* c = logosAPI ? logosAPI->getClient("delivery_module") : nullptr;
    if (!c) { m_deliveryReachable = false; return; }
    // The delivery_module node comes up on its own at load; ask for our peer id to confirm it answers.
    const QVariant r = c->invokeRemoteMethod("delivery_module", "getNodeInfo", QStringLiteral("MyPeerId"));
    const QJsonObject o = QJsonDocument::fromJson(r.toString().toUtf8()).object();
    const QString pid = o.value("value").toString();
    m_deliveryReachable = o.value("success").toBool() && !pid.isEmpty();
    if (m_deliveryReachable) m_deliveryPeerId = pid;
}

QString RadioModulePlugin::getDeliveryStatus()
{
    const bool loaded = logosAPI && logosAPI->getClient("delivery_module") != nullptr;
    // Green once delivery_module's node actually answers (reachable) or our own node is up.
    const QString state = !loaded ? QStringLiteral("offline")
                        : (m_deliveryReachable || m_deliveryNodeUp) ? QStringLiteral("connected")
                                                                    : QStringLiteral("ready");
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {"ok", true}, {"state", state}, {"peerId", m_deliveryPeerId}
    }).toJson(QJsonDocument::Compact));
}

QString RadioModulePlugin::startDiscovery()
{
    if (m_discovering) return ok();  // idempotent; reentrancy guard
    if (!ensureDeliveryNode()) return err("no_delivery_client");

    // Register the receive handler BEFORE subscribing so no announce is missed.
    m_deliveryObj = m_delivery->requestObject("delivery_module");
    if (m_deliveryObj) {
        m_delivery->onEvent(m_deliveryObj, "messageReceived",
            [this](const QString&, const QVariantList& data) {
                if (data.size() < 3) return;
                ingestAnnounce(data[2].toString());  // data[2] = base64(payload)
            });
    }
    subscribeTopic(directoryTopic());
    m_discovering = true;
    qDebug() << "RadioModulePlugin: discovery started on" << directoryTopic();
    return ok();
}

QString RadioModulePlugin::addTopic(const QString& topic)
{
    // #18: validate the user-supplied content topic first (before state/IPC).
    static const QRegularExpression re(QStringLiteral("^/[A-Za-z0-9._/-]{1,128}$"));
    if (!re.match(topic).hasMatch()) return err("invalid_topic");
    if (!m_discovering) return err("discovery_not_started");
    return subscribeTopic(topic) ? ok() : err("subscribe_failed");
}

void RadioModulePlugin::ingestAnnounce(const QString& base64Payload)
{
    const QByteArray json = QByteArray::fromBase64(base64Payload.toUtf8());  // single decode
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    const QString path = o.value("path").toString();
    if (path.isEmpty()) return;
    if (!m_path.isEmpty() && path == m_path) return;                     // self-echo filter
    // #14: an explicit offline announce drops the station immediately (don't wait for the 45s TTL).
    if (o.value("type").toString() == QLatin1String("offline")) {
        if (m_stations.remove(path) > 0) emit eventResponse("stationsChanged", QVariantList() << path);
        return;
    }
    if (o.value("name").toString().isEmpty()) return;                    // malformed

    QJsonObject station = o;
    station["_lastSeen"] = QDateTime::currentMSecsSinceEpoch();  // TTL pruning → #11
    // Canonical onion flag (Senty ISSUE-1) — the UI badge must use THIS, computed with the same
    // isOnionUrl() as playback routing, not a substring match that can disagree with the transport.
    station["_onion"] = isOnionUrl(o.value("streamUrl").toString());
    const bool isNew = !m_stations.contains(path);
    m_stations[path] = station;
    if (isNew) qDebug() << "RadioModulePlugin: discovered station" << path;
    emit eventResponse("stationsChanged", QVariantList() << path);
}

QString RadioModulePlugin::getStations()
{
    // #11 TTL: drop stations not re-heard within the window (default 45s = 3 missed 15s beats).
    const qint64 ttl = port("RADIO_TTL_MS", 45000);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool pruned = false;
    for (auto it = m_stations.begin(); it != m_stations.end(); ) {
        if (now - static_cast<qint64>(it.value().value("_lastSeen").toDouble()) > ttl) {
            it = m_stations.erase(it); pruned = true;
        } else { ++it; }
    }
    if (pruned) emit eventResponse("stationsChanged", QVariantList() << "expired");

    QJsonArray arr;
    for (const QJsonObject& s : m_stations) arr.append(s);
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"ok", true}, {"stations", arr}})
                                 .toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------------------
// #6 — host announce: build the schema payload, gate on live status, publish.
// ---------------------------------------------------------------------------

QString RadioModulePlugin::buildAnnouncePayload(int seq) const
{
    // Onion mode advertises the .onion (no IP). Defense-in-depth (Senty): onion mode NEVER falls back
    // to lanIp() — if the descriptor isn't up yet it yields an empty URL (and announceOnce is gated on
    // onionReady anyway), so a real IP can't leak even if the gate were bypassed.
    const QString hls = (m_privacy == "onion")
        ? (m_onion.isEmpty() ? QString()
                             : QStringLiteral("http://%1/%2/index.m3u8").arg(m_onion, m_path))
        : QStringLiteral("http://%1:%2/%3/index.m3u8").arg(lanIp()).arg(port("RADIO_HLS_PORT", 8888)).arg(m_path);
    const QJsonObject a{
        {"v", 1}, {"name", m_streamName}, {"host", m_hostLabel}, {"path", m_path},
        {"streamUrl", hls}, {"visibility", m_visibility}, {"description", m_description},
        {"startedAt", m_startedAt}, {"seq", seq}
    };
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

QString RadioModulePlugin::announceOnce()
{
    ++m_announceAttempts;  // #10 heartbeat observability (counts every call incl. timer fires)
    auto result = [](bool announced, const QString& reason, const QString& payload, int seq) {
        QJsonObject r{{"ok", true}, {"announced", announced}};
        if (!reason.isEmpty())  r["reason"]  = reason;
        if (!payload.isEmpty()) r["payload"] = QJsonDocument::fromJson(payload.toUtf8()).object();
        if (announced)          r["seq"]     = seq;
        return QString::fromUtf8(QJsonDocument(r).toJson(QJsonDocument::Compact));
    };
    // Gate: only announce once the origin is actually receiving the stream (#4).
    const QString state = streamState();
    if (state != "live" && state != "receiving")
        return result(false, "not_live", QString(), 0);
    // Onion mode: never announce until the hidden-service descriptor is published — otherwise we'd
    // either send a dead URL or (if onion is empty) fall back to the LAN IP, defeating the point.
    if (m_privacy == "onion" && !m_onionReady)
        return result(false, "onion_not_ready", QString(), 0);

    const QString payload = buildAnnouncePayload(m_announceSeq);
    if (!ensureDeliveryNode())
        return result(false, "no_delivery", payload, 0);  // gate passed; delivery just unavailable

    m_delivery->invokeRemoteMethod("delivery_module", "send", m_announceTopic, payload);
    const int seq = m_announceSeq++;
    qDebug() << "RadioModulePlugin: announced seq" << seq << "on" << m_announceTopic;
    return result(true, QString(), payload, seq);
}

// ---------------------------------------------------------------------------
// #9 — listener playback via ffplay subprocess (Qt Multimedia not in AppImage).
// ---------------------------------------------------------------------------

void RadioModulePlugin::killPlayer()
{
    if (!m_player) return;
    m_player->terminate();
    if (!m_player->waitForFinished(2000)) m_player->kill();
    m_player->deleteLater();
    m_player = nullptr;
}

QString RadioModulePlugin::startFfplay()
{
    killPlayer();
    // A .onion stream needs a local tor SOCKS proxy (separate from any host tor — Senty ISSUE-2).
    if (isOnionUrl(m_playingUrl)) {
        const QString te = ensureTorListen();
        if (!te.isEmpty()) return te;
    } else {
        killTorListen();  // switching to a clearnet stream — don't leave the listener tor running
    }
    const QPair<QString, QStringList> cmd = buildPlayerCommand(m_playingUrl);
    m_player = new QProcess(this);
    dieWithParent(m_player);
    QProcessEnvironment env = cleanSpawnEnv();  // system ffplay/torsocks → system libs (AppImage trap)
    if (isOnionUrl(m_playingUrl)) {
        // Lock torsocks onto OUR tor SOCKS instance (Senty ISSUE-4) — without this, an overridden
        // RADIO_TOR_SOCKS_PORT leaves torsocks on its compiled-in 9050 default, so ffplay could hit
        // the wrong proxy or fail and fall back to a direct connection → listener IP leak.
        env.insert("TORSOCKS_TOR_ADDRESS", "127.0.0.1");
        env.insert("TORSOCKS_TOR_PORT", QString::number(m_listenSocksPort > 0 ? m_listenSocksPort : torSocksPort()));
        env.insert("TORSOCKS_ISOLATE_PID", "1");
    }
    m_player->setProcessEnvironment(env);
    m_player->start(cmd.first, cmd.second);
    if (!m_player->waitForStarted(5000)) {
        const bool notFound = m_player->error() == QProcess::FailedToStart;
        qWarning() << "RadioModulePlugin: player failed:" << m_player->errorString();
        killPlayer();
        return notFound ? QStringLiteral("ffplay_not_found") : QStringLiteral("ffplay_failed");
    }
    return QString();
}

// ---------------------------------------------------------------------------
// Tor onion mode — hide the streamer's IP (epic: docs/plans/tor-onion.md).
// TWO independent tor processes (Senty ISSUE-2): a host tor (SocksPort 0 + HiddenService
// mapping :80 → the local MediaMTX HLS port) and a listener tor (SocksPort, for playing a
// .onion via torsocks). Separate lifecycles so hosting and listening can't tear each other down.
// ---------------------------------------------------------------------------

QPair<QString, QStringList> RadioModulePlugin::buildPlayerCommand(const QString& url) const
{
    const QString ffplay = resolveBin(QStringLiteral("ffplay"), "RADIO_FFPLAY_BIN");
    QStringList ffargs;
    ffargs << "-nodisp" << "-autoexit" << "-loglevel" << "error" << "-infbuf";
    // MediaMTX HLS gates playback with a `Secure` cookieCheck cookie; ffmpeg won't send a Secure cookie
    // back over the http:// onion → the 302 redirect loops → "End of file" → no audio. Pre-supply it.
    ffargs << "-cookies" << "cookieCheck=1; path=/";
    // #17 jitter buffer: start N segments behind the live edge (MediaMTX serves 1s mpegts segments)
    // so playback rides out Tor latency spikes; -infbuf lets ffplay hold the read-ahead.
    if (m_listenBufferSec > 0)
        ffargs << "-live_start_index" << QString::number(-m_listenBufferSec);
    ffargs << "-volume" << QString::number(m_volume) << url;
    // ffmpeg has no native SOCKS; route .onion playback through torsocks (LD_PRELOAD → tor SOCKS).
    if (isOnionUrl(url)) {
        const QString torsocks = resolveBin(QStringLiteral("torsocks"), "RADIO_TORSOCKS_BIN");
        return { torsocks, QStringList() << ffplay << ffargs };
    }
    return { ffplay, ffargs };
}

bool RadioModulePlugin::startTorProc(QProcess*& proc, const QString& dir, const QString& cfg, QString& errOut)
{
    const QString bin = resolveBin(QStringLiteral("tor"), "RADIO_TOR_BIN");
    const QString dataDir = dir + "/data", torrc = dir + "/torrc";
    // Remove the temp tree on any failure so a failed start leaves nothing on disk (Senty ISSUE-5).
    auto fail = [&](const QString& code) { QDir(dir).removeRecursively(); errOut = code; return false; };
    if (!QDir().mkpath(dataDir)) return fail(QStringLiteral("tor_dir_failed"));
    // Tor state (key material, data, log) must not be world-readable (Senty FINDING-4).
    const QFileDevice::Permissions ownerOnly =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
    QFile::setPermissions(dir, ownerOnly);
    QFile::setPermissions(dataDir, ownerOnly);
    QFile f(torrc);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail(QStringLiteral("tor_cfg_failed"));
    f.write(cfg.toUtf8()); f.close();

    proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setProcessEnvironment(cleanSpawnEnv());  // apt tor needs system libevent, not the AppImage's
    dieWithParent(proc);   // don't orphan tor on kill -9
    proc->start(bin, QStringList() << "-f" << torrc);
    if (!proc->waitForStarted(5000)) {
        const bool notFound = proc->error() == QProcess::FailedToStart;
        qWarning() << "RadioModulePlugin: tor failed to start:" << proc->errorString();
        proc->deleteLater(); proc = nullptr;
        return fail(notFound ? QStringLiteral("tor_not_found") : QStringLiteral("tor_start_failed"));
    }
    // Immediate exit ⇒ bad config or the SocksPort/HS port already in use (Senty ISSUE-3) — don't
    // proceed as if Tor were healthy, which would silently break the privacy transport.
    if (proc->waitForFinished(500)) {
        const QByteArray out = proc->readAll();
        qWarning() << "RadioModulePlugin: tor exited immediately:" << out;
        // logos_host child stderr is swallowed (#163) — persist the real reason for diagnosis.
        const QString diagPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                                 + "/radio_module/tor-fail.log";
        QDir().mkpath(QFileInfo(diagPath).absolutePath());
        QFile diag(diagPath);
        if (diag.open(QIODevice::WriteOnly | QIODevice::Append)) {
            diag.write("---- tor exited immediately ----\n"); diag.write(out); diag.write("\n"); diag.close();
        }
        proc->deleteLater(); proc = nullptr;
        return fail(QStringLiteral("tor_port_in_use"));
    }
    return true;
}

QString RadioModulePlugin::ensureTorHost()
{
    if (m_torHost && m_torHost->state() == QProcess::Running) return QString();
    m_torHostDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                   + "/radio_module/torhost-" + (m_path.isEmpty() ? randomHex(4) : m_path);
    // #17 — the hidden-service keys live in a PERSISTENT per-profile dir (not the temp run dir), so the
    // .onion address survives restarts. regenerateOnion() wipes it to mint a fresh address on demand.
    const QString hsDir = persistentHsDir();
    QDir().mkpath(hsDir);
    QFile::setPermissions(hsDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QString cfg;
    QTextStream s(&cfg);
    // SocksPort 0 → the host tor ONLY serves the hidden service; listening uses a separate tor, so
    // stopping/starting a broadcast can't tear down an active onion listener (Senty ISSUE-2).
    s << "SocksPort 0\n"
      << "DataDirectory " << m_torHostDir << "/data\n"
      << "Log notice file " << m_torHostDir << "/tor.log\n"
      // The HS descriptor upload is logged at INFO in the [rend] (rendezvous) domain — capture just
      // that (small log) so readiness detection sees the publish (notice level never logs it).
      << "Log [rend]info file " << m_torHostDir << "/hs.log\n"
      << "HiddenServiceDir " << hsDir << "\n"
      << "HiddenServicePort 80 127.0.0.1:" << port("RADIO_HLS_PORT", 8888) << "\n";
    QString err;
    if (!startTorProc(m_torHost, m_torHostDir, cfg, err)) { m_torHostDir.clear(); return err; }
    m_onion.clear(); m_onionReady = false; m_onionError.clear();
    m_onionPollTicks = 0; m_onionBootstrapTick = 0;
    m_onionPublishPoll.start(2000);
    return QString();
}

QString RadioModulePlugin::ensureTorListen()
{
    if (m_torListen && m_torListen->state() == QProcess::Running) return QString();
    const int base = torSocksPort();
    QString err;
    // Retry on the next port if the SOCKS port is momentarily taken (e.g. a previous listener tor
    // hasn't fully released it) — a transient conflict must not fail playback (was: tor_port_in_use).
    for (int off = 0; off < 4; ++off) {
        const int p = base + off;
        m_torListenDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                         + "/radio_module/torlisten-" + randomHex(4);
        QString cfg;
        QTextStream s(&cfg);
        s << "SocksPort " << p << "\n"
          << "DataDirectory " << m_torListenDir << "/data\n"
          << "Log notice file " << m_torListenDir << "/tor.log\n";
        if (startTorProc(m_torListen, m_torListenDir, cfg, err)) { m_listenSocksPort = p; return QString(); }
        m_torListenDir.clear();
    }
    return err;  // every candidate port failed → a real error (not a transient conflict)
}

void RadioModulePlugin::pollOnionStatus()
{
    if (m_torHostDir.isEmpty()) { m_onionPublishPoll.stop(); return; }
    // Bounded poll (Senty ISSUE-3): hard cap ~120s before surfacing a real timeout.
    if (++m_onionPollTicks > 60) {
        m_onionPublishPoll.stop();
        if (!m_onionReady) {
            m_onionError = QStringLiteral("publish_timeout");  // surfaced via getStreamStatus → UI
            qWarning() << "RadioModulePlugin: onion descriptor publish timed out";
            emit eventResponse("onionError", QVariantList() << QStringLiteral("publish_timeout"));
        }
        return;
    }
    if (m_onion.isEmpty()) {
        // #17 — the hostname lives in the PERSISTENT HS dir now (not m_torHostDir); reading the old
        // temp path left m_onion empty → readiness never checked → false publish_timeout + bad announce.
        QFile hf(persistentHsDir() + "/hostname");
        if (hf.open(QIODevice::ReadOnly)) { m_onion = QString::fromUtf8(hf.readAll()).trimmed(); hf.close(); }
    }
    if (m_onion.isEmpty() || m_onionReady) return;

    auto fileHas = [](const QString& path, const char* a, const char* b) {
        QFile f(path); if (!f.open(QIODevice::ReadOnly)) return false;
        const QString s = QString::fromUtf8(f.readAll()); f.close();
        return s.contains(QLatin1String(a), Qt::CaseInsensitive)
            && (!b || s.contains(QLatin1String(b), Qt::CaseInsensitive));
    };
    // Precise: the [hs]info log records the descriptor upload to the HSDirs (this is what the old
    // notice-level grep could NEVER see → it false-timed-out a perfectly reachable onion).
    bool ready = fileHas(m_torHostDir + "/hs.log", "upload", "descriptor");
    // Fallback (robust to tor wording/log-level changes): once bootstrapped 100%, the descriptor
    // publishes within tens of seconds — accept after a short grace so a live onion is never missed.
    if (!ready && fileHas(m_torHostDir + "/tor.log", "Bootstrapped 100%", nullptr)) {
        if (m_onionBootstrapTick == 0) m_onionBootstrapTick = m_onionPollTicks;
        if (m_onionPollTicks - m_onionBootstrapTick >= 12) ready = true;  // ~24s after 100%
    }
    if (ready) {
        m_onionReady = true;
        m_onionPublishPoll.stop();
        qDebug() << "RadioModulePlugin: onion descriptor published — reachable";
        emit eventResponse("onionReady", QVariantList() << m_path);  // host id, not the .onion
    }
}

void RadioModulePlugin::killTorHost()
{
    m_onionPublishPoll.stop();
    if (m_torHost) {
        m_torHost->terminate();
        if (!m_torHost->waitForFinished(3000)) m_torHost->kill();
        m_torHost->deleteLater();
        m_torHost = nullptr;
    }
    if (!m_torHostDir.isEmpty()) { QDir(m_torHostDir).removeRecursively(); m_torHostDir.clear(); }
    m_onion.clear(); m_onionReady = false;
}

void RadioModulePlugin::killTorListen()
{
    if (m_torListen) {
        m_torListen->terminate();
        if (!m_torListen->waitForFinished(3000)) m_torListen->kill();
        m_torListen->deleteLater();
        m_torListen = nullptr;
    }
    if (!m_torListenDir.isEmpty()) { QDir(m_torListenDir).removeRecursively(); m_torListenDir.clear(); }
    m_listenSocksPort = 0;
}

// --- Test seams (not IPC API) ---
void RadioModulePlugin::configureOnionForTest(const QString& onion)
{
    m_privacy = QStringLiteral("onion");
    m_onion = onion;
    m_onionReady = !onion.isEmpty();
}

QStringList RadioModulePlugin::playerCommandForTest(const QString& url) const
{
    const QPair<QString, QStringList> c = buildPlayerCommand(url);
    return QStringList() << c.first << c.second;
}

QString RadioModulePlugin::play(const QString& hlsUrl, const QString& stationName)
{
    if (hlsUrl.isEmpty()) return err("no_url");
    // #18: a station's streamUrl is attacker-controlled (anyone can announce). Only let ffplay
    // open http/https — never file:, pipe:, concat:, a device, or other ffmpeg protocols.
    const QString scheme = QUrl(hlsUrl).scheme().toLower();
    if (scheme != "http" && scheme != "https") return err("unsafe_url");
    m_playingUrl = hlsUrl;
    m_playingStation = stationName;
    const QString e = startFfplay();
    if (!e.isEmpty()) { m_playingUrl.clear(); m_playingStation.clear(); return err(e); }
    emit eventResponse("playerStatusChanged", QVariantList() << "playing" << stationName);
    return ok();
}

QString RadioModulePlugin::setVolume(int percent)  // #13 — ffplay has no runtime volume; restart
{
    m_volume = qBound(0, percent, 100);
    if (m_player) startFfplay();  // brief gap; live HLS reconnects at the edge
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"ok", true}, {"volume", m_volume}})
                                 .toJson(QJsonDocument::Compact));
}

QString RadioModulePlugin::setListenBuffer(int seconds)  // #17 — deeper buffer rides out Tor jitter
{
    m_listenBufferSec = qBound(0, seconds, 20);  // capped by the host playlist depth (24 × 1s)
    if (m_player && m_player->state() != QProcess::NotRunning && !m_playingUrl.isEmpty())
        startFfplay();  // re-apply live; brief re-buffer gap
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"ok", true}, {"bufferSec", m_listenBufferSec}})
                                 .toJson(QJsonDocument::Compact));
}

QString RadioModulePlugin::stop()
{
    if (!m_player) return err("not_playing");
    killPlayer();
    killTorListen();   // playback over; release the listener SOCKS tor (host tor untouched)
    m_playingStation.clear();
    m_playingUrl.clear();
    emit eventResponse("playerStatusChanged", QVariantList() << "stopped");
    return ok();
}

QString RadioModulePlugin::getPlayerStatus()
{
    const bool running = m_player && m_player->state() != QProcess::NotRunning;
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {"ok", true}, {"state", running ? "playing" : "stopped"},
        {"station", m_playingStation}, {"volume", m_volume}, {"bufferSec", m_listenBufferSec}
    }).toJson(QJsonDocument::Compact));
}
