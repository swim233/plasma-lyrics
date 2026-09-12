#include "daemon/src/config.h"

#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

using namespace PlasmaLyrics;

namespace {

QString settingsFilePath()
{
    const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                             QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
    return settings.fileName();
}

QString rawSettingsFileContents()
{
    QFile file(settingsFilePath());
    const bool opened = file.open(QIODevice::ReadOnly | QIODevice::Text);
    Q_ASSERT(opened);
    return QString::fromUtf8(file.readAll());
}

// Writes the literal ini text directly, bypassing QSettings entirely.
// Needed to reproduce a leaked `@Invalid()` literal faithfully: a
// QSettings::setValue(key, QStringList{}) done *within this same test
// process* does not round-trip through that literal the way a real daemon
// process reading a file some earlier process already left in that state
// does -- Qt's in-process QConfFile cache hands a fresh same-process
// QSettings instance the pre-serialization (valid, empty) variant instead
// of forcing a genuine reparse of the on-disk text (confirmed against the
// real Qt6 QSettings implementation, not assumed). Writing the raw text
// ourselves guarantees the only way to observe it is through a real parse.
void writeRawSettingsFileContents(const QByteArray &content)
{
    QFile file(settingsFilePath());
    const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
    Q_ASSERT(opened);
    file.write(content);
}

} // namespace

class ConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.clear();
        settings.sync();
    }

    void emptyOrderFallsBackToBuiltInOrder()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("providers/order"), QStringList{});
        settings.sync();
        QCOMPARE(Config().providerOrder(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                              QStringLiteral("amll"), QStringLiteral("qq")}));
    }

    void oldOrderIsUpgradedWithLocalFirst()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("providers/order"),
                          QStringList({QStringLiteral("netease"), QStringLiteral("amll")}));
        settings.sync();
        QCOMPARE(Config().providerOrder(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                              QStringLiteral("amll")}));
    }

    void enabledSetFiltersOnlyTheActiveChainWithoutChangingOrder()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("providers/order"),
                          QStringList({QStringLiteral("netease"), QStringLiteral("amll")}));
        settings.setValue(QStringLiteral("providers/enabled"),
                          QStringList({QStringLiteral("amll")}));
        settings.sync();
        const Config config;
        QCOMPARE(config.providerOrder(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                              QStringLiteral("amll")}));
        QCOMPARE(config.enabledProviderOrder(), QStringList({QStringLiteral("amll")}));
    }

    void emptyEnabledSetFallsBackToBuiltInOrder()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("providers/order"),
                          QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                                       QStringLiteral("amll")}));
        settings.setValue(QStringLiteral("providers/enabled"), QStringList{});
        settings.sync();
        const Config config;
        QCOMPARE(config.providerOrder(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                              QStringLiteral("amll")}));
        QCOMPARE(config.enabledProviderOrder(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                              QStringLiteral("amll"), QStringLiteral("qq")}));
    }

    // qa-a-2: the test above writes `providers/enabled` empty through
    // QSettings in this same process, which does not prove what a fresh
    // reader of the *file* sees for `EmptyStringListPolicy::KeepPresentAsBlank`'s
    // actual output (`enabled=`, a bare key, not `@Invalid()`). Reproduce
    // that exact on-disk shape directly via a raw file write and confirm
    // Config::enabledProviderOrder() still falls back to the built-in
    // default from it, matching the behaviour above byte-for-behaviour.
    void bareEnabledKeyFallsBackToBuiltInOrder()
    {
        // qa-a-2 / C2 collision: the seeded custom order must differ from
        // Config::builtInProviderOrder() -- asserted against that
        // accessor below, not a hard-coded three-element literal, since
        // C2 appends "qq" to it and a literal here would silently drift
        // stale once that merges -- or this test cannot tell
        // "present-but-drained -> built-in default" (the branch under
        // test) apart from a mutated "treat present-blank as absent ->
        // enabled = order" (the branch KeepPresentAsBlank exists to
        // avoid): both would return the same list.
        const QStringList customOrder{QStringLiteral("amll"), QStringLiteral("local"),
                                      QStringLiteral("netease")};
        writeRawSettingsFileContents(
            QByteArrayLiteral("[providers]\norder=amll, local, netease\nenabled=\n"));
        const Config config;
        QCOMPARE(config.providerOrder(), customOrder);
        const QStringList enabled = config.enabledProviderOrder();
        QCOMPARE(enabled, Config::builtInProviderOrder());
        // Load-bearing on its own, not just belt-and-suspenders: pins that
        // the result is the built-in default specifically, not merely
        // "whatever the seeded custom order happened to be" -- the exact
        // distinction qa-a-2's mutation targets. Asserting only the
        // QCOMPARE above would go quiet again if a future reseed happened
        // to equal the built-in order.
        QVERIFY(enabled != customOrder);
    }

    void disabledProviderKeepsItsConfiguredPosition()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("providers/order"),
                          QStringList({QStringLiteral("amll"), QStringLiteral("local"),
                                       QStringLiteral("netease")}));
        settings.setValue(QStringLiteral("providers/enabled"),
                          QStringList({QStringLiteral("local"), QStringLiteral("netease")}));
        settings.sync();

        const Config config;
        QCOMPARE(config.providerOrder(),
                 QStringList({QStringLiteral("amll"), QStringLiteral("local"),
                              QStringLiteral("netease")}));
        QCOMPARE(config.enabledProviderOrder(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease")}));
    }

    // Q24 / DESIGN.md decision 67: players/blacklist and
    // filter/musicUrlPrefixes must distinguish "unset" (-> built-in
    // default), "explicitly emptied" (-> empty, not the default) and
    // "populated" (-> the stored value). See A.2/A.4 in SPEC.md.

    void blacklistUnsetUsesBuiltInDefault()
    {
        QCOMPARE(Config().policy().serviceBlacklist,
                 QStringList{QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*")});
    }

    void blacklistExplicitlyEmptyStaysEmptyAcrossRestart()
    {
        {
            QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                               QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
            settings.setValue(QStringLiteral("players/blacklistEmpty"), true);
            settings.sync();
        }
        QVERIFY(Config().policy().serviceBlacklist.isEmpty());
        // A second, independent Config -- simulating a daemon restart --
        // must see the same explicitly-empty state, not fall back to the
        // built-in default.
        QVERIFY(Config().policy().serviceBlacklist.isEmpty());
    }

    void blacklistPopulatedValueIsUsedAsIs()
    {
        {
            QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                               QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
            settings.setValue(QStringLiteral("players/blacklist"),
                              QStringList{QStringLiteral("org.mpris.MediaPlayer2.custom.*")});
            settings.sync();
        }
        QCOMPARE(Config().policy().serviceBlacklist,
                 QStringList{QStringLiteral("org.mpris.MediaPlayer2.custom.*")});
    }

    void migrationRestoresBuiltInBlacklistDefaultAfterLeakedInvalidLiteral()
    {
        // Reproduces the real-world leak from A.1: some earlier version of
        // the code wrote an empty QStringList, which serializes to the
        // literal `@Invalid()`.
        writeRawSettingsFileContents(QByteArrayLiteral("[players]\nblacklist=@Invalid()\n"));
        Config config;
        config.migrateLegacySettings();
        QCOMPARE(config.policy().serviceBlacklist,
                 QStringList{QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*")});
        const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                                 QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        QVERIFY(!settings.contains(QStringLiteral("players/blacklist")));
    }

    void migrationOfAllThreeKeysIsIdempotent()
    {
        writeRawSettingsFileContents(
            QByteArrayLiteral("[players]\nblacklist=@Invalid()\n\n"
                              "[filter]\nmusicUrlPrefixes=@Invalid()\nplatforms=@Invalid()\n"));
        {
            Config config;
            config.migrateLegacySettings();
        }
        const QString afterFirstRun = rawSettingsFileContents();
        {
            Config config;
            config.migrateLegacySettings();
        }
        QCOMPARE(rawSettingsFileContents(), afterFirstRun);
    }

    void musicUrlPrefixesUnsetUsesBuiltInDefault()
    {
        QCOMPARE(Config().policy().musicUrlPrefixes,
                 QStringList({QStringLiteral("https://music.163.com/"),
                              QStringLiteral("http://music.163.com/")}));
    }

    void musicUrlPrefixesExplicitlyEmptyStaysEmptyAcrossRestart()
    {
        {
            QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                               QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
            settings.setValue(QStringLiteral("filter/musicUrlPrefixesEmpty"), true);
            settings.sync();
        }
        QVERIFY(Config().policy().musicUrlPrefixes.isEmpty());
        QVERIFY(Config().policy().musicUrlPrefixes.isEmpty());
    }

    void musicUrlPrefixesPopulatedValueIsUsedAsIs()
    {
        {
            QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                               QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
            settings.setValue(QStringLiteral("filter/musicUrlPrefixes"),
                              QStringList{QStringLiteral("https://example.invalid/")});
            settings.sync();
        }
        QCOMPARE(Config().policy().musicUrlPrefixes,
                 QStringList{QStringLiteral("https://example.invalid/")});
    }

    void migrationRestoresBuiltInMusicUrlPrefixesDefaultAfterLeakedInvalidLiteral()
    {
        writeRawSettingsFileContents(QByteArrayLiteral("[filter]\nmusicUrlPrefixes=@Invalid()\n"));
        Config config;
        config.migrateLegacySettings();
        QCOMPARE(config.policy().musicUrlPrefixes,
                 QStringList({QStringLiteral("https://music.163.com/"),
                              QStringLiteral("http://music.163.com/")}));
        const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                                 QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        QVERIFY(!settings.contains(QStringLiteral("filter/musicUrlPrefixes")));
    }

    // filter/platforms uses the same three-state marker machinery as the
    // two Q24 keys (DESIGN.md decision 67); its migration target differs,
    // covered separately below.

    void platformsUnsetUsesBuiltInDefault()
    {
        QCOMPARE(Config().policy().enabledPlatforms,
                 QStringList({QStringLiteral("netease"), QStringLiteral("apple")}));
    }

    void platformsExplicitlyEmptyStaysEmptyAcrossRestart()
    {
        {
            QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                               QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
            settings.setValue(QStringLiteral("filter/platformsEmpty"), true);
            settings.sync();
        }
        QVERIFY(Config().policy().enabledPlatforms.isEmpty());
        QVERIFY(Config().policy().enabledPlatforms.isEmpty());
    }

    void platformsPopulatedValueIsUsedAsIs()
    {
        {
            QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                               QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
            settings.setValue(QStringLiteral("filter/platforms"), QStringList{QStringLiteral("netease")});
            settings.sync();
        }
        QCOMPARE(Config().policy().enabledPlatforms, QStringList{QStringLiteral("netease")});
    }

    // A.3b: unlike the two free-text keys, filter/platforms' migration
    // target is "explicitly empty", not "unset" -- the only way this key
    // ever holds `@Invalid()` is a user deliberately unchecking both
    // platform checkboxes, so migration must preserve that choice rather
    // than silently re-enabling both platforms.
    void migrationOfLeakedPlatformsInvalidLiteralPreservesExplicitEmptyNotDefault()
    {
        writeRawSettingsFileContents(QByteArrayLiteral("[filter]\nplatforms=@Invalid()\n"));
        Config config;
        config.migrateLegacySettings();
        QVERIFY(config.policy().enabledPlatforms.isEmpty());
        const QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                                 QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        QVERIFY(!settings.contains(QStringLiteral("filter/platforms")));
        QCOMPARE(settings.value(QStringLiteral("filter/platformsEmpty")).toBool(), true);
        // Idempotent, same as the other two keys.
        Config again;
        again.migrateLegacySettings();
        QVERIFY(again.policy().enabledPlatforms.isEmpty());
    }

    // qa-1: a marker key holding anything other than a real bool literal
    // (e.g. a hand-typo'd value) reads as explicitly-empty rather than
    // erroring or falling back to the default, because
    // QVariant::toBool() treats any string other than "false"/"0" as
    // true. Defensible (fail toward the safer "nothing blacklisted"
    // state rather than silently resurrecting a filter the user tried to
    // clear), but it was incidental until pinned here. One case per key.

    void garbageBlacklistMarkerValueReadsAsExplicitlyEmpty()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("players/blacklistEmpty"), QStringLiteral("banana"));
        settings.sync();
        QVERIFY(Config().policy().serviceBlacklist.isEmpty());
    }

    void garbageMusicUrlPrefixesMarkerValueReadsAsExplicitlyEmpty()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("filter/musicUrlPrefixesEmpty"), QStringLiteral("banana"));
        settings.sync();
        QVERIFY(Config().policy().musicUrlPrefixes.isEmpty());
    }

    void garbagePlatformsMarkerValueReadsAsExplicitlyEmpty()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("filter/platformsEmpty"), QStringLiteral("banana"));
        settings.sync();
        QVERIFY(Config().policy().enabledPlatforms.isEmpty());
    }

    void debugLoggingDefaultsToFalse()
    {
        QCOMPARE(Config().debugLoggingEnabled(), false);
    }

    void debugLoggingReadsBackWrittenValue()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("logging/debug"), true);
        settings.sync();
        QCOMPARE(Config().debugLoggingEnabled(), true);
    }

    void proxyModeDefaultsToNone()
    {
        QCOMPARE(Config().proxyMode(), QStringLiteral("none"));
    }

    void proxyModeReadsBackWrittenValue()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("network/proxyMode"), QStringLiteral("manual"));
        settings.sync();
        QCOMPARE(Config().proxyMode(), QStringLiteral("manual"));
    }

    void proxyModeFallsBackToNoneForUnknownValue()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("network/proxyMode"), QStringLiteral("bogus"));
        settings.sync();
        QCOMPARE(Config().proxyMode(), QStringLiteral("none"));
    }

    void proxyUrlDefaultsToEmpty()
    {
        QCOMPARE(Config().proxyUrl(), QString());
    }

    void proxyUrlReadsBackWrittenValue()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("network/proxyUrl"), QStringLiteral("socks5://127.0.0.1:1080"));
        settings.sync();
        QCOMPARE(Config().proxyUrl(), QStringLiteral("socks5://127.0.0.1:1080"));
    }
};

QTEST_GUILESS_MAIN(ConfigTest)
#include "tst_config.moc"
