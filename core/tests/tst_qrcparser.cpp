#include "core/lyric/qrcparser.h"
#include "core/lyric/timeline.h"

#include <QFile>
#include <QTest>

#include <algorithm>

using namespace PlasmaLyrics;

namespace {

QString fixture(const QString &name)
{
    QFile file(QStringLiteral(PLASMA_LYRICS_QRC_FIXTURES_DIR) + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

// A minimal well-formed document, so the structural cases do not have to be
// expressed against a 68-line recording.
QString wrap(const QString &body)
{
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<QrcInfos>\r\n"
                          "<LyricInfo LyricCount=\"1\">\r\n"
                          "<Lyric_1 LyricType=\"1\" LyricContent=\"")
        + body + QStringLiteral("\"/>\r\n</LyricInfo>\r\n</QrcInfos>");
}

} // namespace

class QrcParserTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fixturesAreReadable()
    {
        QVERIFY(!fixture(QStringLiteral("qq-qrc-hoshiloop.qrc")).isEmpty());
        QVERIFY(!fixture(QStringLiteral("qq-qrc-hoshiloop-roma.qrc")).isEmpty());
        QVERIFY(!fixture(QStringLiteral("qq-qrc-budaoren.qrc")).isEmpty());
        QVERIFY(!fixture(QStringLiteral("qq-qrc-birthday.qrc")).isEmpty());
        QVERIFY(!fixture(QStringLiteral("qq-qrc-birthday-roma.qrc")).isEmpty());
    }

    void parsesWordLevelTimesFromARealTrack()
    {
        const auto parsed = QrcParser::parse(fixture(QStringLiteral("qq-qrc-hoshiloop.qrc")));
        QCOMPARE(parsed.title, QStringLiteral("惑星ループ"));
        QCOMPARE(parsed.artists, QStringList({QStringLiteral("Eve")}));
        QCOMPARE(parsed.album, QStringLiteral("OFFICIAL NUMBER"));
        QCOMPARE(parsed.lines.size(), 68);

        const auto &first = parsed.lines.first();
        QCOMPARE(first.startMs, 0);
        QCOMPARE(first.endMs, 522);
        QCOMPARE(first.text, QStringLiteral("惑星ループ - Eve"));
        QVERIFY(first.words.has_value());
        QCOMPARE(first.words->size(), 7);
        // The bracket pair is [start,duration], not [start,end].
        QCOMPARE(first.words->at(0).text, QStringLiteral("惑"));
        QCOMPARE(first.words->at(0).startMs, 0);
        QCOMPARE(first.words->at(0).endMs, 116);
        QCOMPARE(first.words->at(2).text, QStringLiteral("ル"));
        QCOMPARE(first.words->at(2).startMs, 233);
        QCOMPARE(first.words->at(2).endMs, 291);
        // A word whose text is " - " carries literal spaces that must survive.
        QCOMPARE(first.words->at(5).text, QStringLiteral(" - "));
        QCOMPARE(first.words->last().text, QStringLiteral("Eve"));
        QCOMPARE(first.words->last().endMs, 523);
    }

    void keepsZeroDurationAndWhitespaceTokens()
    {
        const auto parsed = QrcParser::parse(fixture(QStringLiteral("qq-qrc-hoshiloop.qrc")));
        const auto &line = parsed.lines.at(3);
        QCOMPARE(line.startMs, 1802);
        QVERIFY(line.words.has_value());
        QCOMPARE(line.words->size(), 11);
        // Benign in the source: a separator token with no duration at all.
        const auto &empty = line.words->at(6);
        QCOMPARE(empty.text, QStringLiteral(" "));
        QCOMPARE(empty.startMs, 2538);
        QCOMPARE(empty.endMs, 2538);
        // It is still part of the line's text, so the rendered line keeps its
        // spacing.
        QVERIFY(line.text.contains(QStringLiteral("ル ル")));
    }

