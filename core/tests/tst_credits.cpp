// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Production-credits heuristic, DESIGN #29 / §6.1. Rule: a line is a credits
 * line when it matches `^[^\s：:]{1,12}[：:]\s*.+$`; scanning runs from the
 * first line and stops at the first non-matching line (that line is the first
 * real lyric). Constrained two ways on purpose -- intro region only + colon
 * shape -- because the regex alone would silently eat real lyrics like
 * "我说：你听" mid-song.
 *
 * Known misses (accepted, §6.1): `Mix&Mastering: Foo Bar` (13 chars before the
 * colon) and `Vocal 调整：某某` (space before the colon) stay on screen.
 *
 * Verified lines come from 老街北 (1299289240) -- the fixture's real first 7
 * lines, all of which waylyrics' ^作词/^作曲 filter misses.
 */

#include "lyric/CreditsHeuristic.h"

#include <QTest>

using namespace Lyrics;

class CreditsHeuristicTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void realFixtureCreditsAreFiltered();
    void firstNonMatchingLineStopsTheScan();
    void colonInsideLyricsBeforeFirstRealLine();
    void knownMissesStayVisible();
    void allCreditsLeavesEverythingFiltered();
    void emptyInput();
};

void CreditsHeuristicTest::realFixtureCreditsAreFiltered()
{
    // 老街北's real opening: 7 credits lines, first real lyric at 28.63s.
    const QStringList lines = {
        QStringLiteral("作词: 初繁言"),
        QStringLiteral("作曲: 闹闹丶"),
        QStringLiteral("编曲/伴奏混音：闹闹丶"),
        QStringLiteral("调教：FFF君"),
        QStringLiteral("混音：小欧Ω"),
        QStringLiteral("曲绘：偶尤大肥羊"),
        QStringLiteral("PV/封面设计：Ansa"),
        QStringLiteral("文案：铭言君，Ansa"),
        QStringLiteral("歌姬：洛天依"),
        QStringLiteral("若能再相见"),
    };
    const int first = firstRealLyricLine(lines);
    QCOMPARE(first, 9);
}

void CreditsHeuristicTest::firstNonMatchingLineStopsTheScan()
{
    // A non-matching line ends the scan even if credits-shaped lines follow
    // it: only the intro region is ever touched.
    const QStringList lines = {
        QStringLiteral("作词: 某人"),
        QStringLiteral("若能再相见"),
        QStringLiteral("混音：小欧Ω"),
    };
    QCOMPARE(firstRealLyricLine(lines), 1);
}

void CreditsHeuristicTest::colonInsideLyricsBeforeFirstRealLine()
{
    // Accepted false-positive risk (§6.1): a lyric sentence with a colon that
    // happens to sit before the first real line gets eaten. The constraint is
    // that this can only ever happen in the intro region.
    const QStringList lines = {
        QStringLiteral("我说：你听"),
        QStringLiteral("若能再相见"),
    };
    QCOMPARE(firstRealLyricLine(lines), 1);
}

void CreditsHeuristicTest::knownMissesStayVisible()
{
    // Documented misses (§6.1): 13 chars before the colon, or a space before
    // it. They are not credits by this rule and stay on screen.
    QCOMPARE(firstRealLyricLine({QStringLiteral("Mix&Mastering: Foo Bar")}), 0);
    QCOMPARE(firstRealLyricLine({QStringLiteral("Vocal 调整：某某 某某")}), 0);
}

void CreditsHeuristicTest::allCreditsLeavesEverythingFiltered()
{
    const QStringList lines = {
        QStringLiteral("作词: 某人"),
        QStringLiteral("作曲: 某人"),
    };
    QCOMPARE(firstRealLyricLine(lines), -1);
}

void CreditsHeuristicTest::emptyInput()
{
    QCOMPARE(firstRealLyricLine({}), -1);
}

QTEST_MAIN(CreditsHeuristicTest)
#include "tst_credits.moc"
