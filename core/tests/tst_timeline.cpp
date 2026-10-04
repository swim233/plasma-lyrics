#include "core/lyric/timeline.h"

#include <QTest>

using namespace PlasmaLyrics;

namespace {

// currentLineIndex(), nextLineIndex() and nextBoundaryMs() as they stood at
// a9e95ec, before the next line lead of DESIGN.md decision 81, kept verbatim
// so that a lead of 0 is checked against the behaviour it has to keep rather
// than against the functions under test.
int referenceCurrentLineIndex(const LyricLines &lines, qint64 positionMs, int offsetMs)
{
    const qint64 adjustedPosition = positionMs - offsetMs;
    for (qsizetype index = lines.size(); index > 0; --index) {
        const auto &line = lines[index - 1];
        if (line.startMs <= adjustedPosition) {
            return adjustedPosition < line.endMs ? static_cast<int>(index - 1) : -1;
        }
    }
    return -1;
}

int referenceNextLineIndex(const LyricLines &lines, qint64 positionMs, int offsetMs)
{
    const qint64 adjustedPosition = positionMs - offsetMs;
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (lines[index].startMs > adjustedPosition) {
            while (index + 1 < lines.size() && lines[index + 1].startMs == lines[index].startMs) {
                ++index;
            }
            return static_cast<int>(index);
        }
    }
    return -1;
}

std::optional<qint64> referenceNextBoundaryMs(const LyricLines &lines, qint64 positionMs, int offsetMs)
{
    const qint64 adjustedPosition = positionMs - offsetMs;
    std::optional<qint64> boundary;
    for (const auto &line : lines) {
        for (const qint64 candidate : {line.startMs, line.endMs}) {
            if (candidate > adjustedPosition && (!boundary || candidate < *boundary)) {
                boundary = candidate;
            }
        }
    }
    if (!boundary) {
        return std::nullopt;
    }
    return *boundary + offsetMs;
}

// A count-in from 0, two LRC lines ending where the next one starts, an
// overlap, a long interlude, three lines sharing a start, two short lines
// with gaps under any lead and a last line.
LyricLines leadFixture()
{
    LyricLines lines{{0, 0, QStringLiteral("count-in"), std::nullopt, std::nullopt},
                     {1000, 0, QStringLiteral("lrc one"), std::nullopt, std::nullopt},
                     {2500, 0, QStringLiteral("lrc two"), std::nullopt, std::nullopt},
                     {3000, 10000, QStringLiteral("long"), std::nullopt, std::nullopt},
                     {4000, 4500, QStringLiteral("inside"), std::nullopt, std::nullopt},
                     {5000, 0, QStringLiteral("verse"), std::nullopt, std::nullopt},
                     {20000, 0, QStringLiteral("two"), std::nullopt, std::nullopt},
                     {20000, 0, QStringLiteral("two alt"), std::nullopt, std::nullopt},
                     {20000, 0, QStringLiteral("two third"), std::nullopt, std::nullopt},
                     {21000, 21400, QStringLiteral("short"), std::nullopt, std::nullopt},
                     {21500, 21600, QStringLiteral("dense"), std::nullopt, std::nullopt},
                     {22000, 0, QStringLiteral("last"), std::nullopt, std::nullopt}};
    finalizeEndTimes(lines);
    return lines;
}

} // namespace

class TimelineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void boundariesAndInterlude()
    {
        LyricLines lines{{1000, 2000, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {5000, 6000, QStringLiteral("two"), std::nullopt, std::nullopt},
                         {5000, 6000, QStringLiteral("two alt"), std::nullopt, std::nullopt}};
        QCOMPARE(currentLineIndex(lines, 999), -1);
        QCOMPARE(currentLineIndex(lines, 1000), 0);
        QCOMPARE(currentLineIndex(lines, 3000), -1);
        QCOMPARE(currentLineIndex(lines, 5000), 2);
        QCOMPARE(currentLineIndex(lines, 6000), -1);
        QCOMPARE(currentLineIndex(lines, 5500, 500), 2);
    }

    void armsToTheNextBoundary()
    {
        LyricLines lines{{1000, 2000, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {5000, 6000, QStringLiteral("two"), std::nullopt, std::nullopt},
                         {5000, 6000, QStringLiteral("two alt"), std::nullopt, std::nullopt}};
        const auto boundary = [&lines](qint64 positionMs, int offsetMs = 0) {
            return nextBoundaryMs(lines, positionMs, offsetMs).value_or(-1);
        };
        QCOMPARE(boundary(0), 1000);         // the first line appears
        QCOMPARE(boundary(1000), 2000);      // its own end, where the display clears
        QCOMPARE(boundary(2000), 5000);      // one wake-up spans the whole interlude
        QCOMPARE(boundary(5000), 6000);      // the duplicated timestamps collapse
        QCOMPARE(boundary(5500, 500), 6500); // the offset shifts the wake-up as well
        QVERIFY(!nextBoundaryMs(lines, 6000));
        QVERIFY(!nextBoundaryMs({}, 0));
    }

    void boundaryCatchesAnOverlappingEnd()
    {
        // "short" ends while "long" is still nominally running, so ends are not
        // ordered. Stopping the scan at the first start after the position would
        // answer 4000 here and leave the widget showing a line 1s too long.
        LyricLines lines{{0, 10000, QStringLiteral("long"), std::nullopt, std::nullopt},
                         {2000, 3000, QStringLiteral("short"), std::nullopt, std::nullopt},
                         {4000, 5000, QStringLiteral("next"), std::nullopt, std::nullopt}};
        QCOMPARE(currentLineIndex(lines, 2500), 1);
        QCOMPARE(nextBoundaryMs(lines, 2500).value_or(-1), 3000);
        QCOMPARE(currentLineIndex(lines, 3000), -1);
    }

    void nextLineIsTheFirstNotYetStarted()
    {
        LyricLines lines{{1000, 2000, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {3000, 4000, QStringLiteral("two"), std::nullopt, std::nullopt},
                         {5000, 6000, QStringLiteral("three"), std::nullopt, std::nullopt}};
        QCOMPARE(nextLineIndex({}, 0), -1);
        QCOMPARE(nextLineIndex(lines, 0), 0);    // before the first line
        QCOMPARE(nextLineIndex(lines, 999), 0);
        QCOMPARE(nextLineIndex(lines, 1500), 1); // inside a line
        QCOMPARE(nextLineIndex(lines, 2500), 1); // in the gap after it
        QCOMPARE(nextLineIndex(lines, 2999), 1);
        // A line starting exactly now is current and cannot be next as well.
        QCOMPARE(currentLineIndex(lines, 3000), 1);
        QCOMPARE(nextLineIndex(lines, 3000), 2);
        QCOMPARE(currentLineIndex(lines, 5000), 2); // the last line
        QCOMPARE(nextLineIndex(lines, 5000), -1);
        QCOMPARE(nextLineIndex(lines, 6000), -1);   // and after it
        QCOMPARE(nextLineIndex(lines, 60000), -1);
    }

    void nextLineOfSeveralSharingAStartIsTheLast()
    {
        // The same fixture as boundariesAndInterlude: currentLineIndex() settles
        // on the last of the lines sharing 5000, so that is the one coming up.
        LyricLines lines{{1000, 2000, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {5000, 6000, QStringLiteral("two"), std::nullopt, std::nullopt},
                         {5000, 6000, QStringLiteral("two alt"), std::nullopt, std::nullopt}};
        QCOMPARE(nextLineIndex(lines, 1000), 2);
        QCOMPARE(nextLineIndex(lines, 4999), 2);
        QCOMPARE(currentLineIndex(lines, 5000), 2);
        QCOMPARE(nextLineIndex(lines, 5000), -1);

        LyricLines opening{{1000, 2000, QStringLiteral("one"), std::nullopt, std::nullopt},
                           {1000, 2000, QStringLiteral("one alt"), std::nullopt, std::nullopt},
                           {3000, 4000, QStringLiteral("two"), std::nullopt, std::nullopt}};
        QCOMPARE(nextLineIndex(opening, 0), 1);
        QCOMPARE(nextLineIndex(opening, 1000), 2);
    }

    void nextLineIgnoresAnOverlappingEnd()
    {
        // "long" is still nominally running when "short" starts and after it
        // ends; the next line is decided by starts alone.
        LyricLines lines{{0, 10000, QStringLiteral("long"), std::nullopt, std::nullopt},
                         {2000, 3000, QStringLiteral("short"), std::nullopt, std::nullopt},
                         {4000, 5000, QStringLiteral("next"), std::nullopt, std::nullopt}};
        QCOMPARE(currentLineIndex(lines, 1000), 0);
        QCOMPARE(nextLineIndex(lines, 1000), 1);
        QCOMPARE(currentLineIndex(lines, 2500), 1);
        QCOMPARE(nextLineIndex(lines, 2500), 2);
        QCOMPARE(currentLineIndex(lines, 3500), -1);
        QCOMPARE(nextLineIndex(lines, 3500), 2);
        QCOMPARE(nextLineIndex(lines, 4000), -1);
    }

    void nextLineShowsThroughALongInterlude()
    {
        // finalizeEndTimes() cuts a line off 10 s after it starts, so for most
        // of this interlude nothing is current -- the next line still is.
        LyricLines lines{{1000, 0, QStringLiteral("verse"), std::nullopt, std::nullopt},
                         {40000, 0, QStringLiteral("after"), std::nullopt, std::nullopt}};
        finalizeEndTimes(lines);
        QCOMPARE(lines[0].endMs, 11000);
        QCOMPARE(currentLineIndex(lines, 500), -1); // the intro
        QCOMPARE(nextLineIndex(lines, 500), 0);
        QCOMPARE(nextLineIndex(lines, 10999), 1);
        QCOMPARE(currentLineIndex(lines, 11000), -1);
        QCOMPARE(nextLineIndex(lines, 11000), 1);
        QCOMPARE(currentLineIndex(lines, 39999), -1);
        QCOMPARE(nextLineIndex(lines, 39999), 1);
        QCOMPARE(currentLineIndex(lines, 40000), 1);
        QCOMPARE(nextLineIndex(lines, 40000), -1);
    }

    void nextLineAppliesTheOffsetLikeTheCurrentLine()
    {
        LyricLines lines{{1000, 2000, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {3000, 4000, QStringLiteral("two"), std::nullopt, std::nullopt},
                         {5000, 6000, QStringLiteral("three"), std::nullopt, std::nullopt}};
        // A positive offset shows the lyrics later.
        QCOMPARE(nextLineIndex(lines, 3499, 500), 1);
        QCOMPARE(currentLineIndex(lines, 3500, 500), 1);
        QCOMPARE(nextLineIndex(lines, 3500, 500), 2);
        // A negative one earlier.
        QCOMPARE(nextLineIndex(lines, 2499, -500), 1);
        QCOMPARE(currentLineIndex(lines, 2500, -500), 1);
        QCOMPARE(nextLineIndex(lines, 2500, -500), 2);
        QCOMPARE(nextLineIndex(lines, 499, -500), 0);
        QCOMPARE(nextLineIndex(lines, 500, -500), 1);
        QCOMPARE(nextLineIndex(lines, 4499, -500), 2);
        QCOMPARE(nextLineIndex(lines, 4500, -500), -1);
    }

    void nextLineBecomesCurrentAndWakesOnABoundary()
    {
        // Every millisecond across a timeline with an intro, an overlap, a long
        // interlude, three lines sharing a start (so skipping just one of them
        // is caught too) and a last line, under three offsets. The
        // animation takes a new current line that equals the previous next one
        // as the song moving on (DESIGN.md decision 28), and the widget only
        // recomputes on nextBoundaryMs() (decision 38), so a change of the next
        // line between two boundaries would never reach the screen.
        LyricLines lines{{1000, 2000, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {3000, 10000, QStringLiteral("long"), std::nullopt, std::nullopt},
                         {4000, 4500, QStringLiteral("inside"), std::nullopt, std::nullopt},
                         {5000, 0, QStringLiteral("verse"), std::nullopt, std::nullopt},
                         {20000, 0, QStringLiteral("two"), std::nullopt, std::nullopt},
                         {20000, 0, QStringLiteral("two alt"), std::nullopt, std::nullopt},
                         {20000, 0, QStringLiteral("two third"), std::nullopt, std::nullopt},
                         {22000, 0, QStringLiteral("last"), std::nullopt, std::nullopt}};
        finalizeEndTimes(lines);
        QCOMPARE(lines[3].endMs, 15000); // five seconds with nothing current
        for (const int offsetMs : {0, 700, -700}) {
            for (qint64 position = 1; position <= 35000; ++position) {
                const int current = currentLineIndex(lines, position, offsetMs);
                const int next = nextLineIndex(lines, position, offsetMs);
                const int previousNext = nextLineIndex(lines, position - 1, offsetMs);
                if (current >= 0 && current != currentLineIndex(lines, position - 1, offsetMs)
                    && current != previousNext) {
                    QFAIL(qPrintable(QStringLiteral("offset %1 ms, at %2 ms: line %3 became current, %4 was next")
                                         .arg(offsetMs).arg(position).arg(current).arg(previousNext)));
                }
                if (next != previousNext
                    && nextBoundaryMs(lines, position - 1, offsetMs).value_or(-1) != position) {
                    QFAIL(qPrintable(QStringLiteral("offset %1 ms, at %2 ms: the next line moved from %3 to %4 "
                                                    "without a boundary there")
                                         .arg(offsetMs).arg(position).arg(previousNext).arg(next)));
                }
            }
        }
    }

    // ---- The next line lead (DESIGN.md decision 81)

    void theFirstLineEntersTheLeadBeforeItStarts()
    {
        LyricLines lines{{3000, 4000, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {6000, 7000, QStringLiteral("two"), std::nullopt, std::nullopt}};
        QCOMPARE(lineEntryMs(lines, 0, 2000), 1000);
        QCOMPARE(currentLineIndex(lines, 999, 0, 2000), -1);
        QCOMPARE(nextLineIndex(lines, 999, 0, 2000), 0);
        QCOMPARE(currentLineIndex(lines, 1000, 0, 2000), 0);
        QCOMPARE(nextLineIndex(lines, 1000, 0, 2000), 1);
        QCOMPARE(nextBoundaryMs(lines, 0, 0, 2000).value_or(-1), 1000);

        // A first line starting sooner than the lead is in place from the
        // very beginning of the track.
        LyricLines early{{1500, 2500, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {6000, 7000, QStringLiteral("two"), std::nullopt, std::nullopt}};
        QCOMPARE(lineEntryMs(early, 0, 2000), -500);
        QCOMPARE(currentLineIndex(early, 0, 0, 2000), 0);
        QCOMPARE(nextLineIndex(early, 0, 0, 2000), 1);
    }

    void aGapShorterThanTheLeadEntersWhenTheLineAheadEnds()
    {
        LyricLines lines{{1000, 2000, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {3000, 4000, QStringLiteral("two"), std::nullopt, std::nullopt},
                         {9000, 9500, QStringLiteral("three"), std::nullopt, std::nullopt}};
        QCOMPARE(lineEntryMs(lines, 1, 2000), 2000);
        QCOMPARE(currentLineIndex(lines, 1999, 0, 2000), 0);
        QCOMPARE(nextLineIndex(lines, 1999, 0, 2000), 1);
        // Straight from the line sung to the next, with nothing in between.
        QCOMPARE(currentLineIndex(lines, 2000, 0, 2000), 1);
        QCOMPARE(nextLineIndex(lines, 2000, 0, 2000), 2);
        QCOMPARE(currentLineIndex(lines, 2999, 0, 2000), 1);
        QCOMPARE(currentLineIndex(lines, 3000, 0, 2000), 1);
    }

    void aGapLongerThanTheLeadEntersTheLeadBeforeTheStart()
    {
        LyricLines lines{{1000, 2000, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {6000, 7000, QStringLiteral("two"), std::nullopt, std::nullopt}};
        QCOMPARE(lineEntryMs(lines, 1, 2000), 4000);
        QCOMPARE(currentLineIndex(lines, 2000, 0, 2000), -1);
        QCOMPARE(nextLineIndex(lines, 2000, 0, 2000), 1);
        QCOMPARE(currentLineIndex(lines, 3999, 0, 2000), -1);
        QCOMPARE(nextLineIndex(lines, 3999, 0, 2000), 1);
        QCOMPARE(currentLineIndex(lines, 4000, 0, 2000), 1);
        QCOMPARE(nextLineIndex(lines, 4000, 0, 2000), -1);
        QCOMPARE(currentLineIndex(lines, 6999, 0, 2000), 1);
        QCOMPARE(currentLineIndex(lines, 7000, 0, 2000), -1);
    }

    void contiguousLrcLinesKeepTheirSwitchTimes()
    {
        // finalizeEndTimes() ends each LRC line where the next one starts, so
        // past the intro there is no gap to move into: every lead gives the
        // answers of no lead from the first start on.
        LyricLines lines{{10000, 0, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {11200, 0, QStringLiteral("two"), std::nullopt, std::nullopt},
                         {13000, 0, QStringLiteral("three"), std::nullopt, std::nullopt},
                         {13500, 0, QStringLiteral("four"), std::nullopt, std::nullopt}};
        finalizeEndTimes(lines);
        for (const int leadMs : {500, 2000, 5000}) {
            for (qsizetype index = 1; index < lines.size(); ++index) {
                QCOMPARE(lineEntryMs(lines, index, leadMs), lines[index].startMs);
            }
            for (qint64 position = 10000; position <= 25000; ++position) {
                QCOMPARE(currentLineIndex(lines, position, 0, leadMs), currentLineIndex(lines, position));
                QCOMPARE(nextLineIndex(lines, position, 0, leadMs), nextLineIndex(lines, position));
            }
        }
    }

    void denseShortLinesAreEachInPlaceWhenSung()
    {
        // Starting every lead before the line's start would put the second
        // of these in place at 13.2 s and the third at 14.3 s: neither the
        // first nor the second would ever be in place while it is sung. Each
        // is in place from its start to its end, LRC and with gaps alike.
        LyricLines lrc{{14000, 0, QStringLiteral("one"), std::nullopt, std::nullopt},
                       {15200, 0, QStringLiteral("two"), std::nullopt, std::nullopt},
                       {16300, 0, QStringLiteral("three"), std::nullopt, std::nullopt}};
        finalizeEndTimes(lrc);
        LyricLines gaps{{14000, 15000, QStringLiteral("one"), std::nullopt, std::nullopt},
                        {15200, 16100, QStringLiteral("two"), std::nullopt, std::nullopt},
                        {16300, 17500, QStringLiteral("three"), std::nullopt, std::nullopt}};
        for (const LyricLines &lines : {lrc, gaps}) {
            for (qsizetype index = 0; index < lines.size(); ++index) {
                for (qint64 position = lines[index].startMs; position < lines[index].endMs; ++position) {
                    QCOMPARE(currentLineIndex(lines, position, 0, 2000), static_cast<int>(index));
                }
            }
        }
        QCOMPARE(lineEntryMs(gaps, 1, 2000), 15000);
        QCOMPARE(lineEntryMs(gaps, 2, 2000), 16100);
    }

    void anOverlappingLineEntersAtItsStart()
    {
        // "long" is still running when "overlap" starts: the newer line takes
        // the place at its start, neither a lead earlier nor once "long" ends.
        LyricLines lines{{0, 5000, QStringLiteral("long"), std::nullopt, std::nullopt},
                         {4000, 6000, QStringLiteral("overlap"), std::nullopt, std::nullopt}};
        QCOMPARE(lineEntryMs(lines, 1, 2000), 4000);
        QCOMPARE(currentLineIndex(lines, 3999, 0, 2000), 0);
        QCOMPARE(nextLineIndex(lines, 3999, 0, 2000), 1);
        QCOMPARE(currentLineIndex(lines, 4000, 0, 2000), 1);
        QCOMPARE(nextLineIndex(lines, 4000, 0, 2000), -1);
    }

    void linesSharingAStartShareTheirEntry()
    {
        LyricLines lines{{1000, 2000, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {5000, 6000, QStringLiteral("two"), std::nullopt, std::nullopt},
                         {5000, 6000, QStringLiteral("two alt"), std::nullopt, std::nullopt}};
        QCOMPARE(lineEntryMs(lines, 1, 2000), 3000);
        QCOMPARE(lineEntryMs(lines, 2, 2000), 3000);
        QCOMPARE(nextLineIndex(lines, 2999, 0, 2000), 2);
        QCOMPARE(currentLineIndex(lines, 3000, 0, 2000), 2);
        QCOMPARE(nextLineIndex(lines, 3000, 0, 2000), -1);

        // A group at the very top enters together too, and the line ahead of
        // the group after it is the last of the group, whatever the end of
        // the others.
        LyricLines opening{{1000, 4000, QStringLiteral("one"), std::nullopt, std::nullopt},
                           {1000, 2000, QStringLiteral("one alt"), std::nullopt, std::nullopt},
                           {5000, 6000, QStringLiteral("two"), std::nullopt, std::nullopt}};
        QCOMPARE(lineEntryMs(opening, 0, 2000), -1000);
        QCOMPARE(lineEntryMs(opening, 1, 2000), -1000);
        QCOMPARE(currentLineIndex(opening, 0, 0, 2000), 1);
        QCOMPARE(lineEntryMs(opening, 2, 2000), 3000);
    }

    void theOffsetMovesTheEntry()
    {
        LyricLines lines{{1000, 2000, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {5000, 6000, QStringLiteral("two"), std::nullopt, std::nullopt}};
        // A positive offset shows the lyrics later, a negative one earlier.
        QCOMPARE(currentLineIndex(lines, 3499, 500, 2000), -1);
        QCOMPARE(nextLineIndex(lines, 3499, 500, 2000), 1);
        QCOMPARE(currentLineIndex(lines, 3500, 500, 2000), 1);
        QCOMPARE(nextLineIndex(lines, 3500, 500, 2000), -1);
        QCOMPARE(nextBoundaryMs(lines, 2600, 500, 2000).value_or(-1), 3500);
        QCOMPARE(currentLineIndex(lines, 2499, -500, 2000), -1);
        QCOMPARE(currentLineIndex(lines, 2500, -500, 2000), 1);
        QCOMPARE(nextBoundaryMs(lines, 1600, -500, 2000).value_or(-1), 2500);
    }

    void theBoundariesIncludeTheEntries()
    {
        LyricLines lines{{1000, 2000, QStringLiteral("one"), std::nullopt, std::nullopt},
                         {6000, 7000, QStringLiteral("two"), std::nullopt, std::nullopt},
                         {6000, 7000, QStringLiteral("two alt"), std::nullopt, std::nullopt}};
        // Starts stay candidates, although once the line is in place its
        // start changes nothing.
        QCOMPARE(nextBoundaryMs(lines, 0, 0, 2000).value_or(-1), 1000);
        QCOMPARE(nextBoundaryMs(lines, 2000, 0, 2000).value_or(-1), 4000); // the shared entry
        QCOMPARE(nextBoundaryMs(lines, 4000, 0, 2000).value_or(-1), 6000);
        QCOMPARE(nextBoundaryMs(lines, 2000, 0, 0).value_or(-1), 6000);
        // A negative lead counts as none, the first line's included.
        QCOMPARE(lineEntryMs(lines, 0, -500), 1000);
        QCOMPARE(currentLineIndex(lines, 1000, 0, -500), 0);
        QCOMPARE(lineEntryMs(lines, 1, -500), 6000);
        QCOMPARE(nextBoundaryMs(lines, 2000, 0, -500).value_or(-1), 6000);
        QCOMPARE(currentLineIndex(lines, 5999, 0, -500), -1);
        QVERIFY(!nextBoundaryMs(lines, 7000, 0, 2000));
    }

    void noLeadKeepsEveryAnswerOfBefore()
    {
        // Every millisecond across the fixture, under three offsets: with a
        // lead of 0 all three functions answer exactly as before the lead
        // existed.
        const LyricLines lines = leadFixture();
        for (const int offsetMs : {0, 700, -700}) {
            for (qint64 position = -1000; position <= 35000; ++position) {
                const int current = currentLineIndex(lines, position, offsetMs, 0);
                if (current != referenceCurrentLineIndex(lines, position, offsetMs)) {
                    QFAIL(qPrintable(QStringLiteral("offset %1 ms, at %2 ms: current line %3, was %4")
                                         .arg(offsetMs).arg(position).arg(current)
                                         .arg(referenceCurrentLineIndex(lines, position, offsetMs))));
                }
                const int next = nextLineIndex(lines, position, offsetMs, 0);
                if (next != referenceNextLineIndex(lines, position, offsetMs)) {
                    QFAIL(qPrintable(QStringLiteral("offset %1 ms, at %2 ms: next line %3, was %4")
                                         .arg(offsetMs).arg(position).arg(next)
                                         .arg(referenceNextLineIndex(lines, position, offsetMs))));
                }
                const auto boundary = nextBoundaryMs(lines, position, offsetMs, 0);
                if (boundary != referenceNextBoundaryMs(lines, position, offsetMs)) {
                    QFAIL(qPrintable(QStringLiteral("offset %1 ms, at %2 ms: boundary %3, was %4")
                                         .arg(offsetMs).arg(position).arg(boundary.value_or(-1))
                                         .arg(referenceNextBoundaryMs(lines, position, offsetMs).value_or(-1))));
                }
            }
        }
    }

    void aLeadStillAdvancesInSequenceOnBoundaries()
    {
        // nextLineBecomesCurrentAndWakesOnABoundary under a lead: a line
        // entering early is still the next line recorded just before
        // (DESIGN.md decision 28), and every change of either index lands on
        // a boundary (decision 38).
        const LyricLines lines = leadFixture();
        for (const int leadMs : {0, 1500, 2000, 5000}) {
            for (const int offsetMs : {0, 700, -700}) {
                for (qint64 position = -6000; position <= 35000; ++position) {
                    const int current = currentLineIndex(lines, position, offsetMs, leadMs);
                    const int previousCurrent = currentLineIndex(lines, position - 1, offsetMs, leadMs);
                    const int next = nextLineIndex(lines, position, offsetMs, leadMs);
                    const int previousNext = nextLineIndex(lines, position - 1, offsetMs, leadMs);
                    if (current >= 0 && current != previousCurrent && current != previousNext) {
                        QFAIL(qPrintable(QStringLiteral("lead %1 ms, offset %2 ms, at %3 ms: line %4 became "
                                                        "current, %5 was next")
                                             .arg(leadMs).arg(offsetMs).arg(position).arg(current)
                                             .arg(previousNext)));
                    }
                    if ((current != previousCurrent || next != previousNext)
                        && nextBoundaryMs(lines, position - 1, offsetMs, leadMs).value_or(-1) != position) {
                        QFAIL(qPrintable(QStringLiteral("lead %1 ms, offset %2 ms, at %3 ms: a line moved "
                                                        "without a boundary there")
                                             .arg(leadMs).arg(offsetMs).arg(position)));
                    }
                }
            }
        }
    }

    void filtersOnlyLeadingCredits()
    {
        LyricLines lines{{2800, 5000, QStringLiteral("编曲/伴奏混音：闹闹丶"), std::nullopt, std::nullopt},
                         {5600, 8000, QStringLiteral("调教：FFF君"), std::nullopt, std::nullopt},
                         {28630, 31000, QStringLiteral("若能再相见"), std::nullopt, std::nullopt},
                         {32000, 35000, QStringLiteral("我说：你听"), std::nullopt, std::nullopt}};
        const auto filtered = filterLeadingCredits(lines);
        QCOMPARE(filtered.size(), 2);
        QCOMPARE(filtered.first().text, QStringLiteral("若能再相见"));
    }

    void filtersCreditsWhateverPadsTheColon()
    {
        // What /api/song/lyric returns for NetEase 2699991455 once timestamps are
        // stripped. The padding around the colon used to fail the shape check on
        // the very first line, which ended the scan and let every credit through.
        LyricLines lines{{0, 1, QStringLiteral("作词 : 爆音常安"), std::nullopt, std::nullopt},
                         {1, 2000, QStringLiteral("歌手：洛天依/乐正绫"), std::nullopt, std::nullopt},
                         {2000, 10672, QStringLiteral("作曲：爆音常安"), std::nullopt, std::nullopt},
                         {10672, 13355, QStringLiteral("蝴蝶轻吻花瓣而颤动"), std::nullopt, std::nullopt}};
        const auto filtered = filterLeadingCredits(lines);
        QCOMPARE(filtered.size(), 1);
        QCOMPARE(filtered.first().text, QStringLiteral("蝴蝶轻吻花瓣而颤动"));
    }

    void trustsProviderFlaggedCreditsOverShape()
    {
        // A structured credit entry the shape check would never have matched:
        // the head is too long and carries a space.
        LyricLines lines{{0, 1, QStringLiteral("Mix&Mastering by Foo Bar"),
                          std::nullopt, std::nullopt, std::nullopt, true},
                         {5000, 9000, QStringLiteral("若能再相见"), std::nullopt, std::nullopt}};
        const auto filtered = filterLeadingCredits(lines);
        QCOMPARE(filtered.size(), 1);
        QCOMPARE(filtered.first().text, QStringLiteral("若能再相见"));
    }

    void looksLikeCreditMatchesShapeOrProviderFlag()
    {
        QVERIFY(looksLikeCredit({0, 0, QStringLiteral("作词：想边"), std::nullopt, std::nullopt}));
        QVERIFY(looksLikeCredit({0, 0, QStringLiteral("作词 : 爆音常安"), std::nullopt, std::nullopt}));
        QVERIFY(looksLikeCredit({0, 0, QStringLiteral("Mix&Mastering by Foo Bar"),
                                 std::nullopt, std::nullopt, std::nullopt, true}));
    }

    void looksLikeCreditRejectsOrdinaryLyricLines()
    {
        QVERIFY(!looksLikeCredit({0, 0, QStringLiteral("若能再相见"), std::nullopt, std::nullopt}));
        QVERIFY(!looksLikeCredit({0, 0, QStringLiteral("蝴蝶轻吻花瓣而颤动"), std::nullopt, std::nullopt}));
    }
};

QTEST_GUILESS_MAIN(TimelineTest)
#include "tst_timeline.moc"
