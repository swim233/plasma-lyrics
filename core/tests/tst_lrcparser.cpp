// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * LRC parsing, at the shape the netease plaintext API actually returns it.
 * The fixtures under providers/tests/fixtures carry the three traps this
 * parser must survive:
 *   - the first lines are JSON objects `{"t":..,"c":[{"tx":"作词: "},..]}`
 *     (credits), which a standard LRC parser chokes on;
 *   - timestamps with two or three millisecond digits (`[00:29.63]`,
 *     `[00:29.638]`);
 *   - several timestamps opening one line;
 *   - a global `[offset:]` tag shifting every timestamp.
 *
 * Run with: ctest --test-dir build --output-on-failure -R lrctparser
 */

#include "lyric/LrcParser.h"

#include <QTest>

using namespace Lyrics;

namespace
{

LrcParseResult parse(const char *lrc)
{
    return parseLrc(QString::fromUtf8(lrc));
}

} // namespace

class LrcParserTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void jsonCreditLinesBecomeLyricLines();
    void threeMillisecondDigits();
    void twoMillisecondDigits();
    void multipleTimestampsOnOneLine();
    void offsetTagShiftsEveryLine();
    void emptyLinesAreDropped();
    void dedupesRepeatedTimestamps();
    void endMsFollowsNextStart();
    void lastLineEndStaysOpen();
};

void LrcParserTest::jsonCreditLinesBecomeLyricLines()
{
    // Recorded verbatim from `老街北` (1299289240): the API leads with two JSON
    // credit lines before the first bracketed timestamp.
    const auto r = parse(
        "{\"t\":0,\"c\":[{\"tx\":\"作词: \"},{\"tx\":\"初繁言\"}]}\n"
        "{\"t\":933,\"c\":[{\"tx\":\"作曲: \"},{\"tx\":\"闹闹丶\"}]}\n"
        "[00:28.63]若能再相见\n");
    QVERIFY(r.ok);
    QCOMPARE(r.lines.size(), 3);
    QCOMPARE(r.lines[0].startMs, 0);
    QCOMPARE(r.lines[0].text, QStringLiteral("作词: 初繁言"));
    QCOMPARE(r.lines[1].startMs, 933);
    QCOMPARE(r.lines[1].text, QStringLiteral("作曲: 闹闹丶"));
    QCOMPARE(r.lines[2].startMs, 28630);
    QCOMPARE(r.lines[2].text, QStringLiteral("若能再相见"));
}

void LrcParserTest::threeMillisecondDigits()
{
    const auto r = parse("[00:29.638]晴天\n");
    QVERIFY(r.ok);
    QCOMPARE(r.lines.size(), 1);
    QCOMPARE(r.lines[0].startMs, 29638);
}

void LrcParserTest::twoMillisecondDigits()
{
    // Two digits are centiseconds; the netease plaintext API mostly emits this.
    const auto r = parse("[00:28.63]若能再相见\n");
    QVERIFY(r.ok);
    QCOMPARE(r.lines[0].startMs, 28630);
}

void LrcParserTest::multipleTimestampsOnOneLine()
{
    // One text shown at several points (chorus repeats) is a common LRC
    // idiom. Chronological order governs: the chorus line's second stamp
    // sorts after the interlude line that follows it in the file.
    const auto r = parse("[00:10.00][01:20.00]副歌\n[00:30.00]间奏后\n");
    QVERIFY(r.ok);
    QCOMPARE(r.lines.size(), 3);
    QCOMPARE(r.lines[0].startMs, 10000);
    QCOMPARE(r.lines[0].text, QStringLiteral("副歌"));
    QCOMPARE(r.lines[1].startMs, 30000);
    QCOMPARE(r.lines[1].text, QStringLiteral("间奏后"));
    QCOMPARE(r.lines[2].startMs, 80000);
    QCOMPARE(r.lines[2].text, QStringLiteral("副歌"));
}

void LrcParserTest::offsetTagShiftsEveryLine()
{
    // Positive offset shifts lyrics later (they appear early), negative
    // earlier; the tag may appear anywhere.
    const auto r = parse("[offset:500]\n[00:10.00]一\n[00:20.00]二\n");
    QVERIFY(r.ok);
    QCOMPARE(r.lines[0].startMs, 10500);
    QCOMPARE(r.lines[1].startMs, 20500);

    const auto r2 = parse("[offset:-250]\n[00:10.00]一\n");
    QVERIFY(r2.ok);
    QCOMPARE(r2.lines[0].startMs, 9750);
}

void LrcParserTest::emptyLinesAreDropped()
{
    const auto r = parse("[00:10.00]\n[00:20.00]  \n[00:30.00]词\n");
    QVERIFY(r.ok);
    QCOMPARE(r.lines.size(), 1);
    QCOMPARE(r.lines[0].startMs, 30000);
}

void LrcParserTest::dedupesRepeatedTimestamps()
{
    // Providers sometimes repeat a timestamp; showing the line twice at the
    // same instant would double-flip the animation.
    const auto r = parse("[00:10.00]一\n[00:10.00]一\n[00:20.00]二\n");
    QVERIFY(r.ok);
    QCOMPARE(r.lines.size(), 2);
}

void LrcParserTest::endMsFollowsNextStart()
{
    const auto r = parse("[00:10.00]一\n[00:20.00]二\n");
    QVERIFY(r.ok);
    QCOMPARE(r.lines[0].endMs, 20000);
    QCOMPARE(r.lines[1].endMs, 20000);
}

void LrcParserTest::lastLineEndStaysOpen()
{
    // "Open" means: hold the last line until the track ends. The frontend
    // clamps by track length; the parser has no length to reason about.
    const auto r = parse("[00:10.00]一\n[00:20.00]二\n");
    QVERIFY(r.ok);
    QCOMPARE(r.lines[1].endMs, 20000);
    QVERIFY(r.lines[1].endMs == r.lines[1].startMs);
}

QTEST_MAIN(LrcParserTest)
#include "tst_lrcparser.moc"
