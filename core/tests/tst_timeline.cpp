#include "core/lyric/timeline.h"

#include <QTest>

using namespace PlasmaLyrics;

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
                          std::nullopt, std::nullopt, true},
                         {5000, 9000, QStringLiteral("若能再相见"), std::nullopt, std::nullopt}};
        const auto filtered = filterLeadingCredits(lines);
        QCOMPARE(filtered.size(), 1);
        QCOMPARE(filtered.first().text, QStringLiteral("若能再相见"));
    }
};

QTEST_GUILESS_MAIN(TimelineTest)
#include "tst_timeline.moc"
