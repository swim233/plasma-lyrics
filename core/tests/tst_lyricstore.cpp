#include "core/store/lyricstore.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <climits>

using namespace PlasmaLyrics;

namespace {

// Runs one statement through a connection of its own, the way another
// process -- or an older build -- would touch the file. Rows come back with
// their columns joined by '|'.
QStringList sql(const QString &path, const QString &statement)
{
    const QString connectionName = QStringLiteral("tst-lyricstore-sql-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QStringList rows;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        if (database.open()) {
            QSqlQuery query(database);
            if (!query.exec(statement)) {
                rows.append(QStringLiteral("error: ") + query.lastError().text());
            }
            while (query.isSelect() && query.next()) {
                QStringList columns;
                for (int column = 0; column < query.record().count(); ++column) {
                    columns.append(query.value(column).toString());
                }
                rows.append(columns.join(QLatin1Char('|')));
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return rows;
}

LyricDocument oneLine(bool hasWords)
{
    const QList<LyricWord> words{{1000, 2000, QStringLiteral("word")}};
    return {{{1000, 2000, QStringLiteral("word"), std::nullopt, std::nullopt,
              hasWords ? std::optional(words) : std::nullopt}},
            0, hasWords, {}};
}

const WordLevelPurge qqPurge{.marker = QStringLiteral("migration/test-purge"),
                             .provider = QStringLiteral("qq"),
                             .emptyMissReason = QStringLiteral("empty")};

} // namespace

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
                              QStringLiteral("translation"), std::nullopt,
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

    void romanizationRoundTripsAtBothWordAndLineLevel()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const TrackRef ref{QStringLiteral("qq"), QStringLiteral("204530409"), 1};
        LyricWord first{0, 116, QStringLiteral("惑"), QStringLiteral("waku")};
        LyricWord second{116, 232, QStringLiteral("星"), QStringLiteral("sei")};
        // A word with no romanization of its own has to survive alongside the
        // ones that have it.
        LyricWord bare{232, 300, QStringLiteral("!")};
        LyricLine line{0, 300, QStringLiteral("惑星!"), QStringLiteral("planet"),
                       QStringLiteral("waku sei"), QList<LyricWord>{first, second, bare}};
        LyricDocument input{{line}, 0, true, {}};
        QVERIFY(store.putLyric(ref, input, 100));
        const auto restored = store.lyric(ref);
        QVERIFY(restored.has_value());
        QCOMPARE(restored->lines, input.lines);
        const auto &word = restored->lines.first().words->at(0);
        QCOMPARE(*word.romanization, QStringLiteral("waku"));
        QVERIFY(!restored->lines.first().words->at(2).romanization.has_value());
        QCOMPARE(*restored->lines.first().romanization, QStringLiteral("waku sei"));
        QCOMPARE(*restored->lines.first().translation, QStringLiteral("planet"));
    }

    void rowsWrittenBeforeRomanizationExistedStillLoad()
    {
        // Lines are stored as a JSON blob, so no column was added for
        // romanization and no migration runs. What has to hold instead is
        // that a row written by an older build -- whose JSON simply has no
        // such key -- still reads back, with the field unset rather than
        // empty.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("lyrics.db"));
        // Byte-for-byte what lineToJson produced before romanization existed,
        // taken from that revision's own output rather than retyped -- Qt
        // sorts JSON keys, so a hand-written literal in declaration order
        // would not be the shape an older build actually wrote.
        const QString legacy = QStringLiteral(
            R"([{"endMs":2000,"startMs":1000,"text":"line","translation":"t",)"
            R"("words":[{"endMs":1500,"startMs":1000,"text":"li"}]}])");
        {
            LyricStore store(path);
            QVERIFY(store.open());
            auto database = QSqlDatabase::database(QSqlDatabase::connectionNames().last());
            QSqlQuery query(database);
            query.prepare(QStringLiteral(
                "INSERT INTO lyric(provider, track_id, fetched_at, origin, translation, "
                "has_words, metadata) VALUES(?, ?, ?, ?, ?, ?, ?)"));
            query.addBindValue(QStringLiteral("netease"));
            query.addBindValue(QStringLiteral("1"));
            query.addBindValue(100);
            query.addBindValue(legacy);
            query.addBindValue(QStringLiteral("[]"));
            query.addBindValue(1);
            query.addBindValue(QStringLiteral("{}"));
            QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
        }
        // Reopening runs the schema step a second time; it must be a no-op.
        LyricStore store(path);
        QVERIFY(store.open());
        const auto restored = store.lyric({QStringLiteral("netease"), QStringLiteral("1"), 0});
        QVERIFY(restored.has_value());
        QCOMPARE(restored->lines.size(), 1);
        QCOMPARE(restored->lines.first().text, QStringLiteral("line"));
        QCOMPARE(*restored->lines.first().translation, QStringLiteral("t"));
        QVERIFY(!restored->lines.first().romanization.has_value());
        QVERIFY(restored->lines.first().words.has_value());
        QVERIFY(!restored->lines.first().words->first().romanization.has_value());
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
    }

