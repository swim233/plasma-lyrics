#include "core/lyric/wordsynthesis.h"

#include <QTest>

using namespace PlasmaLyrics;

namespace {

LyricLine makeLine(const QString &text, qint64 startMs, qint64 endMs)
{
    LyricLine line;
    line.startMs = startMs;
    line.endMs = endMs;
    line.text = text;
    return line;
}

} // namespace

class WordSynthesisTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Pinned per the task brief: QChar::isSpace() must already cover the
    // full-width space (4.49% of the corpus's lines) and NBSP (0.44%) for
    // the "whitespace never becomes its own token" rule below to actually
    // catch them, rather than letting either slip through as a one-char CJK
    // or Latin token of its own.
    void qCharIsSpaceCoversFullwidthAndNbsp()
    {
        QVERIFY(QChar(0x3000).isSpace()); // IDEOGRAPHIC SPACE
        QVERIFY(QChar(0x00A0).isSpace()); // NO-BREAK SPACE
        QVERIFY(QChar(u' ').isSpace());
    }

    void pureCjkSplitsOneCharacterPerToken()
    {
        const auto words = synthesizeWords(makeLine(QStringLiteral("你好吗"), 0, 300));
        QCOMPARE(words.size(), 3);
        QCOMPARE(words.at(0).text, QStringLiteral("你"));
        QCOMPARE(words.at(1).text, QStringLiteral("好"));
        QCOMPARE(words.at(2).text, QStringLiteral("吗"));
        QCOMPARE(words.at(0).startMs, 0);
        QCOMPARE(words.at(2).endMs, 300);
    }

    void pureLatinSplitsOnSpaces()
    {
        const auto words = synthesizeWords(makeLine(QStringLiteral("let it go"), 0, 900));
        QCOMPARE(words.size(), 3);
        // The space after "let"/"it" attaches to that word's own tail rather
        // than starting a token of its own -- wordRow has no spacing, so
        // dropping it would render "letitgo".
        QCOMPARE(words.at(0).text, QStringLiteral("let "));
        QCOMPARE(words.at(1).text, QStringLiteral("it "));
        QCOMPARE(words.at(2).text, QStringLiteral("go"));
    }

    void cjkAndLatinMixInOneLine()
    {
        const auto words = synthesizeWords(makeLine(QStringLiteral("我 love 你"), 0, 700));
        // "我 " (2 chars) + "love " (5) + "你" (1) = 8 total weight, 3 tokens.
        QCOMPARE(words.size(), 3);
        QCOMPARE(words.at(0).text, QStringLiteral("我 "));
        QCOMPARE(words.at(1).text, QStringLiteral("love "));
        QCOMPARE(words.at(2).text, QStringLiteral("你"));
    }

    void punctuationAttachesToTheWordItFollows()
    {
        const auto words = synthesizeWords(makeLine(QStringLiteral("Grow up! Nah mean?"), 0, 1800));
        QStringList texts;
        for (const auto &word : words) {
            texts << word.text;
        }
        // "up!" and "mean?" keep their trailing punctuation glued to the same
        // token instead of splitting the exclamation/question mark off on
        // its own -- there is no whitespace between the letter and the mark
        // to trigger a boundary.
        QVERIFY(texts.contains(QStringLiteral("up! ")));
        QVERIFY(texts.contains(QStringLiteral("mean?")));
    }

    void twoConsecutiveHalfwidthSpacesStayGluedToOneToken()
    {
        const auto words = synthesizeWords(makeLine(QStringLiteral("one  two"), 0, 800));
        QCOMPARE(words.size(), 2);
        QCOMPARE(words.at(0).text, QStringLiteral("one  "));
        QCOMPARE(words.at(1).text, QStringLiteral("two"));
    }

    void threeConsecutiveHalfwidthSpacesStayGluedToOneToken()
    {
        const auto words = synthesizeWords(makeLine(QStringLiteral("one   two"), 0, 900));
        QCOMPARE(words.size(), 2);
        QCOMPARE(words.at(0).text, QStringLiteral("one   "));
        QCOMPARE(words.at(1).text, QStringLiteral("two"));
    }

    void fiveConsecutiveHalfwidthSpacesStayGluedToOneToken()
    {
        const auto words = synthesizeWords(makeLine(QStringLiteral("one     two"), 0, 1100));
        QCOMPARE(words.size(), 2);
        QCOMPARE(words.at(0).text, QStringLiteral("one     "));
        QCOMPARE(words.at(1).text, QStringLiteral("two"));
    }

    void fullwidthIdeographicSpaceNeverBecomesItsOwnToken()
    {
        // Every character here is CJK and so is its own token; the point is
        // that U+3000 never becomes a *fifth*, standalone token -- it is
        // folded onto "好"'s tail instead.
        const QString text = QStringLiteral("你好　世界");
        const auto words = synthesizeWords(makeLine(text, 0, 400));
        QCOMPARE(words.size(), 4);
        QCOMPARE(words.at(0).text, QStringLiteral("你"));
        QCOMPARE(words.at(1).text, QStringLiteral("好　"));
        QCOMPARE(words.at(2).text, QStringLiteral("世"));
        QCOMPARE(words.at(3).text, QStringLiteral("界"));
    }

    void nbspNeverBecomesItsOwnToken()
    {
        // NBSP still closes the run it follows (same as any other
        // whitespace, per the "累积...遇到空白...收口" rule) -- it just
        // never becomes a token by itself: it rides on "a"'s tail instead of
        // splitting into its own entry between "a" and "b".
        const QString text = QStringLiteral("a b");
        const auto words = synthesizeWords(makeLine(text, 0, 200));
        QCOMPARE(words.size(), 2);
        QCOMPARE(words.at(0).text, QStringLiteral("a "));
        QCOMPARE(words.at(1).text, QStringLiteral("b"));
    }

    // Defensive case from the spec: 0 lines of the verified corpus start
    // with whitespace, but the rule ("fold into the head of the next token")
    // has to hold if one ever does.
    void leadingWhitespaceFoldsIntoTheFirstToken()
    {
        const auto words = synthesizeWords(makeLine(QStringLiteral("  hi"), 0, 400));
        QCOMPARE(words.size(), 1);
        QCOMPARE(words.at(0).text, QStringLiteral("  hi"));
    }

    void prefixSumHasNoAccumulatedRoundingErrorAndTokensMeetHeadToTail()
    {
        // 7 CJK characters, each its own one-weight token, over a duration
        // that does not divide evenly by 7 (100 / 7 is not an integer) --
        // the classic case where naive per-token duration division drifts
        // the end away from line.endMs.
        const auto words = synthesizeWords(makeLine(QStringLiteral("一二三四五六七"), 1000, 1100));
        QCOMPARE(words.size(), 7);
        QCOMPARE(words.first().startMs, 1000);
        QCOMPARE(words.last().endMs, 1100);
        for (qsizetype i = 1; i < words.size(); ++i) {
            QCOMPARE(words.at(i).startMs, words.at(i - 1).endMs);
        }
    }

    void emptyTextYieldsNoWords()
    {
        QVERIFY(synthesizeWords(makeLine(QString(), 0, 1000)).isEmpty());
    }

    void whitespaceOnlyTextYieldsNoWords()
    {
        QVERIFY(synthesizeWords(makeLine(QStringLiteral("   　  "), 0, 1000)).isEmpty());
    }

    void nonPositiveDurationYieldsNoWords()
    {
        QVERIFY(synthesizeWords(makeLine(QStringLiteral("text"), 1000, 1000)).isEmpty());
        QVERIFY(synthesizeWords(makeLine(QStringLiteral("text"), 1000, 500)).isEmpty());
    }

    void singleCharacterLineProducesOneWordSpanningTheWholeLine()
    {
        const auto words = synthesizeWords(makeLine(QStringLiteral("哦"), 500, 800));
        QCOMPARE(words.size(), 1);
        QCOMPARE(words.at(0).text, QStringLiteral("哦"));
        QCOMPARE(words.at(0).startMs, 500);
        QCOMPARE(words.at(0).endMs, 800);
    }

    void synthesizedWordsCarryNoRomanization()
    {
        const auto words = synthesizeWords(makeLine(QStringLiteral("哦"), 0, 100));
        QCOMPARE(words.size(), 1);
        QVERIFY(!words.at(0).romanization.has_value());
    }
};

QTEST_GUILESS_MAIN(WordSynthesisTest)
#include "tst_wordsynthesis.moc"
