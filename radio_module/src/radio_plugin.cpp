#include "radio_plugin.h"
#include "logos_api.h"
#include <QDebug>

// v0.1.0 scaffold. Bodies are stubs; see docs/plans/radio-implementation.md for the issue
// that implements each one. Keeping a uniform JSON return shape so the QML bridge is stable.

namespace {
QString ok(const QString& extra = QString())
{
    return extra.isEmpty() ? QStringLiteral("{\"ok\":true}")
                           : QStringLiteral("{\"ok\":true,%1}").arg(extra);
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
    // Issue #2: ensure MediaMTX QProcess is terminated here. Issue #9: stop ffplay.
}

void RadioModulePlugin::initLogos(LogosAPI* api)
{
    m_logosAPI = api;
    qDebug() << "RadioModulePlugin: initLogos";
    // Issue #5: pre-init delivery_module client here (eager init avoids bad_alloc; see skill ipc-client-eager-init).
    emit eventResponse("initialized", QVariantList() << "radio_module" << "0.1.0");
}

QString RadioModulePlugin::ping() { return ok("\"version\":\"0.1.0\""); }

QString RadioModulePlugin::startStream(const QString&)   { return notImplemented("startStream"); }
QString RadioModulePlugin::stopStream()                  { return notImplemented("stopStream"); }
QString RadioModulePlugin::getStreamStatus()             { return notImplemented("getStreamStatus"); }
QString RadioModulePlugin::startDiscovery()              { return notImplemented("startDiscovery"); }
QString RadioModulePlugin::addTopic(const QString&)      { return notImplemented("addTopic"); }
QString RadioModulePlugin::getStations()                 { return notImplemented("getStations"); }
QString RadioModulePlugin::play(const QString&, const QString&) { return notImplemented("play"); }
QString RadioModulePlugin::pause()                       { return notImplemented("pause"); }
QString RadioModulePlugin::resume()                      { return notImplemented("resume"); }
QString RadioModulePlugin::stop()                        { return notImplemented("stop"); }
QString RadioModulePlugin::setVolume(int)                { return notImplemented("setVolume"); }
QString RadioModulePlugin::getPlayerStatus()             { return notImplemented("getPlayerStatus"); }
