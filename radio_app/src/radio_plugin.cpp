#include "radio_plugin.h"
#include "logos_api.h"
#include "logos_sdk.h"     // generated LogosModules typed SDK
#include <QTimer>
#include <QDebug>

RadioPlugin::RadioPlugin(QObject* parent) : RadioSimpleSource(parent)
{
    qDebug() << "RadioPlugin: constructed";
}

RadioPlugin::~RadioPlugin()
{
    delete m_logos;
}

void RadioPlugin::initLogos(LogosAPI* api)
{
    if (m_logos) return;
    m_logosAPI = api;
    qDebug() << "RadioPlugin: initLogos — constructing LogosModules (ui-host: delivery works here)";
    m_logos = new LogosModules(api);   // crashes in a core module (#31); fine in a ui_qml backend
    setBackend(this);                  // register as the QRO source for the QML replica
    wireEvents();

    // delivery_module's node comes up on its own; poll its peer id for the status PROP.
    m_healthTimer = new QTimer(this);
    connect(m_healthTimer, &QTimer::timeout, this, [this]{ pollDeliveryHealth(); });
    m_healthTimer->start(5000);
    QTimer::singleShot(1500, this, [this]{ pollDeliveryHealth(); });

    qDebug() << "RadioPlugin: initLogos done";
}

void RadioPlugin::wireEvents()
{
    m_logos->delivery_module.on("connectionStateChanged", [this](const QVariantList& data) {
        if (!data.isEmpty()) qDebug() << "RadioPlugin: connectionStateChanged" << data.at(0).toString();
    });
    m_logos->delivery_module.on("messageReceived", [this](const QVariantList& data) {
        if (data.size() < 3) return;
        qDebug() << "RadioPlugin: messageReceived on" << data.at(1).toString();
        // (announce ingest goes here in the full port)
    });
}

void RadioPlugin::pollDeliveryHealth()
{
    if (!m_logos) return;
    const auto peer = m_logos->delivery_module.getNodeInfo(QStringLiteral("MyPeerId"));
    if (peer.success && !peer.getString().isEmpty()) {
        setPeerId(peer.getString());
        setDeliveryState(m_nodeUp ? QStringLiteral("connected") : QStringLiteral("ready"));
    }
}

QString RadioPlugin::startDiscovery()
{
    if (!m_logos) return QStringLiteral("{\"ok\":false,\"error\":\"no_backend\"}");
    const auto created = m_logos->delivery_module.createNode(
        QStringLiteral("{\"logLevel\":\"INFO\",\"mode\":\"Core\",\"preset\":\"logos.dev\",\"relay\":true}"));
    if (!created.success) { setLastError(created.getError()); return QStringLiteral("{\"ok\":false,\"error\":\"createNode\"}"); }
    const auto started = m_logos->delivery_module.start();
    if (!started.success) { setLastError(started.getError()); return QStringLiteral("{\"ok\":false,\"error\":\"start\"}"); }
    m_logos->delivery_module.subscribe(QStringLiteral("/radio-basecamp/1/directory/json"));
    m_nodeUp = true;
    setDeliveryState(QStringLiteral("connected"));
    pollDeliveryHealth();
    return QStringLiteral("{\"ok\":true}");
}
