#include "core/config/stringlistsetting.h"

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

using namespace PlasmaLyrics;

namespace {

QString iniPath(const QTemporaryDir &directory)
{
    return directory.path() + QStringLiteral("/plasma-lyricsd.ini");
}

QString rawFileContents(const QString &path)
{
    QFile file(path);
    const bool opened = file.open(QIODevice::ReadOnly | QIODevice::Text);
    Q_ASSERT(opened);
    return QString::fromUtf8(file.readAll());
}

} // namespace

class StringListSettingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A.4 / A.3.1: three states for the marker-based representation.

    void unsetKeyFallsBackToDefault()
    {
        QTemporaryDir directory;
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("players/blacklist"),
                                        QStringLiteral("players/blacklistEmpty")};
        QCOMPARE(readStringListOrEmpty(settings, setting, {QStringLiteral("default")}),
                 QStringList{QStringLiteral("default")});
    }

    void explicitlyEmptyStaysEmptyAndDoesNotFallBackToDefault()
    {
        QTemporaryDir directory;
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("players/blacklist"),
                                        QStringLiteral("players/blacklistEmpty")};
        writeStringListOrEmpty(settings, setting, {});
        settings.sync();
        QCOMPARE(readStringListOrEmpty(settings, setting, {QStringLiteral("default")}), QStringList());
    }

    void populatedValueRoundTrips()
    {
        QTemporaryDir directory;
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("players/blacklist"),
                                        QStringLiteral("players/blacklistEmpty")};
        const QStringList value{QStringLiteral("org.mpris.MediaPlayer2.foo.*"),
                                QStringLiteral("org.mpris.MediaPlayer2.bar.*")};
        writeStringListOrEmpty(settings, setting, value);
        settings.sync();
        QCOMPARE(readStringListOrEmpty(settings, setting, {QStringLiteral("default")}), value);
    }

    // qa-a-2: a bare hand-written `key=` parses as a single valid
    // empty-string element (not `@Invalid()`), so it takes the "key holds
    // a value" branch. It must read as an empty list, not a one-element
    // list containing "" -- a caller matching entries as prefixes
    // (QString::startsWith()) would otherwise treat that empty entry as
    // matching everything, inverting "nothing configured" into "accept
    // everything".

    void bareKeyWithNoValueReadsAsEmptyNotAsAnEmptyStringEntry()
    {
        QTemporaryDir directory;
        {
            QFile file(iniPath(directory));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(QByteArrayLiteral("[filter]\nmusicUrlPrefixes=\n"));
        }
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("filter/musicUrlPrefixes"),
                                        QStringLiteral("filter/musicUrlPrefixesEmpty")};
        QCOMPARE(readStringListOrEmpty(settings, setting, {QStringLiteral("default")}), QStringList());
    }

    void blankEntriesAreDroppedFromAMixedList()
    {
        QTemporaryDir directory;
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("players/blacklist"),
                                        QStringLiteral("players/blacklistEmpty")};
        settings.setValue(QStringLiteral("players/blacklist"),
                          QStringList{QStringLiteral("org.mpris.MediaPlayer2.foo.*"), QString(),
                                      QStringLiteral("   "),
                                      QStringLiteral("org.mpris.MediaPlayer2.bar.*")});
        settings.sync();
        QCOMPARE(readStringListOrEmpty(settings, setting, {QStringLiteral("default")}),
                 QStringList({QStringLiteral("org.mpris.MediaPlayer2.foo.*"),
                              QStringLiteral("org.mpris.MediaPlayer2.bar.*")}));
    }

    // A.3.1: round trip stability -- store, read, store again produces the
    // same file, for both the empty and the populated state.

    void explicitEmptyRoundTripIsStable()
    {
        QTemporaryDir directory;
        const StringListSetting setting{QStringLiteral("players/blacklist"),
                                        QStringLiteral("players/blacklistEmpty")};
        {
            QSettings settings(iniPath(directory), QSettings::IniFormat);
            writeStringListOrEmpty(settings, setting, {});
        }
        const QString afterFirstWrite = rawFileContents(iniPath(directory));
        {
            QSettings settings(iniPath(directory), QSettings::IniFormat);
            const auto value = readStringListOrEmpty(settings, setting, {QStringLiteral("default")});
            writeStringListOrEmpty(settings, setting, value);
        }
        QCOMPARE(rawFileContents(iniPath(directory)), afterFirstWrite);
    }

    void populatedRoundTripIsStable()
    {
        QTemporaryDir directory;
        const StringListSetting setting{QStringLiteral("players/blacklist"),
                                        QStringLiteral("players/blacklistEmpty")};
        const QStringList value{QStringLiteral("org.mpris.MediaPlayer2.foo.*")};
        {
            QSettings settings(iniPath(directory), QSettings::IniFormat);
            writeStringListOrEmpty(settings, setting, value);
        }
        const QString afterFirstWrite = rawFileContents(iniPath(directory));
        {
            QSettings settings(iniPath(directory), QSettings::IniFormat);
            const auto stored = readStringListOrEmpty(settings, setting, {QStringLiteral("default")});
            writeStringListOrEmpty(settings, setting, stored);
        }
        QCOMPARE(rawFileContents(iniPath(directory)), afterFirstWrite);
    }

    // A.3.2: the write side must never produce the literal `@Invalid()`.

    void writingEmptyNeverProducesInvalidLiteral()
    {
        QTemporaryDir directory;
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("players/blacklist"),
                                        QStringLiteral("players/blacklistEmpty")};
        writeStringListOrEmpty(settings, setting, {});
        settings.sync();
        QVERIFY(!rawFileContents(iniPath(directory)).contains(QStringLiteral("@Invalid()")));
    }

    // A.3.1: human-editable -- a hand-written marker line is honoured
    // without the primary key ever being present.

    void handWrittenMarkerWithNoPrimaryKeyReadsAsEmpty()
    {
        QTemporaryDir directory;
        {
            QFile file(iniPath(directory));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(QByteArrayLiteral("[players]\nblacklistEmpty=true\n"));
        }
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("players/blacklist"),
                                        QStringLiteral("players/blacklistEmpty")};
        QCOMPARE(readStringListOrEmpty(settings, setting, {QStringLiteral("default")}), QStringList());
    }

    // A freshly typed value under the primary key wins over a stale marker
    // left over from an earlier explicit-empty state (documented precedence
    // in stringlistsetting.h).
    void freshValueUnderPrimaryKeyWinsOverStaleMarker()
    {
        QTemporaryDir directory;
        {
            QFile file(iniPath(directory));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(QByteArrayLiteral(
                "[players]\nblacklist=org.mpris.MediaPlayer2.foo.*\nblacklistEmpty=true\n"));
        }
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("players/blacklist"),
                                        QStringLiteral("players/blacklistEmpty")};
        QCOMPARE(readStringListOrEmpty(settings, setting, {QStringLiteral("default")}),
                 QStringList{QStringLiteral("org.mpris.MediaPlayer2.foo.*")});
    }

    // qa-1: a marker holding anything other than a real bool literal (e.g.
    // a hand-typo'd value) reads as explicitly-empty rather than erroring,
    // because QVariant::toBool() treats any string other than "false"/"0"
    // as true. Defensible (fails toward the safer "nothing configured"
    // state), but incidental until pinned here -- this is the root
    // mechanism every per-key garbage-marker test in tst_config.cpp and
    // tst_backendconfig.cpp relies on.
    void garbageMarkerValueIsTreatedAsExplicitlyEmpty()
    {
        QTemporaryDir directory;
        {
            QFile file(iniPath(directory));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(QByteArrayLiteral("[players]\nblacklistEmpty=banana\n"));
        }
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("players/blacklist"),
                                        QStringLiteral("players/blacklistEmpty")};
        QCOMPARE(readStringListOrEmpty(settings, setting, {QStringLiteral("default")}), QStringList());
    }

    // A.2's trap, reproduced directly: a leftover `@Invalid()` (as an empty
    // QStringList used to serialize before this fix) must never read back
    // as "explicitly empty" -- only the marker may say that.
    void leftoverInvalidLiteralWithNoMarkerFallsBackToDefaultNotEmpty()
    {
        QTemporaryDir directory;
        {
            QFile file(iniPath(directory));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(QByteArrayLiteral("[players]\nblacklist=@Invalid()\n"));
        }
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("players/blacklist"),
                                        QStringLiteral("players/blacklistEmpty")};
        QCOMPARE(readStringListOrEmpty(settings, setting, {QStringLiteral("default")}),
                 QStringList{QStringLiteral("default")});
    }

    // A.4: migration removes a leftover `@Invalid()` key so the built-in
    // default resumes applying, and is idempotent.

    void migrationRemovesLeftoverInvalidLiteral()
    {
        QTemporaryDir directory;
        {
            QFile file(iniPath(directory));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(QByteArrayLiteral("[players]\nblacklist=@Invalid()\n"));
        }
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        QVERIFY(migrateLegacyInvalidEntry(settings, QStringLiteral("players/blacklist")));
        settings.sync();
        QVERIFY(!settings.contains(QStringLiteral("players/blacklist")));
        QCOMPARE(readStringListOrEmpty(settings, {QStringLiteral("players/blacklist"),
                                                  QStringLiteral("players/blacklistEmpty")},
                                       {QStringLiteral("default")}),
                 QStringList{QStringLiteral("default")});
    }

    void migrationIsIdempotent()
    {
        QTemporaryDir directory;
        {
            QFile file(iniPath(directory));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(QByteArrayLiteral("[players]\nblacklist=@Invalid()\n"));
        }
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        QVERIFY(migrateLegacyInvalidEntry(settings, QStringLiteral("players/blacklist")));
        settings.sync();
        const QString afterFirstRun = rawFileContents(iniPath(directory));
        QVERIFY(!migrateLegacyInvalidEntry(settings, QStringLiteral("players/blacklist")));
        settings.sync();
        QCOMPARE(rawFileContents(iniPath(directory)), afterFirstRun);
    }

    void migrationLeavesAGenuineValueAlone()
    {
        QTemporaryDir directory;
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        settings.setValue(QStringLiteral("players/blacklist"),
                          QStringList{QStringLiteral("org.mpris.MediaPlayer2.foo.*")});
        settings.sync();
        QVERIFY(!migrateLegacyInvalidEntry(settings, QStringLiteral("players/blacklist")));
        QCOMPARE(settings.value(QStringLiteral("players/blacklist")).toStringList(),
                 QStringList{QStringLiteral("org.mpris.MediaPlayer2.foo.*")});
    }

    void migrationLeavesAnAbsentKeyAlone()
    {
        QTemporaryDir directory;
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        QVERIFY(!migrateLegacyInvalidEntry(settings, QStringLiteral("players/blacklist")));
        QVERIFY(!settings.contains(QStringLiteral("players/blacklist")));
    }

    // A.3b: filter/platforms migrates a leftover `@Invalid()` to
    // "explicitly empty" rather than "unset" -- the only way this key
    // holds `@Invalid()` is a deliberate "uncheck both platforms" action,
    // so migration must preserve that choice, not silently restore the
    // built-in default.

    void migrationToExplicitEmptySetsMarkerAndRemovesTheInvalidKey()
    {
        QTemporaryDir directory;
        {
            QFile file(iniPath(directory));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(QByteArrayLiteral("[filter]\nplatforms=@Invalid()\n"));
        }
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("filter/platforms"),
                                        QStringLiteral("filter/platformsEmpty")};
        QVERIFY(migrateLegacyInvalidEntryToExplicitEmpty(settings, setting));
        settings.sync();
        QVERIFY(!settings.contains(QStringLiteral("filter/platforms")));
        QCOMPARE(settings.value(QStringLiteral("filter/platformsEmpty")).toBool(), true);
        QCOMPARE(readStringListOrEmpty(settings, setting, {QStringLiteral("netease"), QStringLiteral("apple")}),
                 QStringList());
    }

    void migrationToExplicitEmptyIsIdempotent()
    {
        QTemporaryDir directory;
        {
            QFile file(iniPath(directory));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(QByteArrayLiteral("[filter]\nplatforms=@Invalid()\n"));
        }
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("filter/platforms"),
                                        QStringLiteral("filter/platformsEmpty")};
        QVERIFY(migrateLegacyInvalidEntryToExplicitEmpty(settings, setting));
        settings.sync();
        const QString afterFirstRun = rawFileContents(iniPath(directory));
        QVERIFY(!migrateLegacyInvalidEntryToExplicitEmpty(settings, setting));
        settings.sync();
        QCOMPARE(rawFileContents(iniPath(directory)), afterFirstRun);
    }

    void migrationToExplicitEmptyLeavesAGenuineValueAlone()
    {
        QTemporaryDir directory;
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const StringListSetting setting{QStringLiteral("filter/platforms"),
                                        QStringLiteral("filter/platformsEmpty")};
        settings.setValue(QStringLiteral("filter/platforms"), QStringList{QStringLiteral("netease")});
        settings.sync();
        QVERIFY(!migrateLegacyInvalidEntryToExplicitEmpty(settings, setting));
        QCOMPARE(settings.value(QStringLiteral("filter/platforms")).toStringList(),
                 QStringList{QStringLiteral("netease")});
        QVERIFY(!settings.contains(QStringLiteral("filter/platformsEmpty")));
    }

    // providers/order and providers/enabled (A.3.5): different empty-write
    // substitutes, neither ever an `@Invalid()` literal.

    void removeKeyPolicyRemovesTheKeyOnEmptyWrite()
    {
        QTemporaryDir directory;
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        settings.setValue(QStringLiteral("providers/order"),
                          QStringList{QStringLiteral("local")});
        settings.sync();
        writeStringListNoInvalid(settings, QStringLiteral("providers/order"), {},
                                 EmptyStringListPolicy::RemoveKey);
        settings.sync();
        QVERIFY(!settings.contains(QStringLiteral("providers/order")));
        QVERIFY(!rawFileContents(iniPath(directory)).contains(QStringLiteral("@Invalid()")));
    }

    void keepPresentAsBlankPolicyKeepsKeyPresentOnEmptyWrite()
    {
        QTemporaryDir directory;
        {
            QSettings settings(iniPath(directory), QSettings::IniFormat);
            writeStringListNoInvalid(settings, QStringLiteral("providers/enabled"), {},
                                     EmptyStringListPolicy::KeepPresentAsBlank);
            settings.sync();
        }
        // qa-a-2: asserting contains()/value() on the *same* QSettings
        // instance that just wrote proves only that its in-memory map
        // agrees with itself -- not that anything reached disk. The
        // property KeepPresentAsBlank exists for is what a fresh reader
        // of the *file* sees, so check the raw bytes (this also rules out
        // the empty-QStringList-round-trips-to-@Invalid() failure this
        // whole policy exists to avoid) and re-open with a new QSettings
        // instance before checking contains()/value().
        const QString raw = rawFileContents(iniPath(directory));
        QVERIFY(!raw.contains(QStringLiteral("@Invalid()")));
        const QStringList rawLines = raw.split(QLatin1Char('\n'));
        QVERIFY(std::any_of(rawLines.cbegin(), rawLines.cend(), [](const QString &line) {
            return line.trimmed() == QStringLiteral("enabled=");
        }));
        QSettings reopened(iniPath(directory), QSettings::IniFormat);
        QVERIFY(reopened.contains(QStringLiteral("providers/enabled")));
        // Every known reader of this key discards blank/whitespace entries,
        // so this is observably still "empty".
        QVERIFY(reopened.value(QStringLiteral("providers/enabled")).toStringList().join(QString())
                    .trimmed()
                    .isEmpty());
    }

    void noInvalidPoliciesWriteNonEmptyValuesNormally()
    {
        QTemporaryDir directory;
        QSettings settings(iniPath(directory), QSettings::IniFormat);
        const QStringList value{QStringLiteral("local"), QStringLiteral("netease")};
        writeStringListNoInvalid(settings, QStringLiteral("providers/order"), value,
                                 EmptyStringListPolicy::RemoveKey);
        QCOMPARE(settings.value(QStringLiteral("providers/order")).toStringList(), value);
    }
};

QTEST_GUILESS_MAIN(StringListSettingTest)
#include "tst_stringlistsetting.moc"
