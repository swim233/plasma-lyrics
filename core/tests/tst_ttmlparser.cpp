#include "core/lyric/ttmlparser.h"

#include <QFile>
#include <QJsonArray>
#include <QTest>

using namespace PlasmaLyrics;

namespace {
const QByteArray richInput = QStringLiteral(
    "<tt xmlns=\"http://www.w3.org/ns/ttml\" xmlns:ttm=\"http://www.w3.org/ns/ttml#metadata\" "
    "xmlns:itunes=\"http://music.apple.com/lyric-ttml-internal\" xmlns:amll=\"http://www.example.com/ns/amll\">"
    "<head><metadata xmlns=\"\"><amll:meta key=\"ttmlAuthorGithubLogin\" value=\"author\"/>"
    "<itunes:translations><p itunes:key=\"L2\" ttm:role=\"x-translation\" xml:lang=\"en\">header English</p>"
    "<p itunes:key=\"L2\" ttm:role=\"x-translation\" xml:lang=\"zh-CN\">头部翻译</p>"
    "</itunes:translations></metadata></head><body><div>"
    "<p begin=\"0.5s\" end=\"00:02.000\" itunes:key=\"L1\">"
    "<span begin=\"00:00.500\" end=\"00:01.000\">Hello</span>"
    "<span begin=\"00:01.000\" end=\"00:02.000\"> world</span>"
    "<span ttm:role=\"x-translation\" xml:lang=\"en\">hello world</span>"
    "<span ttm:role=\"x-translation\" xml:lang=\"zh-TW\">哈囉世界</span>"
    "<span ttm:role=\"x-translation\" xml:lang=\"zh-CN\">你好世界</span>"
    "<span ttm:role=\"x-translation\" xml:lang=\"zh-CN\">你好世界</span></p>"
    "<p begin=\"00:01.900\" end=\"00:03.000\" itunes:key=\"L2\">Second</p>"
    "</div></body></tt>").toUtf8();
const QByteArray rolesInput = QStringLiteral(
    "<tt xmlns=\"http://www.w3.org/ns/ttml\" xmlns:ttm=\"http://www.w3.org/ns/ttml#metadata\"><body><div>"
    "<p begin=\"00:00.000\" end=\"00:02.000\"><span begin=\"00:00.000\" end=\"00:01.000\">Main</span>"
    "<span ttm:role=\"x-bg\" begin=\"00:01.000\" end=\"00:02.000\"><span begin=\"00:01.000\" end=\"00:02.000\">(background)</span></span>"
    "<span ttm:role=\"x-romanization\">roman</span></p></div></body></tt>").toUtf8();
} // namespace

class TtmlParserTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesTimes()
    {
        QCOMPARE(TtmlParser::parseTime(QStringLiteral("1.25s")), std::optional<qint64>(1250));
        QCOMPARE(TtmlParser::parseTime(QStringLiteral("01:02.500")), std::optional<qint64>(62500));
        QCOMPARE(TtmlParser::parseTime(QStringLiteral("01:02:03.004")), std::optional<qint64>(3723004));
        QCOMPARE(TtmlParser::parseTime(QStringLiteral("250ms")), std::optional<qint64>(250));
        QCOMPARE(TtmlParser::parseTime(QStringLiteral("45.404")), std::optional<qint64>(45404));
        QVERIFY(!TtmlParser::parseTime(QStringLiteral("00:75.0")));
    }

    void parsesOfficialAmllTranslationAndWhitespaceFixture()
    {
        QFile fixture(QStringLiteral(PLASMA_LYRICS_TTML_FIXTURES_DIR
                                     "/amll-official-translations.ttml"));
        QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.errorString()));
        QString error;

        const auto document = TtmlParser::parse(fixture.readAll(), &error);

        QVERIFY2(document.has_value(), qPrintable(error));
        QCOMPARE(document->lines.size(), 1);
        QCOMPARE(document->lines.first().startMs, 45404);
        QCOMPARE(document->lines.first().endMs, 47812);
        QCOMPARE(document->lines.first().text, QStringLiteral("word1 word2"));
        QCOMPARE(document->lines.first().translation,
                 std::optional<QString>(QStringLiteral("简体 翻译")));
        QVERIFY(document->lines.first().words.has_value());
        QCOMPARE(document->lines.first().words->size(), 2);
        QCOMPARE(document->lines.first().words->at(1).text,
                 QStringLiteral(" word2"));
    }

    void officialAppleTranslationExcludesBackgroundAndPronunciationTrees()
    {
        QFile fixture(QStringLiteral(PLASMA_LYRICS_TTML_FIXTURES_DIR
                                     "/apple-example-2-background-translation.ttml"));
        QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.errorString()));
        QString error;

        const auto document = TtmlParser::parse(fixture.readAll(), &error);

        QVERIFY2(document.has_value(), qPrintable(error));
        QCOMPARE(document->lines.size(), 1);
        QCOMPARE(document->lines.first().text, QStringLiteral("主旋律"));
        QCOMPARE(document->lines.first().translation,
                 std::optional<QString>(QStringLiteral("Main translation")));
    }

    void parsesNamespacedWordsTranslationsAndMetadata()
    {
        QString error;
        const auto document = TtmlParser::parse(richInput, &error);
        QVERIFY2(document.has_value(), qPrintable(error));
        QCOMPARE(document->lines.size(), 2);
        QCOMPARE(document->lines[0].text, QStringLiteral("Hello world"));
        QCOMPARE(document->lines[0].translation, std::optional<QString>(QStringLiteral("你好世界")));
        QVERIFY(document->lines[0].words.has_value());
        QCOMPARE(document->lines[0].words->size(), 2);
        QCOMPARE(document->lines[0].words->at(1).text, QStringLiteral(" world"));
        QCOMPARE(document->lines[1].translation, std::optional<QString>(QStringLiteral("头部翻译")));
        QCOMPARE(document->lines[0].endMs, 2000);
        QCOMPARE(document->lines[1].startMs, 1900); // overlapping lines remain valid
        QVERIFY(document->hasWords);
        QCOMPARE(document->metadata.value(QStringLiteral("ttmlAuthorGithubLogin")).toArray().first().toString(),
                 QStringLiteral("author"));
    }

    void excludesBackgroundAndRomanization()
    {
        const auto document = TtmlParser::parse(rolesInput);
        QVERIFY(document.has_value());
        QCOMPARE(document->lines.first().text, QStringLiteral("Main"));
        QCOMPARE(document->lines.first().words->size(), 1);
    }

    void rejectsMalformedEmptyAndInvalidTimeline_data()
    {
        QTest::addColumn<QByteArray>("input");
        QTest::newRow("malformed") << QByteArray("<tt><body><p></body>");
        QTest::newRow("empty") << QByteArray("<tt xmlns=\"http://www.w3.org/ns/ttml\"><body><div/></body></tt>");
        QTest::newRow("invalid timeline") << QByteArray("<tt xmlns=\"http://www.w3.org/ns/ttml\"><body><p begin=\"2s\" end=\"1s\">line</p></body></tt>");
    }

    void rejectsMalformedEmptyAndInvalidTimeline()
    {
        QFETCH(QByteArray, input);
        QString error;
        QVERIFY(!TtmlParser::parse(input, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TtmlParserTest)
#include "tst_ttmlparser.moc"