    // DESIGN.md decision 79: the per-track offset shares the global one's
    // +-10000 ms invariant, on every write path.
    void trackOffsetClamping()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const TrackRef ref{QStringLiteral("netease"), QStringLiteral("42"), 1};

        QVERIFY(store.setOffset(ref, 50000));
        QCOMPARE(store.offset(ref), 10000);
        QVERIFY(store.setOffset(ref, -50000));
        QCOMPARE(store.offset(ref), -10000);

        QVERIFY(store.setOffset(ref, 9990));
        QCOMPARE(store.adjustOffset(ref, 100), std::optional<int>(10000));
        QCOMPARE(store.offset(ref), 10000);

        QVERIFY(store.setOffset(ref, -9990));
        QCOMPARE(store.adjustOffset(ref, -100), std::optional<int>(-10000));
        QCOMPARE(store.offset(ref), -10000);
    }

    void trackOffsetAdjustExtremeDelta()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const TrackRef ref{QStringLiteral("netease"), QStringLiteral("42"), 1};

        QVERIFY(store.setOffset(ref, 5000));
        QCOMPARE(store.adjustOffset(ref, INT_MAX), std::optional<int>(10000));
        QCOMPARE(store.offset(ref), 10000);

        QVERIFY(store.setOffset(ref, -5000));
        QCOMPARE(store.adjustOffset(ref, INT_MIN), std::optional<int>(-10000));
        QCOMPARE(store.offset(ref), -10000);
    }

    void trackOffsetClampsDirtyRead()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.db"));
        LyricStore store(path);
        QVERIFY(store.open());
        const TrackRef ref{QStringLiteral("netease"), QStringLiteral("42"), 1};
        QVERIFY(store.putLyric(ref, oneLine(false)));

        // Rows written before decision 79, by hand or by a bug, can hold
        // anything, including a value past the range of int.
        QCOMPARE(sql(path, QStringLiteral("INSERT OR REPLACE INTO offset(provider, track_id, offset_ms) "
                                          "VALUES('netease', '42', 999999)")),
                 QStringList());
        QCOMPARE(store.offset(ref), 10000);
        QCOMPARE(store.lyric(ref)->offsetMs, 10000);
        QCOMPARE(sql(path, QStringLiteral("INSERT OR REPLACE INTO offset(provider, track_id, offset_ms) "
                                          "VALUES('netease', '42', -99999999999)")),
                 QStringList());
        QCOMPARE(store.offset(ref), -10000);
        QCOMPARE(store.adjustOffset(ref, 500), std::optional<int>(-9500));
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
        QCOMPARE(store.globalOffsetMs(), 3000);

        QCOMPARE(store.offset(ref), 777);
        QCOMPARE(store.adjustOffset(ref, 500), std::optional<int>(1277));
        QCOMPARE(store.globalOffsetMs(), 3000);
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

    void purgeRemovesOnlyTheProvidersWordLevelRowsAndTheirMisses()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.db"));
        LyricStore store(path);
        QVERIFY(store.open());
        const TrackRef mappedBoth{QStringLiteral("qq"), QStringLiteral("100"), 1};
        const TrackRef mappedPerProvider{QStringLiteral("qq"), QStringLiteral("101"), 1};
        const TrackRef mappedLegacy{QStringLiteral("qq"), QStringLiteral("102"), 1};
        const TrackRef lineLevel{QStringLiteral("qq"), QStringLiteral("200"), 1};
        const TrackRef otherProvider{QStringLiteral("netease"), QStringLiteral("100"), 1};
        QVERIFY(store.putLyric(mappedBoth, oneLine(true), 10));
        QVERIFY(store.putLyric(mappedPerProvider, oneLine(true), 10));
        QVERIFY(store.putLyric(mappedLegacy, oneLine(true), 10));
        QVERIFY(store.putLyric(lineLevel, oneLine(false), 10));
        QVERIFY(store.putLyric(otherProvider, oneLine(true), 10));

        // mapFingerprint writes both mapping tables; the other two tracks are
        // reachable through only one of them each.
        QVERIFY(store.mapFingerprint(QStringLiteral("fp-both"), mappedBoth, 11));
        QVERIFY(store.mapProviderFingerprint(QStringLiteral("fp-per-provider"), mappedPerProvider, 11));
        QCOMPARE(sql(path, QStringLiteral(
                     "INSERT INTO fingerprint VALUES('fp-legacy', 'qq', '102', 11, 1)")),
                 QStringList());
        QVERIFY(store.mapFingerprint(QStringLiteral("fp-line"), lineLevel, 11));
        QVERIFY(store.mapFingerprint(QStringLiteral("fp-netease"), otherProvider, 11));

        // Removed: a qq miss on each fingerprint mapped to a purged track,
        // whatever its reason, and every qq "empty" miss.
        const QString qq = QStringLiteral("qq");
        QVERIFY(store.recordProviderMiss(QStringLiteral("fp-both"), qq, QStringLiteral("network"), {}, 12));
        QVERIFY(store.recordProviderMiss(QStringLiteral("fp-per-provider"), qq, QStringLiteral("fetch-error"), {}, 12));
        QVERIFY(store.recordProviderMiss(QStringLiteral("fp-legacy"), qq, QStringLiteral("no-candidate"), {}, 12));
        QVERIFY(store.recordProviderMiss(QStringLiteral("fp-title-quote"), qq, QStringLiteral("empty"), {}, 12));
        // Kept: an unrelated qq miss, a qq miss on a line-level track, and
        // other providers' misses, even an "empty" one on a purged fingerprint.
        QVERIFY(store.recordProviderMiss(QStringLiteral("fp-unrelated"), qq, QStringLiteral("no-candidate"), {}, 12));
        QVERIFY(store.recordProviderMiss(QStringLiteral("fp-line"), qq, QStringLiteral("network"), {}, 12));
        QVERIFY(store.recordProviderMiss(QStringLiteral("fp-both"), QStringLiteral("netease"),
                                         QStringLiteral("empty"), {}, 12));

        QVERIFY(store.setOffset(mappedBoth, 250));
        QVERIFY(store.setOffset(lineLevel, -100));
        QVERIFY(store.setPreferredProvider(QStringLiteral("fp-both"), qq, 13));

        QString error;
        const auto purged = store.purgeWordLevelLyricsOnce(qqPurge, &error);
        QVERIFY2(purged.has_value(), qPrintable(error));
        QVERIFY(!purged->alreadyDone);
        QCOMPARE(purged->lyrics, 3);
        QCOMPARE(purged->misses, 4);

        QCOMPARE(sql(path, QStringLiteral(
                     "SELECT provider, track_id, has_words FROM lyric ORDER BY provider, track_id")),
                 QStringList({QStringLiteral("netease|100|1"), QStringLiteral("qq|200|0")}));
        QCOMPARE(sql(path, QStringLiteral(
                     "SELECT fingerprint, provider, reason FROM provider_miss "
                     "ORDER BY fingerprint, provider")),
                 QStringList({QStringLiteral("fp-both|netease|empty"),
                              QStringLiteral("fp-line|qq|network"),
                              QStringLiteral("fp-unrelated|qq|no-candidate")}));
        // Mappings, offsets and the preference are left for the resolver,
        // which searches again when a mapped lyric row is missing.
        QCOMPARE(store.refForFingerprint(QStringLiteral("fp-both"))->trackId, QStringLiteral("100"));
        QCOMPARE(store.refForProvider(QStringLiteral("fp-per-provider"), qq)->trackId,
                 QStringLiteral("101"));
        QCOMPARE(store.refForFingerprint(QStringLiteral("fp-legacy"))->trackId, QStringLiteral("102"));
        QCOMPARE(store.offset(mappedBoth), 250);
        QCOMPARE(store.offset(lineLevel), -100);
        QCOMPARE(store.preferredProvider(QStringLiteral("fp-both")), std::optional<QString>(qq));
        QVERIFY(!store.lyric(mappedBoth).has_value());
        QVERIFY(store.lyric(lineLevel).has_value());
        // Recorded under exactly the name it was given.
        QCOMPARE(sql(path, QStringLiteral(
                     "SELECT name FROM setting WHERE name LIKE 'migration/%'")),
                 QStringList({QStringLiteral("migration/test-purge")}));
    }

    void purgeRunsOnlyOncePerMarker()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.db"));
        LyricStore store(path);
        QVERIFY(store.open());
        // An empty store still runs it and records it: that is what a fresh
        // install looks like, and it must not purge the first real lyrics
        // later.
        const auto first = store.purgeWordLevelLyricsOnce(qqPurge);
        QVERIFY(first.has_value());
        QVERIFY(!first->alreadyDone);
        QCOMPARE(first->lyrics, 0);
        QCOMPARE(first->misses, 0);

        const TrackRef fresh{QStringLiteral("qq"), QStringLiteral("100"), 1};
        QVERIFY(store.putLyric(fresh, oneLine(true), 10));
        QVERIFY(store.recordProviderMiss(QStringLiteral("fp"), QStringLiteral("qq"),
                                         QStringLiteral("empty"), {}, 12));
        const auto second = store.purgeWordLevelLyricsOnce(qqPurge);
        QVERIFY(second.has_value());
        QVERIFY(second->alreadyDone);
        QCOMPARE(second->lyrics, 0);
        QCOMPARE(second->misses, 0);
        QVERIFY(store.lyric(fresh).has_value());
        QCOMPARE(sql(path, QStringLiteral("SELECT count(*) FROM provider_miss")),
                 QStringList({QStringLiteral("1")}));

        // A different marker is a different purge.
        const auto other = store.purgeWordLevelLyricsOnce(
            {.marker = QStringLiteral("migration/test-purge-2"), .provider = QStringLiteral("qq"),
             .emptyMissReason = QStringLiteral("empty")});
        QVERIFY(other.has_value());
        QVERIFY(!other->alreadyDone);
        QCOMPARE(other->lyrics, 1);
        QCOMPARE(other->misses, 1);
    }

    void purgeChangesNothingWhenItFailsAfterTheDeletes()
    {
        // The marker insert is the last statement, so refusing it makes the
        // purge fail after both deletes have already run.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.db"));
        LyricStore store(path);
        QVERIFY(store.open());
        const QString qq = QStringLiteral("qq");
        const TrackRef word{qq, QStringLiteral("100"), 1};
        QVERIFY(store.putLyric(word, oneLine(true), 10));
        QVERIFY(store.mapFingerprint(QStringLiteral("fp-word"), word, 11));
        QVERIFY(store.recordProviderMiss(QStringLiteral("fp-word"), qq, QStringLiteral("network"), {}, 12));
        QVERIFY(store.recordProviderMiss(QStringLiteral("fp-empty"), qq, QStringLiteral("empty"), {}, 12));
        QCOMPARE(sql(path, QStringLiteral(
                     "CREATE TRIGGER refuse_marker BEFORE INSERT ON setting "
                     "WHEN NEW.name = 'migration/test-purge' "
                     "BEGIN SELECT RAISE(ABORT, 'marker refused by the test'); END")),
                 QStringList());

        QString error;
        QVERIFY(!store.purgeWordLevelLyricsOnce(qqPurge, &error).has_value());
        QVERIFY2(error.contains(QStringLiteral("marker refused by the test")), qPrintable(error));
        QCOMPARE(sql(path, QStringLiteral("SELECT provider, track_id FROM lyric")),
                 QStringList({QStringLiteral("qq|100")}));
        QCOMPARE(sql(path, QStringLiteral(
                     "SELECT fingerprint FROM provider_miss ORDER BY fingerprint")),
                 QStringList({QStringLiteral("fp-empty"), QStringLiteral("fp-word")}));
        QCOMPARE(sql(path, QStringLiteral("SELECT count(*) FROM setting WHERE name LIKE 'migration/%'")),
                 QStringList({QStringLiteral("0")}));

        // The transaction was closed, not left open: a write through the same
        // connection is visible from another one straight away.
        const TrackRef later{qq, QStringLiteral("101"), 1};
        QVERIFY(store.putLyric(later, oneLine(true), 13));
        QCOMPARE(sql(path, QStringLiteral("SELECT track_id FROM lyric WHERE track_id='101'")),
                 QStringList({QStringLiteral("101")}));

        // Nothing was recorded, so it runs in full once it can.
        QCOMPARE(sql(path, QStringLiteral("DROP TRIGGER refuse_marker")), QStringList());
        const auto retried = store.purgeWordLevelLyricsOnce(qqPurge, &error);
        QVERIFY2(retried.has_value(), qPrintable(error));
        QVERIFY(!retried->alreadyDone);
        QCOMPARE(retried->lyrics, 2);
        QCOMPARE(retried->misses, 2);
    }

    void purgeMarkerSurvivesAnOlderBuildOpeningTheStore()
    {
        // Every released build writes user_version=2 on open, plasmashell's
        // copy included. A marker kept there would be reset by it, and the
        // purge would run again over lyrics the fixed parser wrote.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("lyrics.db"));
        const TrackRef fresh{QStringLiteral("qq"), QStringLiteral("100"), 1};
        {
            LyricStore store(path);
            QVERIFY(store.open());
            QVERIFY(store.purgeWordLevelLyricsOnce(qqPurge).has_value());
            QVERIFY(store.putLyric(fresh, oneLine(true), 10));
        }
        QCOMPARE(sql(path, QStringLiteral("PRAGMA user_version=2")), QStringList());
        QCOMPARE(sql(path, QStringLiteral("PRAGMA user_version")),
                 QStringList({QStringLiteral("2")}));

        LyricStore reopened(path);
        QVERIFY(reopened.open());
        const auto again = reopened.purgeWordLevelLyricsOnce(qqPurge);
        QVERIFY(again.has_value());
        QVERIFY(again->alreadyDone);
        QVERIFY(reopened.lyric(fresh).has_value());
    }
};

QTEST_GUILESS_MAIN(LyricStoreTest)
#include "tst_lyricstore.moc"