    void flagsTheTitleLineFromTheDocumentsOwnTags()
    {
        auto parsed = QrcParser::parse(fixture(QStringLiteral("qq-qrc-hoshiloop.qrc")));
        // "惑星ループ - Eve" carries no colon, so the shape heuristic alone
        // could never identify it; the [ti]/[ar] tags can.
        QVERIFY(parsed.lines.at(0).credit);
        QVERIFY(!looksLikeCredit(LyricLine{0, 1, parsed.lines.at(0).text}));
        QVERIFY(parsed.lines.at(1).credit);   // 词：ナユタン星人
        QVERIFY(parsed.lines.at(2).credit);   // 曲：ナユタン星人
        QVERIFY(!parsed.lines.at(3).credit);  // the first sung line
        const auto shown = filterLeadingCredits(parsed.lines);
        QCOMPARE(shown.size(), 65);
        QCOMPARE(shown.first().startMs, 1802);
    }

    void flagsCreditHeadsThatTheSharedShapeCheckCannotMatch()
    {
        auto parsed = QrcParser::parse(fixture(QStringLiteral("qq-qrc-budaoren.qrc")));
        QCOMPARE(parsed.lines.size(), 30);
        QCOMPARE(parsed.lines.at(1).text, QStringLiteral("Lyrics by：MSR Studio/z1on/ReStudio"));
        // "Lyrics by" has an embedded space and so fails looksLikeCredit's
        // twelve-character, no-whitespace head rule. Flagging it here is what
        // keeps it out of the lyrics without widening that shared rule.
        QVERIFY(!looksLikeCredit(LyricLine{0, 1, parsed.lines.at(1).text}));
        QVERIFY(parsed.lines.at(1).credit);
        QVERIFY(parsed.lines.at(2).credit);
        QVERIFY(parsed.lines.at(3).credit);
        QVERIFY(!parsed.lines.at(4).credit);
        const auto shown = filterLeadingCredits(parsed.lines);
        QCOMPARE(shown.size(), 26);
        QCOMPARE(shown.first().text, QStringLiteral("Ex tenuī filo lucis texor"));
    }

    void flagsTheTitleLineWhenTheTagCarriesASubtitleTheLineDoesNot()
    {
        // Real and common: [ti:] reads "万物有灵 (剑灵八职业同人曲)" while the
        // body line reads "万物有灵 - 洛天依". Comparing the two halves against
        // the tags verbatim leaves the title line in the lyrics.
        auto parsed = QrcParser::parse(fixture(QStringLiteral("qq-qrc-wanwuyouling.qrc")));
        QCOMPARE(parsed.title, QStringLiteral("万物有灵 (剑灵八职业同人曲)"));
        QCOMPARE(parsed.lines.first().text, QStringLiteral("万物有灵 - 洛天依"));
        QVERIFY(parsed.lines.first().credit);
        QVERIFY(!parsed.lines.at(1).credit);
        const auto shown = filterLeadingCredits(parsed.lines);
        QCOMPARE(shown.size(), parsed.lines.size() - 1);
        QCOMPARE(shown.first().text, QStringLiteral("屏幕上鼠标来来回回"));
    }

    void doesNotMistakeALyricForTheTitleLine()
    {
        // The artist half has to match exactly; a line that merely contains
        // " - " is not a header.
        const auto parsed = QrcParser::parse(wrap(QStringLiteral(
            "[ti:Song]\r\n[ar:Artist]\r\n"
            "[0,100]you - me(0,100)\r\n"
            "[200,100]sung(200,100)")));
        QCOMPARE(parsed.lines.size(), 2);
        QVERIFY(!parsed.lines.at(0).credit);
    }

    void stopsFlaggingCreditsAfterTheLeadingRun()
    {
        // A colon inside a later line is ordinary lyric punctuation, not a
        // credit, and the run has already ended by then.
        const auto parsed = QrcParser::parse(wrap(QStringLiteral(
            "[ti:Song]\r\n[ar:Artist]\r\n"
            "[0,100]Song(0,50) - (50,20)Artist(70,30)\r\n"
            "[200,100]sung(200,100)\r\n"
            "[400,100]he said：no(400,100)")));
        QCOMPARE(parsed.lines.size(), 3);
        QVERIFY(parsed.lines.at(0).credit);
        QVERIFY(!parsed.lines.at(1).credit);
        QVERIFY(!parsed.lines.at(2).credit);
    }

    void ignoresTheKanaIndexAndOtherTags()
    {
        // [kana:...] carries its own parenthesised timings and would parse as
        // a word list if it were not recognised as a tag.
        const auto parsed = QrcParser::parse(wrap(QStringLiteral(
            "[ti:Song]\r\n[by:]\r\n[offset:0]\r\n"
            "[kana:1わく1せい1す(17028,136)み(17164,160)\r\n"
            "[0,100]sung(0,100)")));
        QCOMPARE(parsed.lines.size(), 1);
        QCOMPARE(parsed.lines.first().text, QStringLiteral("sung"));
    }

