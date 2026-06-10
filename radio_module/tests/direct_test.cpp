// Direct (in-process) test of radio_module — instantiates the plugin and calls methods
// without the IPC/capability layer. This is the Tier-1 proof path for side-effectful methods
// that bare logoscore can't verify (its capability handshake gates all returns to `false`).
//
// Built + run by tests/run-direct-test.sh (derives Qt/SDK paths). Add cases as issues land.
#include "radio_plugin.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QThread>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QStringList>
#include <cstdio>

static int fails = 0;
static void ok(bool c, const char* msg) { printf("%s: %s\n", c ? "PASS" : "FAIL", msg); if (!c) ++fails; }

static bool mtxApiUp(int port) {
    QProcess c; c.start("curl", {"-s","-o","/dev/null","-w","%{http_code}","--max-time","2",
                                 QString("http://127.0.0.1:%1/v3/config/global/get").arg(port)});
    c.waitForFinished(4000);
    return c.readAllStandardOutput().trimmed() == "200";
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int apiPort = qEnvironmentVariableIntValue("RADIO_API_PORT"); if (!apiPort) apiPort = 9997;

    // --- #6 gating: nothing streaming yet → announce is gated 'not_live' ---
    RadioModulePlugin p;
    ok(QJsonDocument::fromJson(p.announceOnce().toUtf8()).object().value("reason").toString() == "not_live",
       "announce gated 'not_live' before streaming");

    // --- #3: startStream mints a full ingest card ---
    // privacy:public pins direct mode (default is now onion, which would spawn tor — covered separately).
    const QJsonObject card = QJsonDocument::fromJson(
        p.startStream("{\"name\":\"Test\",\"visibility\":\"public\",\"privacy\":\"public\"}").toUtf8()).object();
    printf("CARD: %s\n", QJsonDocument(card).toJson(QJsonDocument::Compact).constData());
    bool fields = true;
    for (const char* k : {"ok","path","streamKey","whipUrl","rtmpUrl","srtUrl","hlsUrl"})
        fields = fields && card.contains(k);
    ok(fields, "card has all ingest fields");
    const QString path1 = card.value("path").toString();
    ok(!path1.isEmpty(), "path minted");
    // #11: getStreamCard rebuilds the same card (the UI uses it to rehydrate after a restart).
    ok(QJsonDocument::fromJson(p.getStreamCard().toUtf8()).object().value("path").toString() == path1,
       "getStreamCard returns the active card (#11)");

    // --- #2 spawn: MediaMTX actually comes up under module control ---
    QThread::msleep(2500);
    ok(mtxApiUp(apiPort), "MediaMTX API up (module spawned it)");

    // --- #4 status: waiting (no publisher) → live (after an ffmpeg push) ---
    auto state = [&]{ return QJsonDocument::fromJson(p.getStreamStatus().toUtf8())
                          .object().value("state").toString(); };
    ok(state() == "waiting", "status 'waiting' with no publisher");

    int rtmp = qEnvironmentVariableIntValue("RADIO_RTMP_PORT"); if (!rtmp) rtmp = 1935;
    // #18: publishing now requires the secret key (card.streamKey = "<path>?user=publisher&pass=…").
    const QString streamKey = card.value("streamKey").toString();
    QProcess push;
    push.start("ffmpeg", {"-hide_banner","-loglevel","error","-re",
        "-f","lavfi","-i","testsrc=size=320x240:rate=15","-f","lavfi","-i","sine=frequency=1000",
        "-c:v","libx264","-preset","ultrafast","-tune","zerolatency","-b:v","300k","-c:a","aac",
        "-f","flv", QString("rtmp://127.0.0.1:%1/%2").arg(rtmp).arg(streamKey)});
    push.waitForStarted(3000);
    QString st; for (int i = 0; i < 12 && st != "live"; ++i) { QThread::msleep(1000); st = state(); }
    ok(st == "live" || st == "receiving", (QString("status '")+st+"' while publishing").toUtf8());

    // --- #6: while live, the announce gate passes and the payload carries the full schema ---
    {
        const QJsonObject ann = QJsonDocument::fromJson(p.announceOnce().toUtf8()).object();
        const QJsonObject pl = ann.value("payload").toObject();
        bool schema = true;
        for (const char* k : {"v","name","host","path","streamUrl","visibility","startedAt","seq"})
            schema = schema && pl.contains(k);
        // no delivery_module in-process → can't actually send, but the GATE passed (state live)
        ok(ann.value("reason").toString() == "no_delivery" && schema,
           "announce gate passes when live + payload has full schema");
        // #18: the announce must NOT leak the publish secret.
        const QString plStr = QString::fromUtf8(QJsonDocument(pl).toJson(QJsonDocument::Compact));
        ok(!plStr.contains("pass=") && !plStr.contains("publisher"), "announce carries no publish secret");
    }

    // --- #10 heartbeat: while live, the timer re-fires announceOnce (RADIO_HEARTBEAT_MS set small) ---
    {
        auto spin = [](int ms){ QEventLoop l; QTimer::singleShot(ms, &l, &QEventLoop::quit); l.exec(); };
        const int before = p.announceAttemptCount();
        spin(1200);
        ok(p.announceAttemptCount() - before >= 3, "heartbeat re-announces while live");
    }

    // --- #9 playback over the LIVE HLS (http only; the player rejects other schemes) ---
    {
        int hlsP = qEnvironmentVariableIntValue("RADIO_HLS_PORT"); if (!hlsP) hlsP = 8888;
        const QString hlsUrl = QString("http://127.0.0.1:%1/%2/index.m3u8").arg(hlsP).arg(path1);
        auto pstate = [&]{ return QJsonDocument::fromJson(p.getPlayerStatus().toUtf8())
                               .object().value("state").toString(); };
        p.play(hlsUrl, "Live FM");
        QThread::msleep(2500);
        ok(pstate() == "playing", "play HLS: ffplay running");
        const int vol = QJsonDocument::fromJson(p.setVolume(40).toUtf8()).object().value("volume").toInt();
        ok(vol == 40, "setVolume: clamps + reports volume");
        p.stop();
        QThread::msleep(400);
        ok(pstate() == "stopped", "stop: player stopped");
    }
    push.kill(); push.waitForFinished(2000);

    // --- stopStream tears it down ---
    p.stopStream();
    QThread::msleep(800);
    ok(!mtxApiUp(apiPort), "MediaMTX down after stopStream");

    // --- uniqueness: a second stream mints a different path ---
    const QString path2 = QJsonDocument::fromJson(
        p.startStream("{\"name\":\"Test2\",\"visibility\":\"public\",\"privacy\":\"public\"}").toUtf8()).object().value("path").toString();
    ok(!path2.isEmpty() && path2 != path1, "second stream path is unique");
    p.stopStream();

    // --- #5 discovery: ingestAnnounce decodes base64, stores, self-echo + malformed filtered ---
    // (the delivery_module IPC round-trip itself needs the AppImage; this proves the decode/parse path.)
    auto b64 = [](const QString& s){ return QString::fromUtf8(s.toUtf8().toBase64()); };
    p.ingestAnnounce(b64("{\"v\":1,\"name\":\"Remote FM\",\"path\":\"abc123\",\"host\":\"alice\",\"hlsUrl\":\"http://h/abc123/index.m3u8\"}"));
    p.ingestAnnounce(b64("{\"name\":\"\",\"path\":\"bad\"}"));   // malformed (no name) → ignored
    {
        const QJsonObject st = QJsonDocument::fromJson(p.getStations().toUtf8()).object();
        const QJsonArray sa = st.value("stations").toArray();
        bool found = false; for (const auto v : sa) if (v.toObject().value("path").toString() == "abc123") found = true;
        ok(sa.size() == 1 && found, "ingestAnnounce: valid station stored, malformed dropped");
    }

    // --- #11 TTL: a station drops out once it isn't re-heard within the window ---
    {
        qputenv("RADIO_TTL_MS", "200");
        p.ingestAnnounce(b64("{\"v\":1,\"name\":\"Expiring\",\"path\":\"ttltest\",\"host\":\"bob\",\"hlsUrl\":\"http://h/x\"}"));
        auto hasTtl = [&]{
            const QJsonArray sa = QJsonDocument::fromJson(p.getStations().toUtf8()).object().value("stations").toArray();
            for (const auto v : sa) if (v.toObject().value("path").toString() == "ttltest") return true;
            return false;
        };
        ok(hasTtl(), "TTL: station present before expiry");
        QThread::msleep(350);  // > 200ms TTL
        ok(!hasTtl(), "TTL: station pruned after expiry");
        qunsetenv("RADIO_TTL_MS");
    }

    // --- #14 offline announce drops the station immediately (no waiting for TTL) ---
    {
        auto has = [&](const QString& pth){
            const QJsonArray sa = QJsonDocument::fromJson(p.getStations().toUtf8()).object().value("stations").toArray();
            for (const auto v : sa) if (v.toObject().value("path").toString() == pth) return true;
            return false; };
        p.ingestAnnounce(b64("{\"v\":1,\"name\":\"Bye FM\",\"path\":\"byep\",\"host\":\"z\",\"streamUrl\":\"http://h/byep/index.m3u8\"}"));
        const bool before = has("byep");
        p.ingestAnnounce(b64("{\"v\":1,\"type\":\"offline\",\"path\":\"byep\"}"));
        ok(before && !has("byep"), "offline announce removes the station immediately (#14)");
    }

    // --- #18 security: player allowlist + topic validation (inputs are attacker-controlled) ---
    ok(QJsonDocument::fromJson(p.play("/etc/passwd", "x").toUtf8()).object().value("error").toString() == "unsafe_url",
       "play rejects a non-http path");
    ok(QJsonDocument::fromJson(p.play("file:///etc/passwd", "x").toUtf8()).object().value("error").toString() == "unsafe_url",
       "play rejects file:// url");
    ok(QJsonDocument::fromJson(p.addTopic("not a topic!").toUtf8()).object().value("error").toString() == "invalid_topic",
       "addTopic rejects a malformed topic");

    // --- Tor onion mode (T4/T5): onion announce carries a .onion (no IP); .onion playback uses torsocks ---
    {
        RadioModulePlugin op;
        op.configureOnionForTest("examplexyz234abcdefghijklmnopqrstuvwx2onionhost.onion");
        const QJsonObject pl = QJsonDocument::fromJson(op.buildAnnouncePayload(0).toUtf8()).object();
        const QString surl = pl.value("streamUrl").toString();
        ok(QUrl(surl).host().endsWith(".onion"), "onion announce streamUrl host is a .onion (no IP leaked)");

        const QStringList oc = op.playerCommandForTest("http://abc23host.onion/p/index.m3u8");
        ok(oc.size() >= 2 && oc.at(0).contains("torsocks") && oc.contains("http://abc23host.onion/p/index.m3u8"),
           "play routes a .onion URL through torsocks");
        ok(oc.contains("-infbuf") && oc.contains("-live_start_index"),
           "player applies the listener jitter buffer (#17)");
        const QStringList dc = op.playerCommandForTest("http://1.2.3.4:8888/p/index.m3u8");
        ok(!dc.at(0).contains("torsocks") && dc.at(0).contains("ffplay"),
           "play uses bare ffplay for a non-onion URL");
        // Senty FINDING-1: mixed-case + trailing-dot .onion must NOT dodge Tor routing.
        const QStringList tc = op.playerCommandForTest("http://AbcXyZ234host.OnIoN./p/index.m3u8");
        ok(tc.at(0).contains("torsocks"),
           "play routes a non-canonical .onion (case/trailing-dot) via torsocks");

        // Senty ISSUE-1: the UI badge flag is backend-computed + canonical (matches routing), not a
        // spoofable substring. A mixed-case/trailing-dot onion is flagged; a clearnet host is not.
        RadioModulePlugin sp;
        auto b = [](const QString& s){ return QString::fromUtf8(s.toUtf8().toBase64()); };
        sp.ingestAnnounce(b("{\"v\":1,\"name\":\"OnionFM\",\"path\":\"onp\",\"host\":\"x\",\"streamUrl\":\"http://AbC234host.OnIoN./onp/index.m3u8\"}"));
        sp.ingestAnnounce(b("{\"v\":1,\"name\":\"ClearFM\",\"path\":\"clp\",\"host\":\"y\",\"streamUrl\":\"http://1.2.3.4:8888/clp/index.m3u8\"}"));
        const QJsonArray sa = QJsonDocument::fromJson(sp.getStations().toUtf8()).object().value("stations").toArray();
        bool onionFlag = false, clearFlag = true;
        for (const auto v : sa) { const QJsonObject o = v.toObject();
            if (o.value("path").toString() == "onp") onionFlag = o.value("_onion").toBool();
            if (o.value("path").toString() == "clp") clearFlag = o.value("_onion").toBool(); }
        ok(onionFlag && !clearFlag, "ingest sets a canonical _onion flag (badge consistent with routing)");
    }

    printf("=== %s ===\n", fails ? "FAILURES" : "ALL PASS");
    return fails ? 1 : 0;
}
