#include "core/store/lyricstore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <climits>

using namespace PlasmaLyrics;

class LyricStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void mappingsAndMissTtl()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const TrackRef ref{QStringLiteral("netease"), QStringLiteral("1299289240"), 0.97};
        QVERIFY(store.mapFingerprint(QStringLiteral("mediaSrc:abc"), ref, 100));
        const auto restored = store.refForFingerprint(QStringLiteral("mediaSrc:abc"));
        QVERIFY(restored.has_value());
        QCOMPARE(restored->trackId, ref.trackId);
        QVERIFY(store.recordMiss(QStringLiteral("meta:video"), QStringLiteral("not-music"), 100));
        const auto miss = store.freshMiss(QStringLiteral("meta:video"), 100 + 60);
        QVERIFY(miss.has_value());
        QCOMPARE(miss->reason, QStringLiteral("not-music"));
        QCOMPARE(miss->triedAt, 100);
        QVERIFY(store.hasFreshMiss(QStringLiteral("meta:video"), 100 + 60));
        QVERIFY(!store.hasFreshMiss(QStringLiteral("meta:video"), 100 + 8 * 24 * 60 * 60));
    }

    void lyricAndOffsetRoundTrip()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const TrackRef ref{QStringLiteral("netease"), QStringLiteral("1"), 1};
        LyricDocument input{{{1000, 2000, QStringLiteral("line"),
                              QStringLiteral("translation"),
                              QList<LyricWord>{{1000, 1400, QStringLiteral("li")},
                                               {1400, 2000, QStringLiteral("ne")}}}},
                             0, true,
                             {{QStringLiteral("source"), QStringLiteral("amll")},
                              {QStringLiteral("contentId"), QStringLiteral("song.ttml")}}};
        QVERIFY(store.putLyric(ref, input, 100));
        QVERIFY(store.setOffset(ref, 250));
        QCOMPARE(store.adjustOffset(ref, -50), std::optional<int>(200));
        const auto restored = store.lyric(ref);
        QVERIFY(restored.has_value());
        QCOMPARE(restored->lines, input.lines);
        QCOMPARE(restored->offsetMs, 200);
        QCOMPARE(restored->hasWords, true);
        QCOMPARE(restored->metadata, input.metadata);
    }

    void providerMappingsPreferencesMissesAndOffsetsAreIndependent()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const QString fingerprint = QStringLiteral("mediaSrc:multi-provider");
        const TrackRef netease{QStringLiteral("netease"), QStringLiteral("1"), 0.91};
        const TrackRef amll{QStringLiteral("amll"), QStringLiteral("ncm:1"), 0.95};

        QVERIFY(store.mapFingerprint(fingerprint, netease, 100));
        QVERIFY(store.mapFingerprint(fingerprint, amll, 101));
        QCOMPARE(store.refForProvider(fingerprint, QStringLiteral("netease"))->trackId,
                 QStringLiteral("1"));
        QCOMPARE(store.refForProvider(fingerprint, QStringLiteral("amll"))->trackId,
                 QStringLiteral("ncm:1"));
        QCOMPARE(store.refForFingerprint(fingerprint)->provider, QStringLiteral("amll"));

        QVERIFY(store.setPreferredProvider(fingerprint, QStringLiteral("netease"), 102));
        QCOMPARE(store.preferredProvider(fingerprint).value_or(QString()),
                 QStringLiteral("netease"));
        QVERIFY(store.clearPreferredProvider(fingerprint));
        QVERIFY(!store.preferredProvider(fingerprint).has_value());

        QVERIFY(store.recordProviderMiss(fingerprint, QStringLiteral("netease"),
                                         QStringLiteral("network"), QStringLiteral("v1"), 200));
        QVERIFY(store.recordProviderMiss(fingerprint, QStringLiteral("amll"),
                                         QStringLiteral("no-candidate"), QStringLiteral("index-a"), 200));
        QVERIFY(store.freshProviderMiss(fingerprint, QStringLiteral("netease"),
                                        QStringLiteral("v1"), 250).has_value());
        QVERIFY(!store.freshProviderMiss(fingerprint, QStringLiteral("netease"),
                                         QStringLiteral("v2"), 250).has_value());
        QVERIFY(store.freshProviderMiss(fingerprint, QStringLiteral("amll"),
                                        QStringLiteral("index-a"), 250).has_value());
        QVERIFY(store.clearProviderMiss(fingerprint, QStringLiteral("netease")));
        QVERIFY(!store.freshProviderMiss(fingerprint, QStringLiteral("netease"),
                                         QStringLiteral("v1"), 250).has_value());
        QVERIFY(store.freshProviderMiss(fingerprint, QStringLiteral("amll"),
                                        QStringLiteral("index-a"), 250).has_value());

        QVERIFY(store.setOffset(netease, 125));
        QVERIFY(store.setOffset(amll, -250));
        QCOMPARE(store.offset(netease), 125);
        QCOMPARE(store.offset(amll), -250);
    }

    void preferredProviderPersistsAcrossStoreInstances()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("lyrics.db"));
        {
            LyricStore store(path);
            QVERIFY(store.open());
            QVERIFY(store.setPreferredProvider(QStringLiteral("mediaSrc:persist"),
                                               QStringLiteral("amll")));
        }
        {
            LyricStore store(path);
            QVERIFY(store.open());
            QCOMPARE(store.preferredProvider(QStringLiteral("mediaSrc:persist")),
                     std::optional<QString>(QStringLiteral("amll")));
        }
    }

    void migratesLegacySchemaWithoutTurningActualResultsIntoPreferences()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.db"));
        const QString connectionName = QStringLiteral("tst-lyricstore-legacy-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        {
            auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            database.setDatabaseName(path);
            QVERIFY(database.open());
            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TABLE lyric (provider TEXT NOT NULL, track_id TEXT NOT NULL, "
                "fetched_at INTEGER NOT NULL, origin TEXT, translation TEXT, "
                "has_words INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(provider, track_id))")));
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TABLE fingerprint (fingerprint TEXT PRIMARY KEY, provider TEXT NOT NULL, "
                "track_id TEXT NOT NULL, matched_at INTEGER NOT NULL, score REAL)")));
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TABLE miss (fingerprint TEXT PRIMARY KEY, tried_at INTEGER NOT NULL, reason TEXT)")));
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TABLE offset (provider TEXT NOT NULL, track_id TEXT NOT NULL, "
                "offset_ms INTEGER NOT NULL, PRIMARY KEY(provider, track_id))")));
            QVERIFY(query.exec(QStringLiteral(
                "INSERT INTO lyric VALUES('netease', 'legacy-track', 10, "
                "'[{\"startMs\":0,\"endMs\":1000,\"text\":\"legacy line\",\"translation\":null,\"words\":null}]', "
                "'[]', 0)")));
            QVERIFY(query.exec(QStringLiteral(
                "INSERT INTO fingerprint VALUES('mediaSrc:legacy', 'netease', "
                "'legacy-track', 11, 0.9)")));
            QVERIFY(query.exec(QStringLiteral(
                "INSERT INTO miss VALUES('mediaSrc:old-miss', 12, 'no-candidate')")));
            QVERIFY(query.exec(QStringLiteral(
                "INSERT INTO offset VALUES('netease', 'legacy-track', 333)")));
            database.close();
        }
        QSqlDatabase::removeDatabase(connectionName);

        {
            LyricStore store(path);
            QString error;
            QVERIFY2(store.open(&error), qPrintable(error));
            const auto actual = store.refForFingerprint(QStringLiteral("mediaSrc:legacy"));
            QVERIFY(actual.has_value());
            QCOMPARE(store.refForProvider(QStringLiteral("mediaSrc:legacy"),
                                          QStringLiteral("netease"))->trackId,
                     QStringLiteral("legacy-track"));
            QVERIFY(!store.preferredProvider(QStringLiteral("mediaSrc:legacy")).has_value());
            QVERIFY(store.freshMiss(QStringLiteral("mediaSrc:old-miss"), 13).has_value());
            QVERIFY(!store.freshProviderMiss(QStringLiteral("mediaSrc:old-miss"),
                                             QStringLiteral("netease"),
                                             QStringLiteral("netease"), 13).has_value());
            const auto document = store.lyric(*actual);
            QVERIFY(document.has_value());
            QCOMPARE(document->lines.first().text, QStringLiteral("legacy line"));
            QVERIFY(document->metadata.isEmpty());
            QCOMPARE(document->offsetMs, 333);
        }

        // Schema migration is deliberately repeatable.
        LyricStore reopened(path);
        QString error;
        QVERIFY2(reopened.open(&error), qPrintable(error));
        QCOMPARE(reopened.refForProvider(QStringLiteral("mediaSrc:legacy"),
                                         QStringLiteral("netease"))->trackId,
                 QStringLiteral("legacy-track"));
    }

    void globalOffsetDefaults()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        QCOMPARE(store.globalOffsetEnabled(), false);
        QCOMPARE(store.globalOffsetMs(), 0);
    }

    void globalOffsetRoundTrip()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        QVERIFY(store.setGlobalOffsetEnabled(true));
        QCOMPARE(store.globalOffsetEnabled(), true);
        QVERIFY(store.setGlobalOffsetMs(1234));
        QCOMPARE(store.globalOffsetMs(), 1234);
        QVERIFY(store.setGlobalOffsetEnabled(false));
        QCOMPARE(store.globalOffsetEnabled(), false);
    }

    void globalOffsetClamping()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());

        QVERIFY(store.setGlobalOffsetMs(50000));
        QCOMPARE(store.globalOffsetMs(), 10000);
        QVERIFY(store.setGlobalOffsetMs(-50000));
        QCOMPARE(store.globalOffsetMs(), -10000);

        QVERIFY(store.setGlobalOffsetMs(9990));
        QCOMPARE(store.adjustGlobalOffset(100), std::optional<int>(10000));
        QCOMPARE(store.globalOffsetMs(), 10000);

        QVERIFY(store.setGlobalOffsetMs(-9990));
        QCOMPARE(store.adjustGlobalOffset(-100), std::optional<int>(-10000));
        QCOMPARE(store.globalOffsetMs(), -10000);
    }

    void globalOffsetAdjustAccumulates()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());

        QCOMPARE(store.adjustGlobalOffset(300), std::optional<int>(300));
        QCOMPARE(store.adjustGlobalOffset(-100), std::optional<int>(200));
        QCOMPARE(store.globalOffsetMs(), 200);
    }

    void globalOffsetPersistsAcrossInstances()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.db"));
        {
            LyricStore store(path);
            QVERIFY(store.open());
            QVERIFY(store.setGlobalOffsetEnabled(true));
            QVERIFY(store.setGlobalOffsetMs(-500));
        }
        {
            LyricStore store(path);
            QVERIFY(store.open());
            QCOMPARE(store.globalOffsetEnabled(), true);
            QCOMPARE(store.globalOffsetMs(), -500);
        }
    }

    void globalOffsetDoesNotAffectPerTrackOffset()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const TrackRef ref{QStringLiteral("netease"), QStringLiteral("42"), 1};
        QVERIFY(store.setOffset(ref, 777));

        QVERIFY(store.setGlobalOffsetEnabled(true));
        QVERIFY(store.setGlobalOffsetMs(3000));
        QCOMPARE(store.adjustGlobalOffset(500), std::optional<int>(3500));

        QCOMPARE(store.offset(ref), 777);
    }

    void globalOffsetAdjustExtremeDelta()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());

        QVERIFY(store.setGlobalOffsetMs(5000));
        QCOMPARE(store.adjustGlobalOffset(INT_MAX), std::optional<int>(10000));
        QCOMPARE(store.globalOffsetMs(), 10000);

        QVERIFY(store.setGlobalOffsetMs(-5000));
        QCOMPARE(store.adjustGlobalOffset(INT_MIN), std::optional<int>(-10000));
        QCOMPARE(store.globalOffsetMs(), -10000);
    }

    void globalOffsetClampsDirtyRead()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.db"));
        LyricStore store(path);
        QVERIFY(store.open());

        // Bypass the public API and write an out-of-range value directly,
        // as if it had been poked in by hand or left over from a bug.
        const QString connectionName = QStringLiteral("tst-lyricstore-dirty-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral("INSERT OR REPLACE INTO setting(name, value) VALUES('globalOffsetMs', '999999')")));
            db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);

        QCOMPARE(store.globalOffsetMs(), 10000);
    }
};

QTEST_GUILESS_MAIN(LyricStoreTest)
#include "tst_lyricstore.moc"
