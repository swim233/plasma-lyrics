#include "frontend/qmlmodule/backendconfig.h"

#include <QDBusConnection>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

namespace {

class FakeControlService final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.swim233.PlasmaLyrics.Control")

public Q_SLOTS:
    QStringList AvailableProviders() const
    {
        return {QStringLiteral("local"), QStringLiteral("amll")};
    }
};

BackendConfig shellConfig(const QString &command)
{
    return BackendConfig(QStringLiteral("/bin/sh"),
                         {QStringLiteral("-c"), command});
}

} // namespace

class BackendConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"),
                           QStringLiteral("plasma-lyricsd"));
        settings.clear();
        settings.sync();
    }

    void persistsProviderOrderAndAmllSettings()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"),
                           QStringLiteral("plasma-lyricsd"));
        settings.clear();
        settings.sync();
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            QCOMPARE(config.providerOrder(),
                     QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                                  QStringLiteral("amll")}));
            config.setProviderOrder({QStringLiteral("amll"), QStringLiteral("unknown"),
                                     QStringLiteral("local"), QStringLiteral("netease")});
            config.setEnabledProviders({QStringLiteral("local"), QStringLiteral("amll"),
                                        QStringLiteral("unknown")});
            config.setAmllIndexUrl(QStringLiteral("https://example.invalid/index.jsonl"));
            config.setAmllContentBaseUrl(QStringLiteral("https://example.invalid/content/"));
            config.setAmllTimeoutMs(12345);
            config.setAmllIndexRefreshHours(36);
            QVERIFY(config.dirty());
            QVERIFY(config.save());
            QVERIFY(!config.dirty());
        }
        {
            auto restored = shellConfig(QStringLiteral("exit 0"));
            QCOMPARE(restored.providerOrder(),
                     QStringList({QStringLiteral("amll"), QStringLiteral("unknown"),
                                  QStringLiteral("local"), QStringLiteral("netease")}));
            QCOMPARE(restored.enabledProviders(),
                     QStringList({QStringLiteral("local"), QStringLiteral("amll"),
                                  QStringLiteral("unknown")}));
            QCOMPARE(restored.amllIndexUrl(),
                     QStringLiteral("https://example.invalid/index.jsonl"));
            QCOMPARE(restored.amllContentBaseUrl(),
                     QStringLiteral("https://example.invalid/content/"));
            QCOMPARE(restored.amllTimeoutMs(), 12345);
            QCOMPARE(restored.amllIndexRefreshHours(), 36);
        }
    }

    void debugLoggingDefaultsToFalse()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        QCOMPARE(config.debugLoggingEnabled(), false);
    }

    void debugLoggingReadsBackWrittenValue()
    {
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            config.setDebugLoggingEnabled(true);
            QVERIFY(config.dirty());
            QVERIFY(config.save());
        }
        auto restored = shellConfig(QStringLiteral("exit 0"));
        QCOMPARE(restored.debugLoggingEnabled(), true);
    }

    void proxyModeDefaultsToNone()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        QCOMPARE(config.proxyMode(), QStringLiteral("none"));
        QCOMPARE(config.proxyUrl(), QString());
    }

    void proxyModeAndUrlReadBackWrittenValues()
    {
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            config.setProxyMode(QStringLiteral("manual"));
            config.setProxyUrl(QStringLiteral("socks5://127.0.0.1:1080"));
            QVERIFY(config.dirty());
            QVERIFY(config.save());
        }
        auto restored = shellConfig(QStringLiteral("exit 0"));
        QCOMPARE(restored.proxyMode(), QStringLiteral("manual"));
        QCOMPARE(restored.proxyUrl(), QStringLiteral("socks5://127.0.0.1:1080"));
    }

    void proxyUrlErrorIsEmptyForAValidAddress()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        QVERIFY(config.proxyUrlError(QStringLiteral("socks5://127.0.0.1:1080")).isEmpty());
        QVERIFY(config.proxyUrlError(QStringLiteral("http://proxy.example.test:8080")).isEmpty());
    }

    void proxyUrlErrorIsNonEmptyForInvalidAddresses()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        QVERIFY(!config.proxyUrlError(QStringLiteral("https://host:443")).isEmpty());
        QVERIFY(!config.proxyUrlError(QStringLiteral("http://host")).isEmpty());
        QVERIFY(!config.proxyUrlError(QStringLiteral("http://:80")).isEmpty());
        QVERIFY(!config.proxyUrlError(QStringLiteral("http://host:80/path")).isEmpty());
        QVERIFY(!config.proxyUrlError(QString()).isEmpty());
    }

    void saveRejectsAnInvalidManualAddressAndPersistsNothing()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"),
                           QStringLiteral("plasma-lyricsd"));
        settings.clear();
        settings.sync();

        auto config = shellConfig(QStringLiteral("exit 0"));
        config.setProxyMode(QStringLiteral("manual"));
        config.setProxyUrl(QStringLiteral("not a url"));
        config.setLocalLyricsDirectory(QStringLiteral("/tmp/should-not-be-saved"));
        QVERIFY(!config.save());
        QVERIFY(config.dirty());
        QVERIFY(!settings.contains(QStringLiteral("network/proxyMode")));
        QVERIFY(!settings.contains(QStringLiteral("providers/local/directory")));
    }

    void invalidAddressDoesNotBlockSaveOutsideManualMode()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        config.setProxyUrl(QStringLiteral("not a url"));
        QCOMPARE(config.proxyMode(), QStringLiteral("none"));
        QVERIFY(config.save());

        config.setProxyMode(QStringLiteral("system"));
        QVERIFY(config.save());
    }

    void discoversAvailableProvidersFromDaemon()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeControlService service;
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"),
                                   &service, QDBusConnection::ExportAllSlots));

        auto config = shellConfig(QStringLiteral("exit 0"));
        QVERIFY(!config.providerDiscoveryFallback());
        const auto entries = config.providerEntries();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("local"));
        QCOMPARE(entries.at(1).toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("amll"));

        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
    }

    void fallsBackWhenDaemonIsUnavailable()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        QVERIFY(config.providerDiscoveryFallback());
        QCOMPARE(config.providerEntries().size(), 3);
    }

    void reordersVisibleProvidersWithoutDroppingUnknownOnes()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"),
                           QStringLiteral("plasma-lyricsd"));
        settings.clear();
        settings.setValue(QStringLiteral("providers/order"),
                          QStringList({QStringLiteral("local"), QStringLiteral("future"),
                                       QStringLiteral("netease"), QStringLiteral("amll")}));
        settings.sync();
        auto config = shellConfig(QStringLiteral("exit 0"));
        QVERIFY(config.moveProvider(0, 2));
        QVERIFY(config.providerOrder().contains(QStringLiteral("future")));
        QVERIFY(config.save());
        QCOMPARE(settings.value(QStringLiteral("providers/order")).toStringList(),
                 config.providerOrder());
    }

    void refusesToDisableTheLastVisibleProvider()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        config.setEnabledProviders({QStringLiteral("local")});
        QVERIFY(!config.setProviderEnabled(QStringLiteral("local"), false));
        QVERIFY(config.enabledProviders().contains(QStringLiteral("local")));
    }

    void disablingAndReenablingDoesNotChangePriority()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        const auto before = config.providerOrder();
        QVERIFY(config.setProviderEnabled(QStringLiteral("amll"), false));
        QVERIFY(config.setProviderEnabled(QStringLiteral("amll"), true));
        QCOMPARE(config.providerOrder(), before);
    }

    void disabledAmllRemainsVisibleAndCanBeReenabledAfterRestart()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeControlService service;
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"),
                                   &service, QDBusConnection::ExportAllSlots));

        QStringList savedOrder;
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            QCOMPARE(config.providerEntries().size(), 2);
            QVERIFY(config.moveProvider(1, 0));
            savedOrder = config.providerOrder();
            QVERIFY(config.setProviderEnabled(QStringLiteral("amll"), false));
            QVERIFY(config.save());
        }
        {
            auto restarted = shellConfig(QStringLiteral("exit 0"));
            QCOMPARE(restarted.providerOrder(), savedOrder);
            const auto entries = restarted.providerEntries();
            QCOMPARE(entries.size(), 2);
            QCOMPARE(entries.at(0).toMap().value(QStringLiteral("id")).toString(),
                     QStringLiteral("amll"));
            QCOMPARE(entries.at(0).toMap().value(QStringLiteral("enabled")).toBool(), false);
            QCOMPARE(entries.at(1).toMap().value(QStringLiteral("id")).toString(),
                     QStringLiteral("local"));
            QVERIFY(restarted.setProviderEnabled(QStringLiteral("amll"), true));
            QCOMPARE(restarted.providerOrder(), savedOrder);
            QVERIFY(restarted.save());
        }
        {
            auto restartedAgain = shellConfig(QStringLiteral("exit 0"));
            QCOMPARE(restartedAgain.providerOrder(), savedOrder);
            const auto entries = restartedAgain.providerEntries();
            QCOMPARE(entries.at(0).toMap().value(QStringLiteral("id")).toString(),
                     QStringLiteral("amll"));
            QCOMPARE(entries.at(0).toMap().value(QStringLiteral("enabled")).toBool(), true);
        }

        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
    }

    void reportsSuccessfulRestart()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        QVERIFY(config.restartService());
        QVERIFY(config.restartInProgress());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(config.restartState(), BackendConfig::RestartSucceeded);
        QVERIFY(!config.restartInProgress());
        QVERIFY(config.restartError().isEmpty());
        QCOMPARE(finished.first().first().toBool(), true);
    }

    void reportsNonZeroExit()
    {
        auto config = shellConfig(QStringLiteral("exit 23"));
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        QVERIFY(config.restartService());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(config.restartState(), BackendConfig::RestartFailed);
        QVERIFY(config.restartError().contains(QStringLiteral("23")));
        QCOMPARE(finished.first().first().toBool(), false);
    }

    void failedRestartCanBeRetried()
    {
        auto config = shellConfig(QStringLiteral("exit 9"));
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        QVERIFY(config.restartService());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(config.restartState(), BackendConfig::RestartFailed);
        QVERIFY(config.restartService());
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(config.restartState(), BackendConfig::RestartFailed);
    }

    void reportsStartFailure()
    {
        BackendConfig config(QStringLiteral("/does/not/exist/plasma-lyrics-restart"), {});
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        QVERIFY(config.restartService());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(config.restartState(), BackendConfig::RestartFailed);
        QVERIFY(!config.restartError().isEmpty());
        QVERIFY(!config.restartInProgress());
    }

    void reportsCrash()
    {
        auto config = shellConfig(QStringLiteral("kill -SEGV $$"));
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        QVERIFY(config.restartService());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(config.restartState(), BackendConfig::RestartFailed);
        QVERIFY(config.restartError().contains(QStringLiteral("crashed")));
    }

    void ignoresConcurrentRestart()
    {
        auto config = shellConfig(QStringLiteral("sleep 0.1; exit 0"));
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        QVERIFY(config.restartService());
        QVERIFY(!config.restartService());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(config.restartState(), BackendConfig::RestartSucceeded);
    }

    void destroyingWhileRunningIsSafe()
    {
        QElapsedTimer elapsed;
        elapsed.start();
        auto *config = new BackendConfig(QStringLiteral("/bin/sleep"),
                                         {QStringLiteral("10")});
        QVERIFY(config->restartService());
        delete config;
        QVERIFY(elapsed.elapsed() < 2000);
    }
};

QTEST_GUILESS_MAIN(BackendConfigTest)
#include "tst_backendconfig.moc"