    void rejectsContentThatIsNotAQrcDocument()
    {
        QVERIFY(QrcParser::parse(QStringLiteral("not xml at all")).lines.isEmpty());
        QVERIFY(QrcParser::parse(QString()).lines.isEmpty());
        // Present but unterminated attribute.
        QVERIFY(QrcParser::parse(QStringLiteral("<Lyric_1 LyricContent=\"[0,1]a(0,1)"))
                    .lines.isEmpty());
        // Well-formed envelope whose body holds nothing timed.
        QVERIFY(QrcParser::parse(wrap(QStringLiteral("[ti:Song]\r\nplain text"))).lines.isEmpty());
    }

    void unescapesXmlEntitiesInTheBody()
    {
        const auto parsed = QrcParser::parse(wrap(QStringLiteral(
            "[0,100]rock &amp; roll(0,60) &lt;x&gt;(60,40)")));
        QCOMPARE(parsed.lines.size(), 1);
        QCOMPARE(parsed.lines.first().text, QStringLiteral("rock & roll <x>"));
    }

    void keepsRawQuotesInsideTheBody()
    {
        // QQ does not escape '"' in the body. Both spellings in one body, with
        // lines after each: ending the value at the first raw quote would lose
        // everything from the first line on.
        const auto parsed = QrcParser::parse(wrap(QStringLiteral(
            "[ti:Song]\r\n"
            "[0,100]\"(0,10)raw(10,40)\"(50,50)\r\n"
            "[200,100]&quot;(200,10)escaped(210,40)&quot;(250,50)\r\n"
            "[400,100]after(400,100)")));
        QCOMPARE(parsed.lines.size(), 3);
        QCOMPARE(parsed.lines.at(0).text, QStringLiteral("\"raw\""));
        QCOMPARE(parsed.lines.at(0).words->at(2).text, QStringLiteral("\""));
        QCOMPARE(parsed.lines.at(0).words->at(2).startMs, 50);
        QCOMPARE(parsed.lines.at(1).text, QStringLiteral("\"escaped\""));
        QCOMPARE(parsed.lines.at(2).text, QStringLiteral("after"));
    }

    void keepsALyricWhoseTagsCarryARawQuote()
    {
        // The shape of musicid 446012964, "Slut!" (Taylor's Version): the
        // [ti:] tag comes before every timed line, so a quote in the title
        // used to end the body before the first lyric line.
        const auto parsed = QrcParser::parse(wrap(QStringLiteral(
            "[ti:\"Song\" (Live)]\r\n[ar:Artist]\r\n"
            "[0,100]\"(0,10)Song\" ((10,40)Live) - (50,20)Artist(70,30)\r\n"
            "[200,100]sung(200,100)")));
        QCOMPARE(parsed.title, QStringLiteral("\"Song\" (Live)"));
        QCOMPARE(parsed.lines.size(), 2);
        QCOMPARE(parsed.lines.at(0).text, QStringLiteral("\"Song\" (Live) - Artist"));
        QVERIFY(parsed.lines.at(0).credit);
        QCOMPARE(parsed.lines.at(1).text, QStringLiteral("sung"));
        QVERIFY(!parsed.lines.at(1).credit);

        // Any other tag ahead of the first line does the same, the album
        // included.
        const auto album = QrcParser::parse(wrap(QStringLiteral(
            "[ti:Heroes]\r\n[ar:Artist]\r\n[al:\"Heroes\" (2017 Remaster)]\r\n"
            "[0,100]sung(0,100)\r\n[200,100]more(200,100)")));
        QCOMPARE(album.album, QStringLiteral("\"Heroes\" (2017 Remaster)"));
        QCOMPARE(album.lines.size(), 2);
    }

    void endsTheBodyOnlyAtAQuoteThatClosesTheElement()
    {
        const QString head = QStringLiteral("<Lyric_1 LyricType=\"1\" LyricContent=\"");
        // Whitespace between the quote and "/>" is still the end.
        QCOMPARE(QrcParser::parse(head + QStringLiteral("[0,100]a(0,100)\r\n\" />"))
                     .lines.size(),
                 1);
        // A quote followed by '>' alone is not: no recorded payload closes the
        // element that way, so the attribute stays unterminated.
        QVERIFY(QrcParser::parse(head + QStringLiteral("[0,100]a(0,100)\r\n\">")).lines.isEmpty());
    }

