// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * SQLite cache, DESIGN #14 / §2.1. waylyrics' rule: search results are cached
 * by fingerprint (title + artist join + length bucket); a miss is served
 * from cache with the recorded offset re-applied; a miss past `miss-ttl`
 * is re-searched, not served stale.
 *
 * The real payload is 魔法厨娘's recorded 3-candidate result.
 */

#include "store/SearchCache.h"

#include <QTest>

using namespace Lyrics;

class SearchCacheTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void fingerprintIsStableAcrossRuns();
    void fingerprintDiffersByTitle();
    void fingerprintDiffersByArtistJoin();
    void missRoundTrips();
    void offsetReapplied();
    void missPastTtlIsSearched();

private:
    QString m_dbPath;
};

void SearchCacheTest::initTestCase()
{
    m_dbPath = QDir::temp().filePath(QStringLiteral("searchcache_test.db"));
    QFile::remove(m_dbPath);
}

void SearchCacheTest::fingerprintIsStableAcrossRuns()
{
    TrackQuery q;
    q.title = QStringLiteral("魔法厨娘");
    q.artists = QStringList{QStringLiteral("闹闹丶"), QStringLiteral("FFF君")};
    q.lengthMs = 286066;

    const QString fp1 = fingerprint(q);
    QCOMPARE(fp1, fingerprint(q));
}

void SearchCacheTest::fingerprintDiffersByTitle()
{
    TrackQuery q;
    q.artists = QStringList{QStringLiteral("闹闹丶")};
    q.lengthMs = 286066;
    const QString fp1 = fingerprint(q);

    TrackQuery q2;
    q2.title = QStringLiteral("别的歌");
    q2.artists = QStringList{QStringLiteral("闹闹丶")};
    q2.lengthMs = 286066;

    QVERIFY(fp1 != fingerprint(q2));
}

void SearchCacheTest::fingerprintDiffersByArtistJoin()
{
    TrackQuery q;
    q.title = QStringLiteral("魔法厨娘");
    q.artists = QStringList{QStringLiteral("闹闹丶")};
    q.lengthMs = 286066;
    const QString fp1 = fingerprint(q);

    TrackQuery q2;
    q2.title = QStringLiteral("魔法厨娘");
    q2.artists = QStringList{QStringLiteral("FFF君")};
    q2.lengthMs = 286066;

    QVERIFY(fp1 != fingerprint(q2));
}

void SearchCacheTest::missRoundTrips()
{
    SearchCache cache(m_dbPath);
    TrackQuery q;
    q.title = QStringLiteral("魔法厨娘");
    q.artists = QStringList{QStringLiteral("闹闹丶")};
    q.lengthMs = 286066;

    QCOMPARE(cache.lookup(fingerprint(q)).found, false);
}

void SearchCacheTest::offsetReapplied()
{
    SearchCache cache(m_dbPath);
    TrackQuery q;
    q.title = QStringLiteral("魔法厨娘");
    q.artists = QStringList{QStringLiteral("闹闹丶")};
    q.lengthMs = 286066;

    const qint64 recordedOffsetMs = 120;
    cache.store(fingerprint(q), QStringLiteral("423776453"), recordedOffsetMs);

    const CacheHit hit = cache.lookup(fingerprint(q));
    QCOMPARE(hit.found, true);
    QCOMPARE(hit.trackId, QStringLiteral("423776453"));
    QCOMPARE(hit.offsetMs, recordedOffsetMs);
}

void SearchCacheTest::missPastTtlIsSearched()
{
    SearchCache cache(m_dbPath);
    TrackQuery q;
    q.title = QStringLiteral("魔法厨娘");
    q.artists = QStringList{QStringLiteral("闹闹丶")};
    q.lengthMs = 286066;

    cache.store(fingerprint(q), QStringLiteral("423776453"), 0);
    cache.expirePastTtl(fingerprint(q));

    QCOMPARE(cache.lookup(fingerprint(q)).found, false);
}

QTEST_MAIN(SearchCacheTest)
#include "tst_searchcache.moc"
