#include "core/lyric/lrcparser.h"

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
};

QTEST_GUILESS_MAIN(LrcParserTest)
#include "tst_lrcparser.moc"
