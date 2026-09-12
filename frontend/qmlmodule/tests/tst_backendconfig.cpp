#include "frontend/qmlmodule/backendconfig.h"

#include "daemon/src/config.h"

#include <QDBusConnection>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <algorithm>

namespace {

QString rawSettingsFileContents()
{
    const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                             QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
    QFile file(settings.fileName());
    const bool opened = file.open(QIODevice::ReadOnly | QIODevice::Text);
    Q_ASSERT(opened);
    return QString::fromUtf8(file.readAll());
}

// Writes the literal ini text directly, bypassing QSettings entirely --
// same reason as daemon/tests/tst_config.cpp's helper of the same shape:
// a same-process QSettings::setValue(key, QStringList()) does not
// round-trip through the `@Invalid()` literal a genuinely fresh read
// would see (Qt's in-process QConfFile cache hands a same-process reader
// the pre-serialization valid-empty variant instead).
void writeRawSettingsFileContents(const QByteArray &content)
{
    const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                             QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
    QFile file(settings.fileName());
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
    Q_ASSERT(opened);
    file.write(content);
}

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

    // Q24 / DESIGN.md decision 67: players/blacklist and
    // filter/musicUrlPrefixes must distinguish "unset" (-> built-in
    // default), "explicitly emptied" (-> empty, not the default) and
    // "populated" (-> the stored value), and the write side must never
    // produce the literal `@Invalid()`. See A.2-A.4 in SPEC.md.

    void blacklistUnsetUsesBuiltInDefault()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        QCOMPARE(config.serviceBlacklist(),
                 QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*"));
    }

