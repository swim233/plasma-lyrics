#include "frontend/qmlmodule/backendconfig.h"

#include "core/log/configlog.h"
#include "daemon/src/config.h"
#include "frontend/qmlmodule/settingslog.h"

#include <QDBusConnection>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <algorithm>
#include <functional>

#include <unistd.h>

// A data-driven test below feeds one of these through QTest's data table
// per key save() writes, so it needs to be a registered meta type.
Q_DECLARE_METATYPE(std::function<void(BackendConfig &)>)

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

// Temporarily strips write permission from a file, restoring the original
// permissions on scope exit regardless of how the scope is left (including
// an early QVERIFY return), so a failed assertion in the middle of a test
// cannot leave the shared settings file read-only for whatever runs next.
class ScopedReadOnlyFile
{
public:
    explicit ScopedReadOnlyFile(QString path)
        : m_path(std::move(path))
        , m_original(QFileInfo(m_path).permissions())
    {
        QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::ReadGroup
                                          | QFileDevice::ReadOther);
    }

    ~ScopedReadOnlyFile()
    {
        QFile::setPermissions(m_path, m_original);
    }

private:
    QString m_path;
    QFileDevice::Permissions m_original;
};

// Mirrors daemon/tests/tst_controlservice.cpp's helper of the same shape,
// extended to also record severity: the config-change lines split between
// qCInfo and qCWarning depending on which report* call produced them, and
// several tests below need to tell the two apart.
QList<QPair<QtMsgType, QString>> *capturedMessages = nullptr;

void captureMessages(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (capturedMessages && (type == QtInfoMsg || type == QtWarningMsg)) {
        capturedMessages->append({type, message});
    }
}

class MessageCapture
{
public:
    MessageCapture()
        : m_previous(qInstallMessageHandler(captureMessages))
    {
        capturedMessages = &m_messages;
    }

    ~MessageCapture()
    {
        capturedMessages = nullptr;
        qInstallMessageHandler(m_previous);
    }

    const QList<QPair<QtMsgType, QString>> &messages() const { return m_messages; }

private:
    QList<QPair<QtMsgType, QString>> m_messages;
    QtMessageHandler m_previous;
};

class FakeControlService final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.swim233.PlasmaLyrics.Control")

public:
    struct ConfigChangeCall {
        QString store;
        QString applet;
        QString form;
        QString key;
        QString oldValue;
        QString newValue;
    };

    QList<ConfigChangeCall> configChanges;
    QList<QPair<QString, QString>> saveFailedCalls;
    int restartRequestedCalls = 0;
    QList<QPair<bool, QString>> restartFinishedCalls;

