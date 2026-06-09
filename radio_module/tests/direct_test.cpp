// Direct (in-process) test of radio_module — instantiates the plugin and calls methods
// without the IPC/capability layer. This is the Tier-1 proof path for side-effectful methods
// that bare logoscore can't verify (its capability handshake gates all returns to `false`).
//
// Built + run by tests/run-direct-test.sh (derives Qt/SDK paths). Add cases as issues land.
#include "radio_plugin.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
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

    // --- stopStream tears it down ---
    p.stopStream();
    QThread::msleep(800);
    ok(!mtxApiUp(apiPort), "MediaMTX down after stopStream");

    // --- uniqueness: a second stream mints a different path ---
    const QString path2 = QJsonDocument::fromJson(
        p.startStream("{\"name\":\"Test2\",\"visibility\":\"public\"}").toUtf8()).object().value("path").toString();
    ok(!path2.isEmpty() && path2 != path1, "second stream path is unique");
    p.stopStream();

    printf("=== %s ===\n", fails ? "FAILURES" : "ALL PASS");
    return fails ? 1 : 0;
}
