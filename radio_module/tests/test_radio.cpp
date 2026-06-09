// Tier-1 offline unit tests for radio_module (Qt::Test).
// Scaffold: asserts the API contract is wired and ping() works. Each issue adds cases
// (mock the HTTP layer for #4, the delivery events for #5/#11, an injectable clock for #10/#11).
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include "radio_plugin.h"

class TestRadio : public QObject
{
    Q_OBJECT
private slots:
    void ping_returns_ok()
    {
        RadioModulePlugin plugin;
        const QJsonObject o = QJsonDocument::fromJson(plugin.ping().toUtf8()).object();
        QVERIFY(o.value("ok").toBool());
        QCOMPARE(o.value("version").toString(), QStringLiteral("0.1.0"));
    }

    void unimplemented_methods_report_cleanly()
    {
        RadioModulePlugin plugin;
        const QJsonObject o = QJsonDocument::fromJson(plugin.getStations().toUtf8()).object();
        QVERIFY(!o.value("ok").toBool());
        QCOMPARE(o.value("error").toString(), QStringLiteral("not_implemented"));
    }

    // Issue #4:  status_waiting_without_publisher()  — mock MediaMTX HTTP
    // Issue #5:  message_roundtrips_single_base64()   — mock delivery events
    // Issue #11: station_expires_after_ttl()          — injectable clock, advance > 45s
    // Issue #13: play_pause_resume_stop_states()       — mock QProcess
};

QTEST_MAIN(TestRadio)
#include "test_radio.moc"