    void blacklistExplicitlyEmptyStaysEmptyAcrossRestartAndWritesNoInvalidLiteral()
    {
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            config.setServiceBlacklist(QString());
            QVERIFY(config.save());
        }
        QVERIFY(!rawSettingsFileContents().contains(QStringLiteral("@Invalid()")));
        auto restarted = shellConfig(QStringLiteral("exit 0"));
        QVERIFY(restarted.serviceBlacklist().isEmpty());
    }

    void blacklistPopulatedValueRoundTrips()
    {
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            config.setServiceBlacklist(QStringLiteral("org.mpris.MediaPlayer2.custom.*"));
            QVERIFY(config.save());
        }
        auto restarted = shellConfig(QStringLiteral("exit 0"));
        QCOMPARE(restarted.serviceBlacklist(), QStringLiteral("org.mpris.MediaPlayer2.custom.*"));
    }

    void musicUrlPrefixesUnsetUsesBuiltInDefault()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        QCOMPARE(config.musicUrlPrefixes(),
                 QStringLiteral("https://music.163.com/\nhttp://music.163.com/"));
    }

    void musicUrlPrefixesExplicitlyEmptyStaysEmptyAcrossRestartAndWritesNoInvalidLiteral()
    {
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            config.setMusicUrlPrefixes(QString());
            QVERIFY(config.save());
        }
        QVERIFY(!rawSettingsFileContents().contains(QStringLiteral("@Invalid()")));
        auto restarted = shellConfig(QStringLiteral("exit 0"));
        QVERIFY(restarted.musicUrlPrefixes().isEmpty());
    }

    void musicUrlPrefixesPopulatedValueRoundTrips()
    {
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            config.setMusicUrlPrefixes(QStringLiteral("https://example.invalid/"));
            QVERIFY(config.save());
        }
        auto restarted = shellConfig(QStringLiteral("exit 0"));
        QCOMPARE(restarted.musicUrlPrefixes(), QStringLiteral("https://example.invalid/"));
    }

    // A.3.5: providers/order and providers/enabled keep their pre-existing
    // "empty means: use the built-in default" semantics, but the write side
    // must stop producing `@Invalid()` too. Verified against the daemon's
    // own Config reading the same file, so both sides are proven to still
    // agree -- not just BackendConfig in isolation.

    void emptyProviderOrderWritesNoInvalidLiteralAndDaemonStillFallsBackToBuiltIn()
    {
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            config.setProviderOrder({});
            QVERIFY(config.save());
        }
        QVERIFY(!rawSettingsFileContents().contains(QStringLiteral("@Invalid()")));
        // Asserted against the accessor, not a hard-coded three-element
        // literal: C2 appends "qq" to it, and a literal here would
        // silently drift stale once that merges.
        QCOMPARE(PlasmaLyrics::Config().providerOrder(),
                 PlasmaLyrics::Config::builtInProviderOrder());
    }

    void emptyEnabledProvidersWritesNoInvalidLiteralAndDaemonStillFallsBackToBuiltIn()
    {
        const QStringList customOrder{QStringLiteral("amll"), QStringLiteral("local"),
                                      QStringLiteral("netease")};
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            config.setProviderOrder(customOrder);
            config.setEnabledProviders({});
            QVERIFY(config.save());
        }
        // qa-a-2: checking daemonConfig's *behaviour* below is not the
        // same as checking what KeepPresentAsBlank actually put on disk --
        // reading the raw bytes is what rules out the empty-QStringList-
        // round-trips-to-@Invalid() failure this policy exists to avoid.
        const QString rawAfterSave = rawSettingsFileContents();
        QVERIFY(!rawAfterSave.contains(QStringLiteral("@Invalid()")));
        const QStringList rawLinesAfterSave = rawAfterSave.split(QLatin1Char('\n'));
        QVERIFY(std::any_of(rawLinesAfterSave.cbegin(), rawLinesAfterSave.cend(),
                            [](const QString &line) {
            return line.trimmed() == QStringLiteral("enabled=");
        }));
        PlasmaLyrics::Config daemonConfig;
        QCOMPARE(daemonConfig.providerOrder(), customOrder);
        // Matches Config::enabledProviderOrder()'s pre-existing behaviour:
        // an empty selection falls all the way back to the hard-coded
        // built-in order, not to the (customised) providers/order above --
        // see tst_config's emptyEnabledSetFallsBackToBuiltInOrder(), which
        // this mirrors on the write side. Asserted against the accessor,
        // not a hard-coded literal, since C2 appends "qq" to it.
        const QStringList enabled = daemonConfig.enabledProviderOrder();
        QCOMPARE(enabled, PlasmaLyrics::Config::builtInProviderOrder());
        // Load-bearing on its own: pins that the result is the built-in
        // default specifically, not merely the seeded custom order above
        // (which qa-a-2's mutation -- present-but-drained providers/enabled
        // treated as absent -- would otherwise return instead).
        QVERIFY(enabled != customOrder);
    }

    // filter/platforms uses the same three-state marker machinery as the
    // two Q24 keys (DESIGN.md decision 67); its migration target differs,
    // covered separately below.

    void platformsUnsetUsesBuiltInDefault()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        QVERIFY(config.platformNetease());
        QVERIFY(config.platformApple());
    }

    void platformsExplicitlyEmptyStaysEmptyAcrossRestartAndWritesNoInvalidLiteral()
    {
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            config.setPlatformNetease(false);
            config.setPlatformApple(false);
            QVERIFY(config.save());
        }
        QVERIFY(!rawSettingsFileContents().contains(QStringLiteral("@Invalid()")));
        auto restarted = shellConfig(QStringLiteral("exit 0"));
        QVERIFY(!restarted.platformNetease());
        QVERIFY(!restarted.platformApple());
    }

    void platformsPopulatedValueRoundTrips()
    {
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            config.setPlatformNetease(true);
            config.setPlatformApple(false);
            QVERIFY(config.save());
        }
        auto restarted = shellConfig(QStringLiteral("exit 0"));
        QVERIFY(restarted.platformNetease());
        QVERIFY(!restarted.platformApple());
    }

    // A.3b: a leaked `filter/platforms=@Invalid()` migrates to explicitly
    // empty, not to the built-in default -- the only way that literal gets
    // written is a user deliberately unchecking both platforms, so
    // migration must not silently re-enable them.
    void migrationOfLeakedPlatformsInvalidLiteralPreservesExplicitEmptyNotDefault()
    {
        writeRawSettingsFileContents(QByteArrayLiteral("[filter]\nplatforms=@Invalid()\n"));
        auto config = shellConfig(QStringLiteral("exit 0"));
        QVERIFY(!config.platformNetease());
        QVERIFY(!config.platformApple());
        QVERIFY(!rawSettingsFileContents().contains(QStringLiteral("@Invalid()")));
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
