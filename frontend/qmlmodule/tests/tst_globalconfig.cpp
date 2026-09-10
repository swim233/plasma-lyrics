#include "frontend/qmlmodule/globalconfig.h"

#include <QDBusConnection>
#include <QStandardPaths>
#include <QTest>

class FakeOffsetControl final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.swim233.PlasmaLyrics.Control")

public:
    int refreshCount = 0;

public Q_SLOTS:
    QString RefreshGlobalOffset()
    {
        ++refreshCount;
        return {};
    }
};

class GlobalConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void successfulSaveAsksDaemonToRepublishSnapshots()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeOffsetControl control;
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"),
                                   &control, QDBusConnection::ExportAllSlots));

        GlobalConfig config;
        config.setEnabled(!config.enabled());
        config.setOffsetMs(321);
        QVERIFY(config.save());
        QTRY_COMPARE(control.refreshCount, 1);

        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
    }
};

QTEST_GUILESS_MAIN(GlobalConfigTest)
#include "tst_globalconfig.moc"
