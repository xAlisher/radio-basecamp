#ifndef RADIO_PLUGIN_H
#define RADIO_PLUGIN_H

#include <QString>
#include <QVariantList>
#include "radio_interface.h"
#include "LogosViewPluginBase.h"
#include "rep_radio_source.h"

class LogosAPI;
class LogosModules;
class QTimer;

// ui_qml module with C++ backend (tutorial Part 3 / logos-delivery-demo shape).
// Runs in ui-host, where consuming delivery_module via LogosModules works (unlike a core module).
class RadioPlugin : public RadioSimpleSource,
                    public RadioInterface,
                    public RadioViewPluginBase
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID RadioInterface_iid FILE "metadata.json")
    Q_INTERFACES(RadioInterface)

public:
    explicit RadioPlugin(QObject* parent = nullptr);
    ~RadioPlugin() override;

    QString name()    const override { return "radio"; }
    QString version() const override { return "0.2.0"; }

    Q_INVOKABLE void initLogos(LogosAPI* api);

    // .rep SLOTs
    QString startDiscovery() override;

signals:
    void eventResponse(const QString& eventName, const QVariantList& args);

private:
    void wireEvents();
    void pollDeliveryHealth();

    LogosAPI*     m_logosAPI = nullptr;
    LogosModules* m_logos = nullptr;
    QTimer*       m_healthTimer = nullptr;
    bool          m_nodeUp = false;
};

#endif // RADIO_PLUGIN_H
