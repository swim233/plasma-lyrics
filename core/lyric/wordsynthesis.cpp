#include "wordsynthesis.h"

#include <cmath>

namespace PlasmaLyrics {

namespace {

bool isCjkChar(QChar ch)
{
    switch (ch.script()) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
    case QChar::Script_Hangul:
    case QChar::Script_Bopomofo:
        return true;
    default:
        return false;
    }
}

} // namespace

QList<LyricWord> synthesizeWords(const LyricLine &line)
{
    QList<LyricWord> result;
    if (line.text.trimmed().isEmpty() || line.endMs <= line.startMs) {
        return result;
    }

    // Token boundaries are decided here; timing is a second, independent pass
    // below over the finished token texts, so the two concerns (what counts
    // as a word, how long each gets) never tangle.
    QList<QString> tokens;
    // Whitespace seen before any token exists (a leading run -- 0 lines of
    // the verified corpus have one, but the rule is "attach to the previous
    // token", and there is no previous token yet). Held here and folded onto
    // the head of the first token once one appears, rather than ever
    // becoming a token of its own.
    QString leadingWhitespace;
    // True while the last entry in `tokens` is a still-open Latin/digit/
    // punctuation run that the next non-whitespace, non-CJK character should
    // extend rather than start a new token for. A CJK character or a run of
    // whitespace always closes it.
    bool runOpen = false;

    for (const QChar ch : line.text) {
        if (ch.isSpace()) {
            if (tokens.isEmpty()) {
                leadingWhitespace += ch;
            } else {
                tokens.last() += ch;
            }
            runOpen = false;
            continue;
        }
        if (isCjkChar(ch)) {
            QString token = leadingWhitespace;
            leadingWhitespace.clear();
            token += ch;
            tokens.append(token);
            runOpen = false;
            continue;
        }
        if (runOpen) {
            tokens.last() += ch;
        } else {
            QString token = leadingWhitespace;
            leadingWhitespace.clear();
            token += ch;
            tokens.append(token);
            runOpen = true;
        }
    }
    if (tokens.isEmpty()) {
        // Defensive only: line.text.trimmed() being non-empty guarantees at
        // least one non-whitespace character was seen above, which always
        // opens a token.
        return result;
    }

    qint64 totalWeight = 0;
    for (const QString &token : tokens) {
        totalWeight += token.size();
    }
    if (totalWeight <= 0) {
        return result;
    }

    // Prefix-sum allocation: each token's endMs is rounded independently from
    // the running total rather than accumulating per-token rounding error, so
    // the very last token's endMs lands exactly on line.endMs regardless of
    // how many tokens came before it.
    const qint64 total = line.endMs - line.startMs;
    qint64 accumulatedWeight = 0;
    qint64 previousEndMs = line.startMs;
    result.reserve(tokens.size());
    for (const QString &token : tokens) {
        accumulatedWeight += token.size();
        LyricWord word;
        word.startMs = previousEndMs;
        word.endMs = line.startMs
            + std::llround(static_cast<double>(total) * static_cast<double>(accumulatedWeight)
                            / static_cast<double>(totalWeight));
        word.text = token;
        word.romanization = std::nullopt;
        result.append(word);
        previousEndMs = word.endMs;
    }
    return result;
}

} // namespace PlasmaLyrics