    void readsARealBodyThatCarriesRawQuotes()
    {
        // musicid 394368429. Line 54327 opens with a raw quote; stopping at it
        // kept 22 of the 66 lines, the last of them ending at 54160.
        const auto parsed = QrcParser::parse(fixture(QStringLiteral("qq-qrc-birthday.qrc")));
        QCOMPARE(parsed.title, QStringLiteral("バースデー"));
        QCOMPARE(parsed.lines.size(), 66);
        const auto quoted = std::find_if(parsed.lines.cbegin(), parsed.lines.cend(),
                                         [](const auto &line) { return line.startMs == 54327; });
        QVERIFY(quoted != parsed.lines.cend());
        QCOMPARE(quoted->text, QStringLiteral("\"今日\"は眠って夢へ逃げよう"));
        QCOMPARE(quoted->words->first().text, QStringLiteral("\""));
        QCOMPARE(parsed.lines.last().startMs, 195486);
        QCOMPARE(parsed.lines.last().text, QStringLiteral("言っていないと生きていけないよ"));

        // The romanization payload carries the same four quotes. It also has
        // 66 timed lines, but the first three hold only whitespace and are
        // skipped as any empty line is.
        const auto romanized = QrcParser::parse(fixture(QStringLiteral("qq-qrc-birthday-roma.qrc")));
        QCOMPARE(romanized.lines.size(), 63);
        QCOMPARE(romanized.lines.last().startMs, 195486);
    }

    void readsWordLevelRomanizationByTime()
    {
        const auto document = QrcParser::assemble(
            fixture(QStringLiteral("qq-qrc-hoshiloop.qrc")),
            fixture(QStringLiteral("qq-qrc-hoshiloop-roma.qrc")),
            fixture(QStringLiteral("qq-qrc-hoshiloop-trans.lrc")));
        QVERIFY(document.hasWords);
        QCOMPARE(document.lines.size(), 68);
        const auto &first = document.lines.first();
        QVERIFY(first.words.has_value());
        // Romanization is per mora and the lyric is per character, so the two
        // token streams differ in length and cannot be paired by index: 惑 is
        // two morae, 星 is two more.
        QCOMPARE(*first.words->at(0).romanization, QStringLiteral("waku"));
        QCOMPARE(*first.words->at(1).romanization, QStringLiteral("sei"));
        QCOMPARE(*first.words->at(2).romanization, QStringLiteral("ru"));
        QCOMPARE(*first.words->at(3).romanization, QStringLiteral("u"));
        QCOMPARE(*first.words->at(4).romanization, QStringLiteral("pu"));
        QCOMPARE(*first.words->last().romanization, QStringLiteral("Eve"));

        const auto &sung = document.lines.at(5);
        QCOMPARE(sung.text, QStringLiteral("銀河の隅で 惑星はグルグル周る"));
        QCOMPARE(*sung.words->at(0).romanization, QStringLiteral("gin"));
        QCOMPARE(*sung.words->at(3).romanization, QStringLiteral("sumi"));
    }

    void lineRomanizationIsExactlyItsWordsJoined()
    {
        const auto document = QrcParser::assemble(
            fixture(QStringLiteral("qq-qrc-hoshiloop.qrc")),
            fixture(QStringLiteral("qq-qrc-hoshiloop-roma.qrc")), QString());
        int checked = 0;
        for (const auto &line : document.lines) {
            if (!line.romanization) {
                continue;
            }
            QStringList parts;
            for (const auto &word : *line.words) {
                if (word.romanization) {
                    parts.append(*word.romanization);
                }
            }
            QCOMPARE(*line.romanization, parts.join(QLatin1Char(' ')));
            ++checked;
        }
        QCOMPARE(checked, 68);
        QCOMPARE(*document.lines.first().romanization, QStringLiteral("waku sei ru u pu - Eve"));
    }

