// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Netease provider parsing, against the real recorded fixtures (DESIGN §6).
 * The fixtures carry the dirty shapes this provider must survive offline:
 *   - the search body leads with a huge `result.songs[]` array;
 *   - the lyric body's `lrc.lyric` leads with JSON credit lines;
 *   - `klyric` is always empty; `yrc` is absent (DESIGN §1.2).
 *
 * Run with: ctest --test-dir build --output-on-failure -R netease_parse
 */

#include "netease/NeteaseParse.h"

#include "lyric/LrcParser.h"

#include <QFile>
#include <QTest>

using namespace Lyrics;

class NeteaseParseTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fixtureFilesExist();
    void searchParsesRealCandidates();
    void searchMagicChefThreeVersions();
    void lyricExtractsLrcText();
    void lyricFeedsLrcParserWithJsonCredits();
    void lyricHandlesMissingLrc();
    void searchHandlesGarbageBody();

private:
    static QByteArray readFixture(const QString &name);
};

QByteArray NeteaseParseTest::readFixture(const QString &name)
{
    QFile f(QStringLiteral("fixtures/netease/") + name);
    QVERIFY(f.open(QIODevice::ReadOnly));
    return f.readAll();
}

void NeteaseParseTest::fixtureFilesExist()
{
    QVERIFY(QFile::exists(QStringLiteral("fixtures/netease/search-laojiebei.json")));
    QVERIFY(QFile::exists(QStringLiteral("fixtures/netease/lyric-1299289240.json")));
}

void NeteaseParseTest::searchParsesRealCandidates()
{
    const QList<NeteaseSong> songs = parseNeteaseSearch(readFixture(QStringLiteral("search-laojiebei.json")));
    QVERIFY(!songs.isEmpty());
    // The recorded response's first hit is the album version, id 72118172.
    bool found = false;
    for (const NeteaseSong &s : songs) {
        if (s.trackId == QLatin1String("72118172")) {
            found = true;
            QCOMPARE(s.title, QStringLiteral("老街北"));
            QVERIFY(!s.artists.isEmpty());
            QVERIFY(s.durationMs > 200000); // ~227s per DESIGN §1.1
        }
    }
    QVERIFY(found);
}

void NeteaseParseTest::searchMagicChefThreeVersions()
{
    // DESIGN §6 test 3's real candidates, straight from the recorded body.
    const QList<NeteaseSong> songs = parseNeteaseSearch(readFixture(QStringLiteral("search-mofachuniang.json")));
    QHash<QString, qint64> byId;
    for (const NeteaseSong &s : songs) {
        byId.insert(s.trackId, s.durationMs);
    }
    QCOMPARE(byId.value(QStringLiteral("423776453")), 286066);
    QCOMPARE(byId.value(QStringLiteral("1418713342")), 286289);
    QCOMPARE(byId.value(QStringLiteral("33497601")), 284328);
}

void NeteaseParseTest::lyricExtractsLrcText()
{
    const QString lrc = parseNeteaseLyric(readFixture(QStringLiteral("lyric-1299289240.json")));
    QVERIFY(!lrc.isEmpty());
    QVERIFY(lrc.contains(QStringLiteral("若能再相见")));
}

void NeteaseParseTest::lyricFeedsLrcParserWithJsonCredits()
{
    // The extracted lrc text must survive parseLrc: JSON credit lines first,
    // first real line at 28.63s (DESIGN §1.2).
    const QString lrc = parseNeteaseLyric(readFixture(QStringLiteral("lyric-1299289240.json")));
    const LrcParseResult r = parseLrc(lrc);
    QVERIFY(r.ok);
    QVERIFY(r.lines.size() > 20);
    bool firstRealFound = false;
    for (const LyricLine &line : r.lines) {
        if (line.text == QStringLiteral("若能再相见")) {
            QCOMPARE(line.startMs, 28630);
            firstRealFound = true;
            break;
        }
    }
    QVERIFY(firstRealFound);
}

void NeteaseParseTest::lyricHandlesMissingLrc()
{
    QCOMPARE(parseNeteaseLyric(QByteArrayLiteral("{\"code\":200}")), QString());
    QCOMPARE(parseNeteaseLyric(QByteArrayLiteral("not json")), QString());
}

void NeteaseParseTest::searchHandlesGarbageBody()
{
    QVERIFY(parseNeteaseSearch(QByteArrayLiteral("garbage")).isEmpty());
    QVERIFY(parseNeteaseSearch(QByteArrayLiteral("{}")).isEmpty());
}

QTEST_MAIN(NeteaseParseTest)
#include "tst_netease_parse.moc"
