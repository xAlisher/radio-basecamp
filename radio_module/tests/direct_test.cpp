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

    // --- #3: startStream mints a full ingest card ---
    RadioModulePlugin p;
    const QJsonObject card = QJsonDocument::fromJson(
        p.startStream("{\"name\":\"Test\",\"visibility\":\"public\"}").toUtf8()).object();
    printf("CARD: %s\n", QJsonDocument(card).toJson(QJsonDocument::Compact).constData());
    bool fields = true;
    for (const char* k : {"ok","path","streamKey","whipUrl","rtmpUrl","srtUrl","hlsUrl"})
        fields = fields && card.contains(k);
    ok(fields, "card has all ingest fields");
    const QString path1 = card.value("path").toString();
    ok(!path1.isEmpty(), "path minted");

    // --- #2 spawn: MediaMTX actually comes up under module control ---
    QThread::msleep(2500);
    ok(mtxApiUp(apiPort), "MediaMTX API up (module spawned it)");

    // --- #4 status: waiting (no publisher) → live (after an ffmpeg push) ---
    auto state = [&]{ return QJsonDocument::fromJson(p.getStreamStatus().toUtf8())
                          .object().value("state").toString(); };
    ok(state() == "waiting", "status 'waiting' with no publisher");

    int rtmp = qEnvironmentVariableIntValue("RADIO_RTMP_PORT"); if (!rtmp) rtmp = 1935;
    QProcess push;
    push.start("ffmpeg", {"-hide_banner","-loglevel","error","-re",
        "-f","lavfi","-i","testsrc=size=320x240:rate=15","-f","lavfi","-i","sine=frequency=1000",
        "-c:v","libx264","-preset","ultrafast","-tune","zerolatency","-b:v","300k","-c:a","aac",
        "-f","flv", QString("rtmp://127.0.0.1:%1/%2").arg(rtmp).arg(path1)});
    push.waitForStarted(3000);
    QString st; for (int i = 0; i < 12 && st != "live"; ++i) { QThread::msleep(1000); st = state(); }
    ok(st == "live" || st == "receiving", (QString("status '")+st+"' while publishing").toUtf8());
    push.kill(); push.waitForFinished(2000);

    // --- stopStream tears it down ---
    p.stopStream();
    QThread::msleep(800);
    ok(!mtxApiUp(apiPort), "MediaMTX down after stopStream");

    // --- uniqueness: a second stream mints a different path ---
    const QString path2 = QJsonDocument::fromJson(
        p.startStream("{\"name\":\"Test2\",\"visibility\":\"public\"}").toUtf8()).object().value("path").toString();
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

    printf("=== %s ===\n", fails ? "FAILURES" : "ALL PASS");
    return fails ? 1 : 0;
}
