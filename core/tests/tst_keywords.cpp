// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Keyword cleaning, DESIGN #12 / §1.1. MPRIS hands xesam:artist as a
 * single-element array with one slash-joined DOMString:
 *   ['闹闹丶/FFF君/欧Ωhm/洛天依Official']
 * Searching that string against netease (whose artists are a clean array)
 * fails to match. Split on slashes, trim, drop empties.
 */

#include "match/KeywordCleaner.h"

#include <QTest>

using namespace Lyrics;

class KeywordCleanerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void slashJoinedArtistSplits();
    void realPbiPayload();
    void emptyArtistList();
    void emptyStringsDropped();
    void whitespaceTrimmed();
};

void KeywordCleanerTest::slashJoinedArtistSplits()
{
    const QStringList expected{QStringLiteral("闹闹丶"), QStringLiteral("FFF君")};
    QCOMPARE(cleanArtists({QStringLiteral("闹闹丶/FFF君")}), expected);
}

void KeywordCleanerTest::realPbiPayload()
{
    // The exact payload recorded from pbi (DESIGN §1.1).
    const QStringList expected{QStringLiteral("闹闹丶"), QStringLiteral("FFF君"),
                               QStringLiteral("欧Ωhm"), QStringLiteral("洛天依Official")};
    QCOMPARE(cleanArtists({QStringLiteral("闹闹丶/FFF君/欧Ωhm/洛天依Official")}), expected);
}

void KeywordCleanerTest::emptyArtistList()
{
    // The chromium-tab-title trap reports artist=['']: nothing to search by.
    QVERIFY(cleanArtists({QStringLiteral("")}).isEmpty());
}

void KeywordCleanerTest::emptyStringsDropped()
{
    QCOMPARE(cleanArtists({QStringLiteral(""), QStringLiteral("闹闹丶")}).size(), 1);
}

void KeywordCleanerTest::whitespaceTrimmed()
{
    const QStringList expected{QStringLiteral("闹闹丶")};
    QCOMPARE(cleanArtists({QStringLiteral(" 闹闹丶 ")}), expected);
}

QTEST_MAIN(KeywordCleanerTest)
#include "tst_keywords.moc"
