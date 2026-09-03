// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * "Given a position, which line is current?" -- DESIGN §6 test 2. The edges:
 * before the first line, after the last (open) line, interludes (a line's
 * endMs to the next startMs shows the *next* line only once reached), and
 * several lines sharing one timestamp.
 */

#include "lyric/LyricTimeline.h"

#include <QTest>

using namespace Lyrics;

class TimelineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void beforeFirstLineShowsNothing();
    void insideLineShowsIt();
    void afterLastLineHoldsIt();
    void interludeShowsNothingUntilNextStart();
    void sameStampLines();
};

void TimelineTest::beforeFirstLineShowsNothing()
{
    LyricLines lines;
    lines << LyricLine{10000, 20000, QStringLiteral("一")};
    LyricTimeline t(lines);
    QCOMPARE(t.lineAt(0), -1);
    QCOMPARE(t.lineAt(9999), -1);
}

void TimelineTest::insideLineShowsIt()
{
    LyricLines lines;
    lines << LyricLine{10000, 20000, QStringLiteral("一")};
    lines << LyricLine{20000, 30000, QStringLiteral("二")};
    LyricTimeline t(lines);
    QCOMPARE(t.lineAt(10000), 0);
    QCOMPARE(t.lineAt(15000), 0);
    QCOMPARE(t.lineAt(19999), 0);
    QCOMPARE(t.lineAt(20000), 1);
    QCOMPARE(t.lineAt(25000), 1);
}

void TimelineTest::afterLastLineHoldsIt()
{
    LyricLines lines;
    lines << LyricLine{10000, 20000, QStringLiteral("一")};
    lines << LyricLine{20000, 20000, QStringLiteral("二")}; // open end
    LyricTimeline t(lines);
    QCOMPARE(t.lineAt(60000), 1);
}

void TimelineTest::interludeShowsNothingUntilNextStart()
{
    // [00:10-00:12]一  [00:30]二: between 12s and 30s there is no line.
    LyricLines lines;
    lines << LyricLine{10000, 12000, QStringLiteral("一")};
    lines << LyricLine{30000, 30000, QStringLiteral("二")};
    LyricTimeline t(lines);
    QCOMPARE(t.lineAt(12000), 0);
    QCOMPARE(t.lineAt(12001), -1);
    QCOMPARE(t.lineAt(29999), -1);
    QCOMPARE(t.lineAt(30000), 1);
}

void TimelineTest::sameStampLines()
{
    // Two lines at the same instant (translation or reprise): the later one
    // wins as "current", since it is what replaced the earlier.
    LyricLines lines;
    lines << LyricLine{10000, 10000, QStringLiteral("一")};
    lines << LyricLine{10000, 20000, QStringLiteral("二")};
    LyricTimeline t(lines);
    QCOMPARE(t.lineAt(15000), 1);
}

QTEST_MAIN(TimelineTest)
#include "tst_timeline.moc"
