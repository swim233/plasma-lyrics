#include "core/store/lyricstore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
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
        QVERIFY(store.hasFreshMiss(QStringLiteral("meta:video"), 100 + 60));
        QVERIFY(!store.hasFreshMiss(QStringLiteral("meta:video"), 100 + 8 * 24 * 60 * 60));
    }

    void lyricAndOffsetRoundTrip()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const TrackRef ref{QStringLiteral("netease"), QStringLiteral("1"), 1};
        LyricDocument input{{{1000, 2000, QStringLiteral("line"), QStringLiteral("translation"), std::nullopt}}, 0, false};
        QVERIFY(store.putLyric(ref, input, 100));
        QVERIFY(store.setOffset(ref, 250));
        QCOMPARE(store.adjustOffset(ref, -50), std::optional<int>(200));
        const auto restored = store.lyric(ref);
        QVERIFY(restored.has_value());
        QCOMPARE(restored->lines, input.lines);
        QCOMPARE(restored->offsetMs, 200);
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
