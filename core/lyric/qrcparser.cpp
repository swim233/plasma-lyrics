#include "qrcparser.h"

#include "lrcparser.h"
#include "timeline.h"

#include <QRegularExpression>
#include <algorithm>

namespace PlasmaLyrics {
namespace {

// The companion payloads are produced by a different pass of the same
// authoring tool and round their line and word times independently, so the
// same line can be a millisecond or two apart between them. Both observed
// deviations are 1 ms; the window is set well above that and still far below
// any real gap between adjacent lines.
constexpr qint64 kCompanionToleranceMs = 20;

// contentts timestamps are LRC, whose finest unit is a centisecond, while QRC
// line starts are exact milliseconds -- so a translation line is routinely up
// to 9 ms early. Matching them exactly (as LrcParser::merge does, which is
// right for two LRC files) aligns only a handful of lines per track.
constexpr qint64 kTranslationToleranceMs = 15;

// QQ writes this in contentts for a line it has no translation for. Taken
// literally it would put "//" under every untranslated line.
const QString &translationPlaceholder()
{
    static const QString placeholder = QStringLiteral("//");
    return placeholder;
}

// The body lives in an XML attribute but must not be read with an XML parser:
// attribute-value normalization turns every newline into a space (XML 1.0
// section 3.3.3), which would collapse the whole lyric into one line. The
// attribute is scanned out by hand and unescaped afterwards instead.
//
// The value ends at the first '"' followed by optional whitespace and "/>",
// not at the first '"': QQ writes raw quotes inside the body. musicid
// 394368429 carries four (around "今日" and "明日"), and stopping at the first
// one kept 22 of its 66 lines. The [ti:], [ar:] and [al:] tags come ahead of
// every timed line, so a quote in the title, artist or album -- "Slut!",
// musicid 446012964 -- left none at all.
//
// The rule rests on evidence, not on the format ruling the case out: none of
// the seven recorded bodies (the six fixtures plus 446012964) has a '"'
// followed by '>' or "/>" anywhere before its real end, and a lyric would
// have to spell out those characters itself to be cut short. A bare '>' is
// not taken as the end, since every recorded payload closes the element with
// "/>".
QString extractLyricContent(QStringView document)
{
    static const QString marker = QStringLiteral("LyricContent=\"");
    static const QRegularExpression terminator(QStringLiteral(R"("\s*/>)"));
    const qsizetype begin = document.indexOf(marker);
    if (begin < 0) {
        return {};
    }
    const qsizetype valueStart = begin + marker.size();
    const auto end = terminator.matchView(document, valueStart);
    if (!end.hasMatch()) {
        return {};
    }
    QString value = document.sliced(valueStart, end.capturedStart() - valueStart).toString();
    // &quot; and the other four predefined entities are unescaped here, and
    // &amp; last so that an escaped "&amp;lt;" survives as the literal
    // "&lt;".
    value.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    value.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    value.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    value.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    value.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return value;
}

// A "(start,duration)" group is a word's timestamp only when its start lies
// inside the line's own [start, start + duration]. Any other group is lyric
// text that happens to look like one -- "(1,2)", or a year such as
// "(2020,1)" -- and stays inside the word it sits in instead of splitting it.
// Measured over six real payloads (the four fixtures plus content and
// romanization of musicid 394368429), 3,392 word starts with none outside
// their line's window, so the test is exact, with no tolerance, and costs no
// real word. Adjacency cannot make the call instead: a real word can have
// empty text (that romanization has "[6539,1474](7592,421)") and 65 more are
// whitespace only, so two groups side by side say nothing about either.
//
// Two kinds of line keep the old reading, where every group is a timestamp,
// so that no line that parsed before loses its words: one with no group
// inside its window at all, and one of zero duration, whose window [s, s]
// would turn every word after the first into text. Neither occurs in the
// measured payloads.
QList<LyricWord> parseWords(const QString &payload, qint64 lineStartMs, qint64 lineDurationMs)
{
    // Lazy leading group so a literal '(' inside the word text is handled:
    // the title line of a track whose name carries a parenthetical reads
    // "人(659,40) ((699,80)", where the word text is " (".
    static const QRegularExpression token(QStringLiteral(R"((.*?)\((\d+),(\d+)\))"),
                                          QRegularExpression::DotMatchesEverythingOption);
    struct Group {
        QRegularExpressionMatch match;
        qint64 start = 0;
        qint64 duration = 0;
        bool timestamp = false;
    };
    QList<Group> groups;
    bool anyInWindow = false;
    auto iterator = token.globalMatch(payload);
    while (iterator.hasNext()) {
        auto match = iterator.next();
        bool startOk = false;
        bool durationOk = false;
        const qint64 start = match.capturedView(2).toLongLong(&startOk);
        const qint64 duration = match.capturedView(3).toLongLong(&durationOk);
        // A group too long for qint64 reads as 0, which would land inside
        // the window of a line starting at 0, hence the ok flags. The window
        // test is a difference so that a huge start cannot overflow a sum.
        const bool inWindow = startOk && durationOk && start >= lineStartMs
            && start - lineStartMs <= lineDurationMs;
        anyInWindow |= inWindow;
        groups.append({.match = std::move(match), .start = start, .duration = duration, .timestamp = inWindow});
    }
    const bool windowed = anyInWindow && lineDurationMs > 0;

    QList<LyricWord> words;
    // Text read since the last accepted timestamp: word text, plus every
    // rejected group exactly as it was written.
    QString pending;
    qsizetype pendingFrom = 0;
    bool pendingHasRejected = false;
    for (const auto &group : groups) {
        pending += group.match.capturedView(1);
        if (windowed && !group.timestamp) {
            pending += group.match.capturedView(0).sliced(group.match.capturedLength(1));
            pendingHasRejected = true;
            continue;
        }
        words.append({.startMs = group.start, .endMs = group.start + group.duration, .text = pending, .romanization = std::nullopt});
        pending.clear();
        pendingFrom = group.match.capturedEnd(0);
        pendingHasRejected = false;
    }
    // Everything after the last accepted timestamp is one run. Untimed text
    // with no group in it is dropped, as it always was; a run holding a
    // rejected group is lyric text that ends the line, so it joins the last
    // word, untimed tail included, rather than being lost.
    if (pendingHasRejected && !words.isEmpty()) {
        words.last().text += payload.sliced(pendingFrom);
    }
    return words;
}

// The last word whose start is not after timeMs -- deliberately not an
// "is timeMs inside [start, end)" test, since a time falling in a gap
// between two words must still resolve to the preceding one rather than to
// nothing. Durations are not consulted and are not passed in; which time the
// caller hands over is the caller's decision, and applyRomanization explains
// why it hands over a midpoint.
qsizetype wordIndexForTime(const QList<LyricWord> &words, qint64 timeMs)
{
    if (words.isEmpty()) {
        return -1;
    }
    qsizetype index = 0;
    for (qsizetype candidate = 0; candidate < words.size(); ++candidate) {
        if (words[candidate].startMs <= timeMs) {
            index = candidate;
        } else {
            break;
        }
    }
    return index;
}

void applyRomanization(LyricLines &lines, const LyricLines &romanized)
{
    // A two-pointer walk rather than index pairing: the counts match on every
    // sample seen, but a companion that gained or lost a line must skew one
    // line rather than every line after it.
    qsizetype romaIndex = 0;
    for (auto &line : lines) {
        while (romaIndex < romanized.size()
               && romanized[romaIndex].startMs + kCompanionToleranceMs < line.startMs) {
            ++romaIndex;
        }
        if (romaIndex >= romanized.size()
            || qAbs(romanized[romaIndex].startMs - line.startMs) > kCompanionToleranceMs) {
            continue;
        }
        const auto &source = romanized[romaIndex];
        ++romaIndex;
        if (!source.words || !line.words || line.words->isEmpty()) {
            continue;
        }
        QStringList perWord(line.words->size());
        for (const auto &mora : *source.words) {
            // A mora is placed by its midpoint, not its start: the two
            // payloads round their times independently, so a mora starting at
            // 232 belongs to the word starting at 233 and comparing starts
            // would hand it to the previous word. Measured on the reference
            // track, 283 of 715 tokens (about 40%) land on a different word
            // if start is used, and no midpoint falls in a gap between words.
            const qint64 midpoint = mora.startMs + (mora.endMs - mora.startMs) / 2;
            const qsizetype index = wordIndexForTime(*line.words, midpoint);
            if (index >= 0) {
                perWord[index] += mora.text;
            }
        }
        QStringList lineParts;
        for (qsizetype index = 0; index < line.words->size(); ++index) {
            // The source separates morae with a trailing space ("wa ku "),
            // which is a delimiter inside a word, not part of it.
            const QString word = perWord[index].simplified().remove(QLatin1Char(' '));
            if (word.isEmpty()) {
                continue;
            }
            (*line.words)[index].romanization = word;
            lineParts.append(word);
        }
        // Built from the same per-word strings that were just stored, so the
        // line and its words can never disagree.
        if (!lineParts.isEmpty()) {
            line.romanization = lineParts.join(QLatin1Char(' '));
        }
    }
}

void applyTranslation(LyricLines &lines, QStringView translation)
{
    if (translation.trimmed().isEmpty()) {
        return;
    }
    const auto parsed = LrcParser::parse(translation);
    qsizetype index = 0;
    for (auto &line : lines) {
        while (index < parsed.lines.size()
               && parsed.lines[index].startMs + kTranslationToleranceMs < line.startMs) {
            ++index;
        }
        if (index >= parsed.lines.size()
            || qAbs(parsed.lines[index].startMs - line.startMs) > kTranslationToleranceMs) {
            continue;
        }
        const QString text = parsed.lines[index].text.trimmed();
        ++index;
        if (text.isEmpty() || text == translationPlaceholder() || text == line.text) {
            continue;
        }
        line.translation = text;
    }
}

// QQ opens a lyric with the track's own "<title> - <artist>" line and then
// its production credits, all as ordinary timed body lines. The title line is
// flagged from the document's own [ti]/[ar] tags, which is proof rather than
// a guess. The credit lines that follow it are flagged by shape, but only
// while still inside the leading run -- looksLikeCredit cannot catch them on
// its own because QQ writes "Lyrics by：MSR Studio", whose head carries a
// space and exceeds the twelve characters DESIGN.md 6.1 allows. Widening that
// shared heuristic would change what every other provider filters, so the
// looser rule is kept here, where the format is known and the run is bounded.
bool isTitleLine(const QString &text, const ParsedQrc &parsed)
{
    if (parsed.artists.isEmpty()) {
        return false;
    }
    // The line is "<title> - <artists>", but the two halves cannot simply be
    // compared against the tags: the [ti:] tag often carries a parenthetical
    // the line leaves off ("万物有灵 (剑灵八职业同人曲)" against a line reading
    // "万物有灵 - 洛天依"). The artist half is the reliable anchor, so it is
    // required to match exactly and the title half only has to be consistent.
    const qsizetype separator = text.lastIndexOf(QStringLiteral(" - "));
    if (separator <= 0) {
        return false;
    }
    const QString trailing = text.sliced(separator + 3).trimmed();
    const QString leading = text.first(separator).trimmed();
    if (leading.isEmpty()) {
        return false;
    }
    QStringList acceptedArtists{parsed.artists.join(QLatin1Char('/'))};
    acceptedArtists.append(parsed.artists);
    bool artistMatches = false;
    for (const auto &artist : acceptedArtists) {
        artistMatches |= trailing == artist.simplified();
    }
    if (!artistMatches) {
        return false;
    }
    const QString title = parsed.title.simplified();
    return title.isEmpty() || leading == title || title.startsWith(leading)
        || leading.startsWith(title);
}

void markLeadingCredits(ParsedQrc &parsed)
{
    // Deliberately looser than looksLikeCredit -- it admits a head carrying
    // spaces and up to 24 characters, because QQ writes "Lyrics by：MSR
    // Studio" -- and deliberately left that way, with a real cost that is
    // accepted rather than absent: a SUNG line containing a colon, reached
    // while the run is still going, is flagged and then dropped by
    // filterLeadingCredits, which is on by default. Measured: a line
    // "And then he said：don't go" two entries into the run is eaten, and
    // looksLikeCredit would not have flagged it, so this is exposure the
    // widened pattern adds.
    //
    // The bounds that do hold are the leading run and filterLeadingCredits'
    // 30 s intro window. The title line does NOT bound anything -- it starts
    // the run rather than ending it, since isTitleLine flags it and the loop
    // continues past it.
    //
    // Accepted because the run ends at the first line matching neither rule,
    // which on every sample is the first sung line, and because tightening
    // the pattern would put QQ's real credit lines back into the lyrics.
    static const QRegularExpression creditShape(QStringLiteral(R"(^[^：:]{1,24}[：:]\s*\S.*$)"));
    for (auto &line : parsed.lines) {
        const QString text = line.text.simplified();
        if (!isTitleLine(text, parsed) && !creditShape.match(text).hasMatch()) {
            break;
        }
        line.credit = true;
    }
}

} // namespace

ParsedQrc QrcParser::parse(QStringView document)
{
    static const QRegularExpression lineHeader(QStringLiteral(R"(^\[(\d+),(\d+)\](.*)$)"),
                                               QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression metadata(
        QStringLiteral(R"(^\[(ti|ar|al|offset)\s*:\s*(.*)\]$)"),
        QRegularExpression::CaseInsensitiveOption);

    ParsedQrc result;
    const QString body = extractLyricContent(document);
    if (body.isEmpty()) {
        return result;
    }
    const auto rawLines = body.split(QLatin1Char('\n'));
    for (QString rawLine : rawLines) {
        if (rawLine.endsWith(QLatin1Char('\r'))) {
            rawLine.chop(1);
        }
        const auto headerMatch = lineHeader.match(rawLine);
        if (headerMatch.hasMatch()) {
            const qint64 start = headerMatch.capturedView(1).toLongLong();
            const qint64 duration = headerMatch.capturedView(2).toLongLong();
            // Raw times: the [offset:] shift further down moves lines and
            // words together, so it never changes which group is in the
            // window.
            auto words = parseWords(headerMatch.captured(3), start, duration);
            QString text;
            for (const auto &word : words) {
                text += word.text;
            }
            if (text.trimmed().isEmpty()) {
                continue;
            }
            LyricLine line{.startMs = start, .endMs = start + duration, .text = text, .translation = std::nullopt, .romanization = std::nullopt, .words = std::nullopt};
            line.words = std::move(words);
            result.lines.append(std::move(line));
            continue;
        }
        // Everything else is a tag. [kana:...] in particular must not be read
        // as a lyric: it is a furigana index that carries its own
        // parenthesised timings and would otherwise parse as a word list.
        const auto metadataMatch = metadata.match(rawLine.trimmed());
        if (!metadataMatch.hasMatch()) {
            continue;
        }
        const QString key = metadataMatch.captured(1).toCaseFolded();
        const QString value = metadataMatch.captured(2).trimmed();
        if (key == QStringLiteral("ti")) {
            result.title = value;
        } else if (key == QStringLiteral("ar") && !value.isEmpty()) {
            result.artists.append(value);
        } else if (key == QStringLiteral("al")) {
            result.album = value;
        } else if (key == QStringLiteral("offset")) {
            bool ok = false;
            const int offset = value.toInt(&ok);
            if (ok) {
                result.embeddedOffsetMs = offset;
            }
        }
    }
    std::stable_sort(result.lines.begin(), result.lines.end(),
                     [](const auto &left, const auto &right) { return left.startMs < right.startMs; });
    // Applied to the times, exactly as LrcParser::parse does with the same
    // tag -- reporting it without applying it would silently mistime a whole
    // song. Every payload seen so far carries [offset:0], so this has no
    // observable effect today; that is precisely why it would go unnoticed.
    if (result.embeddedOffsetMs != 0) {
        const auto shift = [offset = result.embeddedOffsetMs](qint64 time) {
            return std::max<qint64>(0, time + offset);
        };
        for (auto &line : result.lines) {
            line.startMs = shift(line.startMs);
            line.endMs = shift(line.endMs);
            if (!line.words) {
                continue;
            }
            for (auto &word : *line.words) {
                word.startMs = shift(word.startMs);
                word.endMs = shift(word.endMs);
            }
        }
    }
    markLeadingCredits(result);
    return result;
}

LyricDocument QrcParser::assemble(QStringView content, QStringView romanization,
                                  QStringView translation)
{
    auto parsed = parse(content);
    if (parsed.lines.isEmpty()) {
        return {};
    }
    if (!romanization.trimmed().isEmpty()) {
        applyRomanization(parsed.lines, parse(romanization).lines);
    }
    applyTranslation(parsed.lines, translation);
    // Only fills lines whose own duration was zero; QRC states a real one for
    // almost every line.
    finalizeEndTimes(parsed.lines);
    const bool hasWords = std::any_of(parsed.lines.cbegin(), parsed.lines.cend(),
                                      [](const auto &line) {
                                          return line.words && !line.words->isEmpty();
                                      });
    // The embedded [offset:] tag has already been applied to the line and
    // word times by parse(). offsetMs stays reserved for the user's own
    // per-track adjustment, the same reservation LrcParser::merge documents.
    return {.lines = parsed.lines, .offsetMs = 0, .hasWords = hasWords, .metadata = {}};
}

} // namespace PlasmaLyrics
