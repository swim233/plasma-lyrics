#include "core/lyric/lrcparser.h"
#include "core/lyric/timeline.h"

#include <QTest>

using namespace PlasmaLyrics;

class LrcParserTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesDirtyNetEasePayload()
    {
        const QString lrc = QStringLiteral(
            "{\"t\":0,\"c\":[{\"tx\":\"作词: \"},{\"tx\":\"初繁言\"}]}\n"
            "{\"t\":933,\"c\":[{\"tx\":\"作曲: \"},{\"tx\":\"闹闹丶\"}]}\n"
            "[offset:+120]\n[00:02.80][00:03.000]编曲：闹闹丶\n[00:29.638]若能再相见");
        const auto parsed = LrcParser::parse(lrc);
        QCOMPARE(parsed.embeddedOffsetMs, 120);
        QCOMPARE(parsed.lines.size(), 5);
        QCOMPARE(parsed.lines[0].text, QStringLiteral("作词: 初繁言"));
        QCOMPARE(parsed.lines[2].startMs, 2920);
        QCOMPARE(parsed.lines[3].startMs, 3120);
        QCOMPARE(parsed.lines.last().startMs, 29758);
    }

    void mergesTranslationByTimestamp()
    {
        const auto document = LrcParser::merge(QStringLiteral("[00:01.000]你好"),
                                               QStringLiteral("[00:01.000]Hello"));
        QVERIFY(document.lines.first().translation.has_value());
        QCOMPARE(*document.lines.first().translation, QStringLiteral("Hello"));
        QVERIFY(!document.lines.first().words.has_value());
    }

    void onlyJsonCreditsProducesAnEmptyDisplayDocument()
    {
        const QString lrc = QStringLiteral(
            "{\"t\":-1,\"c\":[{\"tx\":\"Lyrics by Foo\",\"li\":\"https://example.test/foo\"}]}\n"
            "{\"t\":-1,\"c\":[{\"tx\":\"Music by Bar\",\"li\":\"https://example.test/bar\"}]}");
        auto document = LrcParser::merge(lrc, QString());
        QCOMPARE(document.lines.size(), 2);
        QVERIFY(document.lines[0].credit);
        QVERIFY(document.lines[1].credit);
        document.lines = filterLeadingCredits(document.lines);
        QVERIFY(document.lines.isEmpty());
    }

    void exposesIdTagsForLocalMatching()
    {
        const auto parsed = LrcParser::parse(QStringLiteral(
            "[ti:First title]\n[ti:Final title]\n"
            "[ar:First artist]\n[ar:Second artist]\n[ar:First artist]\n"
            "[al:Album]\n[length:03:30.5]\n[00:01.00]line"));
        QCOMPARE(parsed.title, QStringLiteral("Final title"));
        QCOMPARE(parsed.artists,
                 QStringList({QStringLiteral("First artist"), QStringLiteral("Second artist")}));
        QCOMPARE(parsed.album, QStringLiteral("Album"));
        QCOMPARE(parsed.lengthMs, 210500);
        QCOMPARE(parsed.lines.size(), 1);
    }

    void ignoresMissingAndInvalidIdTags()
    {
        const auto parsed = LrcParser::parse(QStringLiteral(
            "[ti:]\n[ar:]\n[al:]\n[length:not-a-duration]\n[00:01.00]line"));
        QVERIFY(parsed.title.isEmpty());
        QVERIFY(parsed.artists.isEmpty());
        QVERIFY(parsed.album.isEmpty());
        QCOMPARE(parsed.lengthMs, 0);
    }

    void parseBilingualPairsAdjacentSameTimestampLines()
    {
        const QString lrc = QStringLiteral(
            "[00:01.000]你好\n[00:01.000]Hello\n[00:02.000]再见");
        int dropped = -1;
        const auto document = LrcParser::parseBilingual(lrc, &dropped);
        QCOMPARE(document.lines.size(), 2);
        QCOMPARE(document.lines[0].text, QStringLiteral("你好"));
        QVERIFY(document.lines[0].translation.has_value());
        QCOMPARE(*document.lines[0].translation, QStringLiteral("Hello"));
        QCOMPARE(document.lines[1].text, QStringLiteral("再见"));
        QVERIFY(!document.lines[1].translation.has_value());
        QCOMPARE(dropped, 1);
    }

    void parseBilingualKeepsOnlyOneLineWhenTextsAreIdentical()
    {
        const QString lrc = QStringLiteral("[00:01.000]相同文本\n[00:01.000]相同文本");
        int dropped = -1;
        const auto document = LrcParser::parseBilingual(lrc, &dropped);
        QCOMPARE(document.lines.size(), 1);
        QCOMPARE(document.lines[0].text, QStringLiteral("相同文本"));
        QVERIFY(!document.lines[0].translation.has_value());
        QCOMPARE(dropped, 1);
    }

    void parseBilingualDropsFromTheThirdLineInARun()
    {
        const QString lrc = QStringLiteral(
            "[00:01.000]原文\n[00:01.000]译文\n[00:01.000]多余的第三行");
        int dropped = -1;
        const auto document = LrcParser::parseBilingual(lrc, &dropped);
        QCOMPARE(document.lines.size(), 1);
        QCOMPARE(document.lines[0].text, QStringLiteral("原文"));
        QVERIFY(document.lines[0].translation.has_value());
        QCOMPARE(*document.lines[0].translation, QStringLiteral("译文"));
        QCOMPARE(dropped, 2);
    }

    void parseBilingualLeavesCreditShapedRunsUntouched()
    {
        const QString lrc = QStringLiteral(
            "[00:00.000]作词：想边\n[00:00.000]作曲：闹闹丶\n"
            "[00:01.000]作词 : 爆音常安\n[00:01.000]作曲 : 爆音常安\n"
            "[00:02.000]正文歌词");
        int dropped = -1;
        const auto document = LrcParser::parseBilingual(lrc, &dropped);
        QCOMPARE(document.lines.size(), 5);
        QCOMPARE(dropped, 0);
        for (int index = 0; index < 4; ++index) {
            QVERIFY(!document.lines[index].translation.has_value());
        }
        QCOMPARE(document.lines[4].text, QStringLiteral("正文歌词"));
    }

    void parseBilingualLeavesRunUntouchedWhenOnlyTheSecondLineLooksLikeACredit()
    {
        // The guard checks either line of the pair, not just the first: an
        // ordinary lyric followed by a shape-matched (non-JSON) credit line
        // must not be paired as original/translation.
        const QString lrc = QStringLiteral(
            "[00:01.000]你好\n[00:01.000]作曲：某人\n[00:02.000]再见");
        int dropped = -1;
        const auto document = LrcParser::parseBilingual(lrc, &dropped);
        QCOMPARE(document.lines.size(), 3);
        QCOMPARE(dropped, 0);
        QCOMPARE(document.lines[0].text, QStringLiteral("你好"));
        QVERIFY(!document.lines[0].translation.has_value());
        QCOMPARE(document.lines[1].text, QStringLiteral("作曲：某人"));
        QVERIFY(!document.lines[1].translation.has_value());
        QCOMPARE(document.lines[2].text, QStringLiteral("再见"));
    }

    void parseBilingualPairsEachMultiTimestampOccurrence()
    {
        const QString lrc = QStringLiteral(
            "[00:12.00][01:30.00]原文\n[00:12.00][01:30.00]译文");
        int dropped = -1;
        const auto document = LrcParser::parseBilingual(lrc, &dropped);
        QCOMPARE(document.lines.size(), 2);
        QCOMPARE(document.lines[0].startMs, qint64(12000));
        QCOMPARE(document.lines[0].text, QStringLiteral("原文"));
        QCOMPARE(*document.lines[0].translation, QStringLiteral("译文"));
        QCOMPARE(document.lines[1].startMs, qint64(90000));
        QCOMPARE(document.lines[1].text, QStringLiteral("原文"));
        QCOMPARE(*document.lines[1].translation, QStringLiteral("译文"));
        QCOMPARE(dropped, 2);
    }

    void parseBilingualPairsMultiTimestampLinesEvenWhenTranslationOrderIsReversed()
    {
        // parse() appends one entry per timestamp in the order the source
        // line writes them, then stable_sort only reorders by startMs -- so
        // within a run, the original line's entry for a given startMs always
        // precedes the translation line's entry for that same startMs,
        // regardless of what order either line lists its own timestamps in.
        // Pin this: swapping the timestamp order on the translation line
        // alone must not break which line becomes the original and which
        // becomes the translation.
        const QString lrc = QStringLiteral(
            "[00:12.00][01:30.00]原文\n[01:30.00][00:12.00]译文");
        int dropped = -1;
        const auto document = LrcParser::parseBilingual(lrc, &dropped);
        QCOMPARE(document.lines.size(), 2);
        QCOMPARE(document.lines[0].startMs, qint64(12000));
        QCOMPARE(document.lines[0].text, QStringLiteral("原文"));
        QCOMPARE(*document.lines[0].translation, QStringLiteral("译文"));
        QCOMPARE(document.lines[1].startMs, qint64(90000));
        QCOMPARE(document.lines[1].text, QStringLiteral("原文"));
        QCOMPARE(*document.lines[1].translation, QStringLiteral("译文"));
        QCOMPARE(dropped, 2);
    }

    void parseBilingualCollapsesArtificialRunFromNegativeOffset()
    {
        // A negative [offset] large enough to clamp several distinct
        // timestamps to 0ms is pathological input (DESIGN.md decision 62):
        // parseBilingual does not special-case it, it just treats the
        // resulting run like any other. Pin the resulting shape so a future
        // change to the run-splitting logic doesn't silently alter it.
        const QString lrc = QStringLiteral(
            "[offset:-60000]\n[00:01.00]LineOne\n[00:02.00]LineTwo\n"
            "[00:03.00]LineThree\n[02:00.00]LineFour");
        int dropped = -1;
        const auto document = LrcParser::parseBilingual(lrc, &dropped);
        QCOMPARE(document.lines.size(), 2);
        QCOMPARE(document.lines[0].startMs, qint64(0));
        QCOMPARE(document.lines[0].text, QStringLiteral("LineOne"));
        QVERIFY(document.lines[0].translation.has_value());
        QCOMPARE(*document.lines[0].translation, QStringLiteral("LineTwo"));
        QCOMPARE(document.lines[1].startMs, qint64(60000));
        QCOMPARE(document.lines[1].text, QStringLiteral("LineFour"));
        QVERIFY(!document.lines[1].translation.has_value());
        QCOMPARE(dropped, 2);
    }

    void parseBilingualLeavesRunWithProviderFlaggedCreditUntouched()
    {
        const QString lrc = QStringLiteral(
            "{\"t\":933,\"c\":[{\"tx\":\"制作人\"}]}\n[00:00.933]另一行文本");
        int dropped = -1;
        const auto document = LrcParser::parseBilingual(lrc, &dropped);
        QCOMPARE(document.lines.size(), 2);
        QCOMPARE(dropped, 0);
        QVERIFY(document.lines[0].credit);
        QVERIFY(!document.lines[0].translation.has_value());
        QVERIFY(!document.lines[1].translation.has_value());
    }

    void parseBilingualDoesNotChangeParseOrMerge()
    {
        // parse()/merge() must be byte-for-byte unaffected by the new entry
        // point; re-run a couple of the existing regression cases directly.
        const QString lrc = QStringLiteral(
            "{\"t\":0,\"c\":[{\"tx\":\"作词: \"},{\"tx\":\"初繁言\"}]}\n"
            "{\"t\":933,\"c\":[{\"tx\":\"作曲: \"},{\"tx\":\"闹闹丶\"}]}\n"
            "[offset:+120]\n[00:02.80][00:03.000]编曲：闹闹丶\n[00:29.638]若能再相见");
        const auto parsed = LrcParser::parse(lrc);
        QCOMPARE(parsed.embeddedOffsetMs, 120);
        QCOMPARE(parsed.lines.size(), 5);
        QCOMPARE(parsed.lines[0].text, QStringLiteral("作词: 初繁言"));

        const auto merged = LrcParser::merge(QStringLiteral("[00:01.000]你好"),
                                             QStringLiteral("[00:01.000]Hello"));
        QCOMPARE(*merged.lines.first().translation, QStringLiteral("Hello"));
    }
};

QTEST_GUILESS_MAIN(LrcParserTest)
#include "tst_lrcparser.moc"
