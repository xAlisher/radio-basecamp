#ifndef RADIO_INTERFACE_H
#define RADIO_INTERFACE_H

#include <QObject>
#include <QString>
#include "interface.h"

class RadioInterface : public PluginInterface
{
public:
    virtual ~RadioInterface() = default;
};

#define RadioInterface_iid "org.logos.RadioInterface"
Q_DECLARE_INTERFACE(RadioInterface, RadioInterface_iid)

#endif // RADIO_INTERFACE_H