public Q_SLOTS:
    QStringList AvailableProviders() const
    {
        return {QStringLiteral("local"), QStringLiteral("amll")};
    }

    void NoteConfigChange(const QString &store, const QString &applet, const QString &form,
                         const QString &key, const QString &oldValue, const QString &newValue)
    {
        configChanges.append({store, applet, form, key, oldValue, newValue});
    }

    void NoteSaveFailed(const QString &store, const QString &reason)
    {
        saveFailedCalls.append({store, reason});
    }

    void NoteRestartRequested()
    {
        ++restartRequestedCalls;
    }

    void NoteRestartFinished(bool success, const QString &error)
    {
        restartFinishedCalls.append({success, error});
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
                                  QStringLiteral("amll"), QStringLiteral("qq")}));
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
                                  QStringLiteral("local"), QStringLiteral("netease"),
                                  QStringLiteral("qq")}));
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

    // DESIGN.md decision 75: save() reports exactly the keys whose rendered
    // value actually changed, and nothing for a no-op save.

    void saveReportsOnlyTheKeysThatActuallyChanged()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        config.setFilterCredits(false);
        config.setNetworkTimeoutMs(9000);
        QVERIFY(config.dirty());

        MessageCapture capture;
        QVERIFY(config.save());
        QStringList changeLines;
        for (const auto &entry : capture.messages()) {
            if (entry.second.startsWith(QStringLiteral("config changed"))) {
                changeLines.append(entry.second);
            }
        }
        QCOMPARE(changeLines.size(), 2);
        QVERIFY(changeLines.contains(PlasmaLyrics::configChangedLine(
            {.store = QStringLiteral("ini"),
             .key = QStringLiteral("lyrics/filterLeadingCredits"),
             .oldValue = QStringLiteral("true"),
             .newValue = QStringLiteral("false")})));
        QVERIFY(changeLines.contains(PlasmaLyrics::configChangedLine(
            {.store = QStringLiteral("ini"),
             .key = QStringLiteral("providers/netease/timeoutMs"),
             .oldValue = QStringLiteral("4000"),
             .newValue = QStringLiteral("9000")})));

        MessageCapture second;
        QVERIFY(config.save());
        for (const auto &entry : second.messages()) {
            QVERIFY(!entry.second.startsWith(QStringLiteral("config changed")));
        }
    }

    void proxyUrlWithCredentialsLogsOnlySchemeHostPort()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        config.setProxyMode(QStringLiteral("manual"));
        config.setProxyUrl(QStringLiteral("socks5://user:secret@proxy.example.test:1080"));

        MessageCapture capture;
        QVERIFY(config.save());

        const QString rendered = QStringLiteral("socks5://proxy.example.test:1080");
        bool found = false;
        for (const auto &entry : capture.messages()) {
            if (entry.second.startsWith(QStringLiteral("config changed"))
                && entry.second.contains(QStringLiteral("key=network/proxyUrl"))) {
                found = true;
                QVERIFY(entry.second.contains(rendered));
                QVERIFY(!entry.second.contains(QStringLiteral("secret")));
                QVERIFY(!entry.second.contains(QStringLiteral("user")));
            }
        }
        QVERIFY(found);
    }

    // Reviewer-confirmed bug: comparing the redacted renderings to decide
    // whether network/proxyUrl changed missed a credentials-only edit
    // entirely (same host:port -> same redacted string -> "unchanged"),
    // even though the ini file was rewritten. The fix compares the raw
    // strings instead, so this must still produce a line -- with an
    // old/new that are identical once redacted, since nothing else can be
    // shown about a credentials-only change.
    void proxyUrlCredentialsOnlyChangeStillReportsAndForwardsAnIdenticalLine()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeControlService service;
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"),
                                   &service, QDBusConnection::ExportAllSlots));

        auto config = shellConfig(QStringLiteral("exit 0"));
        config.setProxyMode(QStringLiteral("manual"));
        config.setProxyUrl(QStringLiteral("socks5://alice:pw1@host.example.test:1080"));
        QVERIFY(config.save());

        config.setProxyUrl(QStringLiteral("socks5://bob:pw2@host.example.test:1080"));
        MessageCapture capture;
        QVERIFY(config.save());

        const QString rendered = QStringLiteral("socks5://host.example.test:1080");
        QStringList proxyLines;
        for (const auto &entry : capture.messages()) {
            if (entry.second.startsWith(QStringLiteral("config changed"))
                && entry.second.contains(QStringLiteral("key=network/proxyUrl"))) {
                proxyLines.append(entry.second);
            }
        }
        QCOMPARE(proxyLines.size(), 1);
        QCOMPARE(proxyLines.first(),
                 PlasmaLyrics::configChangedLine({.store = QStringLiteral("ini"),
                                                  .key = QStringLiteral("network/proxyUrl"),
                                                  .oldValue = rendered,
                                                  .newValue = rendered}));

        QTRY_VERIFY(std::any_of(service.configChanges.cbegin(), service.configChanges.cend(),
                                [&rendered](const FakeControlService::ConfigChangeCall &call) {
            return call.key == QStringLiteral("network/proxyUrl") && call.oldValue == rendered
                && call.newValue == rendered;
        }));

        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
    }

    // DESIGN.md decision 75: every key save() writes must be independently
    // covered by an exact-line assertion -- a probe that deleted the
    // record() call for one key (players/blacklist) left this file green
    // before this test existed. One row per key (two for each of the
    // three-state keys, to also cover clearing to empty).
    void singleKeyChangeReportsExactlyOneLine_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("expectedOld");
        QTest::addColumn<QString>("expectedNew");
        QTest::addColumn<std::function<void(BackendConfig &)>>("mutate");

        using Mutator = std::function<void(BackendConfig &)>;

        QTest::newRow("players/blacklist populated")
            << QStringLiteral("players/blacklist")
            << QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*")
            << QStringLiteral("org.mpris.MediaPlayer2.custom.*")
            << Mutator([](BackendConfig &c) {
                   c.setServiceBlacklist(QStringLiteral("org.mpris.MediaPlayer2.custom.*"));
               });
        QTest::newRow("players/blacklist cleared to empty")
            << QStringLiteral("players/blacklist")
            << QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*")
            << QString()
            << Mutator([](BackendConfig &c) { c.setServiceBlacklist(QString()); });

        QTest::newRow("filter/musicUrlPrefixes populated")
            << QStringLiteral("filter/musicUrlPrefixes")
            << QStringLiteral("https://music.163.com/,http://music.163.com/")
            << QStringLiteral("https://example.invalid/")
            << Mutator([](BackendConfig &c) {
                   c.setMusicUrlPrefixes(QStringLiteral("https://example.invalid/"));
               });
        QTest::newRow("filter/musicUrlPrefixes cleared to empty")
            << QStringLiteral("filter/musicUrlPrefixes")
            << QStringLiteral("https://music.163.com/,http://music.163.com/")
            << QString()
            << Mutator([](BackendConfig &c) { c.setMusicUrlPrefixes(QString()); });

        QTest::newRow("filter/metadataHeuristic")
            << QStringLiteral("filter/metadataHeuristic")
            << QStringLiteral("true")
            << QStringLiteral("false")
            << Mutator([](BackendConfig &c) { c.setMetadataHeuristic(false); });

        QTest::newRow("filter/platforms narrowed")
            << QStringLiteral("filter/platforms")
            << QStringLiteral("netease,apple")
            << QStringLiteral("apple")
            << Mutator([](BackendConfig &c) { c.setPlatformNetease(false); });
        QTest::newRow("filter/platforms cleared to empty")
            << QStringLiteral("filter/platforms")
            << QStringLiteral("netease,apple")
            << QString()
            << Mutator([](BackendConfig &c) {
                   c.setPlatformNetease(false);
                   c.setPlatformApple(false);
               });

        QTest::newRow("lyrics/filterLeadingCredits")
            << QStringLiteral("lyrics/filterLeadingCredits")
            << QStringLiteral("true")
            << QStringLiteral("false")
            << Mutator([](BackendConfig &c) { c.setFilterCredits(false); });

        QTest::newRow("providers/netease/baseUrl")
            << QStringLiteral("providers/netease/baseUrl")
            << QStringLiteral("https://music.163.com")
            << QStringLiteral("https://example.invalid")
            << Mutator([](BackendConfig &c) {
                   c.setNeteaseBaseUrl(QStringLiteral("https://example.invalid"));
               });

        QTest::newRow("providers/netease/timeoutMs")
            << QStringLiteral("providers/netease/timeoutMs")
            << QStringLiteral("4000")
            << QStringLiteral("9000")
            << Mutator([](BackendConfig &c) { c.setNetworkTimeoutMs(9000); });

        QTest::newRow("providers/order")
            << QStringLiteral("providers/order")
            << QStringLiteral("local,netease,amll,qq")
            << QStringLiteral("amll,local,netease,qq")
            << Mutator([](BackendConfig &c) {
                   c.setProviderOrder({QStringLiteral("amll"), QStringLiteral("local"),
                                       QStringLiteral("netease"), QStringLiteral("qq")});
               });

        QTest::newRow("providers/enabled")
            << QStringLiteral("providers/enabled")
            << QStringLiteral("local,netease,amll,qq")
            << QStringLiteral("local,amll")
            << Mutator([](BackendConfig &c) {
                   c.setEnabledProviders({QStringLiteral("local"), QStringLiteral("amll")});
               });

        const QString defaultLocalDirectory =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                + QStringLiteral("/plasma-lyrics/lyrics");
        QTest::newRow("providers/local/directory")
            << QStringLiteral("providers/local/directory")
            << defaultLocalDirectory
            << QStringLiteral("/tmp/custom-lyrics")
            << Mutator([](BackendConfig &c) {
                   c.setLocalLyricsDirectory(QStringLiteral("/tmp/custom-lyrics"));
               });

        QTest::newRow("providers/amll/indexUrl")
            << QStringLiteral("providers/amll/indexUrl")
            << QStringLiteral("https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/"
                              "metadata/raw-lyrics-index.jsonl")
            << QStringLiteral("https://example.invalid/index.jsonl")
            << Mutator([](BackendConfig &c) {
                   c.setAmllIndexUrl(QStringLiteral("https://example.invalid/index.jsonl"));
               });

        QTest::newRow("providers/amll/contentBaseUrl")
            << QStringLiteral("providers/amll/contentBaseUrl")
            << QStringLiteral("https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/")
            << QStringLiteral("https://example.invalid/content/")
            << Mutator([](BackendConfig &c) {
                   c.setAmllContentBaseUrl(QStringLiteral("https://example.invalid/content/"));
               });

        QTest::newRow("providers/amll/timeoutMs")
            << QStringLiteral("providers/amll/timeoutMs")
            << QStringLiteral("8000")
            << QStringLiteral("12345")
            << Mutator([](BackendConfig &c) { c.setAmllTimeoutMs(12345); });

        QTest::newRow("providers/amll/indexRefreshHours")
            << QStringLiteral("providers/amll/indexRefreshHours")
            << QStringLiteral("24")
            << QStringLiteral("36")
            << Mutator([](BackendConfig &c) { c.setAmllIndexRefreshHours(36); });

        QTest::newRow("logging/fileEnabled")
            << QStringLiteral("logging/fileEnabled")
            << QStringLiteral("false")
            << QStringLiteral("true")
            << Mutator([](BackendConfig &c) { c.setFileLoggingEnabled(true); });

        const QString defaultLogFilePath =
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                + QStringLiteral("/plasma-lyrics/plasma-lyricsd.log");
        QTest::newRow("logging/filePath")
            << QStringLiteral("logging/filePath")
            << defaultLogFilePath
            << QStringLiteral("/tmp/custom.log")
            << Mutator([](BackendConfig &c) { c.setLogFilePath(QStringLiteral("/tmp/custom.log")); });

        QTest::newRow("logging/debug")
            << QStringLiteral("logging/debug")
            << QStringLiteral("false")
            << QStringLiteral("true")
            << Mutator([](BackendConfig &c) { c.setDebugLoggingEnabled(true); });

        QTest::newRow("network/proxyMode")
            << QStringLiteral("network/proxyMode")
            << QStringLiteral("none")
            << QStringLiteral("system")
            << Mutator([](BackendConfig &c) { c.setProxyMode(QStringLiteral("system")); });

        QTest::newRow("network/proxyUrl")
            << QStringLiteral("network/proxyUrl")
            << QString()
            << QStringLiteral("socks5://127.0.0.1:1080")
            << Mutator([](BackendConfig &c) {
                   c.setProxyUrl(QStringLiteral("socks5://127.0.0.1:1080"));
               });
    }

    void singleKeyChangeReportsExactlyOneLine()
    {
        QFETCH(QString, key);
        QFETCH(QString, expectedOld);
        QFETCH(QString, expectedNew);
        QFETCH(std::function<void(BackendConfig &)>, mutate);

        auto config = shellConfig(QStringLiteral("exit 0"));
        mutate(config);

        MessageCapture capture;
        QVERIFY(config.save());
        QStringList changeLines;
        for (const auto &entry : capture.messages()) {
            if (entry.second.startsWith(QStringLiteral("config changed"))) {
                changeLines.append(entry.second);
            }
        }
        QCOMPARE(changeLines.size(), 1);
        QCOMPARE(changeLines.first(),
                 PlasmaLyrics::configChangedLine({.store = QStringLiteral("ini"),
                                                  .key = key,
                                                  .oldValue = expectedOld,
                                                  .newValue = expectedNew}));
    }

    void invalidManualProxyReportsSaveFailedAndNoChangeLines()
    {
        auto config = shellConfig(QStringLiteral("exit 0"));
        config.setProxyMode(QStringLiteral("manual"));
        config.setProxyUrl(QStringLiteral("not a url"));

        MessageCapture capture;
        QVERIFY(!config.save());
        QCOMPARE(capture.messages().size(), 1);
        QCOMPARE(capture.messages().first().first, QtWarningMsg);
        QCOMPARE(capture.messages().first().second,
                 PlasmaLyrics::configSaveFailedLine(QStringLiteral("ini"),
                                                    QStringLiteral("proxy-url-invalid")));
    }

    void iniAccessErrorReportsSaveFailedAndNoChangeLines()
    {
        if (geteuid() == 0) {
            QSKIP("root ignores file permissions, so this cannot force an access error");
        }
        // The file has to exist first: a fresh, still-missing ini file
        // does not fail to sync just because its (non-existent) directory
        // entry is unwritable in a way distinguishable from other causes.
        {
            auto config = shellConfig(QStringLiteral("exit 0"));
            QVERIFY(config.save());
        }
        const QSettings probe(QSettings::IniFormat, QSettings::UserScope,
                              QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        ScopedReadOnlyFile readOnly(probe.fileName());

        auto config = shellConfig(QStringLiteral("exit 0"));
        config.setFilterCredits(!config.filterCredits());

        MessageCapture capture;
        QVERIFY(!config.save());

        bool sawWarning = false;
        for (const auto &entry : capture.messages()) {
            QVERIFY(!entry.second.startsWith(QStringLiteral("config changed")));
            if (entry.second == PlasmaLyrics::configSaveFailedLine(
                    QStringLiteral("ini"), QStringLiteral("ini-access-error"))) {
                sawWarning = true;
                QCOMPARE(entry.first, QtWarningMsg);
            }
        }
        QVERIFY(sawWarning);
    }

    void forwardsConfigChangesToDaemon()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeControlService service;
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"),
                                   &service, QDBusConnection::ExportAllSlots));

        auto config = shellConfig(QStringLiteral("exit 0"));
        config.setFilterCredits(false);
        config.setNetworkTimeoutMs(9000);
        QVERIFY(config.save());

        QTRY_COMPARE(service.configChanges.size(), 2);
        bool sawCredits = false;
        bool sawTimeout = false;
        for (const auto &call : service.configChanges) {
            QCOMPARE(call.store, QStringLiteral("ini"));
            QVERIFY(call.applet.isEmpty());
            QVERIFY(call.form.isEmpty());
            if (call.key == QStringLiteral("lyrics/filterLeadingCredits")) {
                sawCredits = true;
                QCOMPARE(call.oldValue, QStringLiteral("true"));
                QCOMPARE(call.newValue, QStringLiteral("false"));
            } else if (call.key == QStringLiteral("providers/netease/timeoutMs")) {
                sawTimeout = true;
                QCOMPARE(call.oldValue, QStringLiteral("4000"));
                QCOMPARE(call.newValue, QStringLiteral("9000"));
            }
        }
        QVERIFY(sawCredits);
        QVERIFY(sawTimeout);

        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
    }

    void forwardsSaveFailedToDaemon()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeControlService service;
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"),
                                   &service, QDBusConnection::ExportAllSlots));

        auto config = shellConfig(QStringLiteral("exit 0"));
        config.setProxyMode(QStringLiteral("manual"));
        config.setProxyUrl(QStringLiteral("not a url"));
        QVERIFY(!config.save());

        QTRY_VERIFY(!service.saveFailedCalls.isEmpty());
        QCOMPARE(service.saveFailedCalls.first().first, QStringLiteral("ini"));
        QCOMPARE(service.saveFailedCalls.first().second, QStringLiteral("proxy-url-invalid"));

        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
    }

    void restartReportsRequestedThenFinishedOnSuccess()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeControlService service;
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"),
                                   &service, QDBusConnection::ExportAllSlots));

        auto config = shellConfig(QStringLiteral("exit 0"));
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        MessageCapture capture;
        QVERIFY(config.restartService());
        QTRY_COMPARE(finished.size(), 1);

        QVERIFY(!capture.messages().isEmpty());
        QCOMPARE(capture.messages().first().first, QtInfoMsg);
        QCOMPARE(capture.messages().first().second, PlasmaLyrics::restartRequestedLine());
        QVERIFY(std::any_of(capture.messages().cbegin(), capture.messages().cend(),
                            [](const QPair<QtMsgType, QString> &entry) {
            return entry.first == QtInfoMsg
                && entry.second == PlasmaLyrics::restartFinishedLine(true, QString());
        }));

        QTRY_COMPARE(service.restartRequestedCalls, 1);
        QTRY_VERIFY(!service.restartFinishedCalls.isEmpty());
        QCOMPARE(service.restartFinishedCalls.first().first, true);
        QCOMPARE(service.restartFinishedCalls.first().second, QString());

        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
    }

    void restartReportsRequestedThenFinishedOnFailure()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeControlService service;
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"),
                                   &service, QDBusConnection::ExportAllSlots));

        auto config = shellConfig(QStringLiteral("exit 23"));
        QSignalSpy finished(&config, &BackendConfig::restartFinished);
        MessageCapture capture;
        QVERIFY(config.restartService());
        QTRY_COMPARE(finished.size(), 1);

        QVERIFY(std::any_of(capture.messages().cbegin(), capture.messages().cend(),
                            [](const QPair<QtMsgType, QString> &entry) {
            return entry.first == QtWarningMsg
                && entry.second.startsWith(QStringLiteral("config restart finished result=failed"));
        }));

        QTRY_VERIFY(!service.restartFinishedCalls.isEmpty());
        QCOMPARE(service.restartFinishedCalls.first().first, false);
        QVERIFY(!service.restartFinishedCalls.first().second.isEmpty());

        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));
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
        QCOMPARE(config.providerEntries().size(), 4);
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