    void leavesRomanizationUnsetWithoutACompanionPayload()
    {
        const auto document = QrcParser::assemble(
            fixture(QStringLiteral("qq-qrc-budaoren.qrc")), QString(),
            fixture(QStringLiteral("qq-qrc-budaoren-trans.lrc")));
        QVERIFY(document.hasWords);
        for (const auto &line : document.lines) {
            QVERIFY(!line.romanization.has_value());
            for (const auto &word : *line.words) {
                QVERIFY(!word.romanization.has_value());
            }
        }
    }

    void alignsTranslationDespiteCentisecondRounding()
    {
        const auto document = QrcParser::assemble(
            fixture(QStringLiteral("qq-qrc-hoshiloop.qrc")), QString(),
            fixture(QStringLiteral("qq-qrc-hoshiloop-trans.lrc")));
        // The LRC timestamp is [00:16.20] = 16200 ms; the QRC line starts at
        // 16204. Requiring exact equality would align almost nothing.
        const auto &sung = document.lines.at(5);
        QCOMPARE(sung.startMs, 16204);
        QVERIFY(sung.translation.has_value());
        QCOMPARE(*sung.translation, QStringLiteral("行星在银河一隅 一圈圈地旋转"));
        int translated = 0;
        for (const auto &line : document.lines) {
            if (line.translation) {
                ++translated;
            }
        }
        // 68 translation lines are supplied; 25 of them are the "//"
        // placeholder and must not become visible text.
        QCOMPARE(translated, 43);
    }

    void dropsThePlaceholderTranslation()
    {
        const auto document = QrcParser::assemble(
            fixture(QStringLiteral("qq-qrc-hoshiloop.qrc")), QString(),
            fixture(QStringLiteral("qq-qrc-hoshiloop-trans.lrc")));
        QVERIFY(!document.lines.first().translation.has_value());
    }

    void toleratesACompanionWithADifferentLineCount()
    {
        // A skew must cost one line, not every line after it.
        const auto content = wrap(QStringLiteral(
            "[0,100]a(0,100)\r\n[200,100]b(200,100)\r\n[400,100]c(400,100)"));
        const auto roma = wrap(QStringLiteral(
            "[0,100]aa(0,100)\r\n[400,100]cc(400,100)"));
        const auto document = QrcParser::assemble(content, roma, QString());
        QCOMPARE(document.lines.size(), 3);
        QCOMPARE(*document.lines.at(0).romanization, QStringLiteral("aa"));
        QVERIFY(!document.lines.at(1).romanization.has_value());
        QCOMPARE(*document.lines.at(2).romanization, QStringLiteral("cc"));
    }

    void assembleReportsNothingForAnEmptyDocument()
    {
        const auto document = QrcParser::assemble(QString(), QString(), QString());
        QVERIFY(document.lines.isEmpty());
        QVERIFY(!document.hasWords);
    }

    void appliesTheEmbeddedOffsetToLineAndWordTimes()
    {
        const auto parsed = QrcParser::parse(wrap(QStringLiteral(
            "[offset:120]\r\n[1000,200]a(1000,100)b(1100,100)")));
        QCOMPARE(parsed.embeddedOffsetMs, 120);
        QCOMPARE(parsed.lines.first().startMs, 1120);
        QCOMPARE(parsed.lines.first().endMs, 1320);
        // Word times move with the line; leaving them behind would desync the
        // per-word highlight from the line it belongs to.
        QCOMPARE(parsed.lines.first().words->at(0).startMs, 1120);
        QCOMPARE(parsed.lines.first().words->at(1).endMs, 1320);
    }

    void clampsANegativeEmbeddedOffsetAtZero()
    {
        const auto parsed = QrcParser::parse(wrap(QStringLiteral(
            "[offset:-250]\r\n[100,200]a(100,200)")));
        QCOMPARE(parsed.lines.first().startMs, 0);
        QCOMPARE(parsed.lines.first().words->first().startMs, 0);
    }

    void keepsTheEmbeddedOffsetOutOfTheUsersOffset()
    {
        const auto document = QrcParser::assemble(wrap(QStringLiteral(
            "[offset:-250]\r\n[1000,100]a(1000,100)")), QString(), QString());
        // offsetMs belongs to the user's per-track adjustment, as it does for
        // LrcParser::merge; the tag has already moved the times instead.
        QCOMPARE(document.offsetMs, 0);
        QCOMPARE(document.lines.first().startMs, 750);
    }
};

QTEST_GUILESS_MAIN(QrcParserTest)
#include "tst_qrcparser.moc"
