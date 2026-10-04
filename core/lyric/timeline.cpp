#include "timeline.h"

#include <QRegularExpression>
#include <algorithm>

namespace PlasmaLyrics {

void finalizeEndTimes(LyricLines &lines, qint64 maximumDisplayMs)
{
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (lines[index].endMs > lines[index].startMs) {
            continue;
        }
        qint64 nextStart = lines[index].startMs + maximumDisplayMs;
        for (qsizetype next = index + 1; next < lines.size(); ++next) {
            if (lines[next].startMs > lines[index].startMs) {
                nextStart = lines[next].startMs;
                break;
            }
        }
        lines[index].endMs = std::min(nextStart, lines[index].startMs + maximumDisplayMs);
    }
}

qint64 lineEntryMs(const LyricLines &lines, qsizetype index, int leadMs)
{
    // DESIGN.md decision 81. Lines are in start order, so the line ahead is
    // the one just before this line's group. Taking the later of its end and
    // `startMs - lead` moves a line in as soon as the one ahead is sung when
    // the gap between them is shorter than the lead; capping at the line's
    // own start keeps an overlapping line, whose predecessor ends after it
    // starts, from being held back. With a lead of 0 both bounds collapse
    // onto startMs.
    const qint64 lead = std::max(leadMs, 0);
    const qint64 startMs = lines[index].startMs;
    qsizetype first = index;
    while (first > 0 && lines[first - 1].startMs == startMs) {
        --first;
    }
    if (first == 0) {
        return startMs - lead;
    }
    return std::min(startMs, std::max(lines[first - 1].endMs, startMs - lead));
}

int currentLineIndex(const LyricLines &lines, qint64 positionMs, int offsetMs, int leadMs)
{
    // Entries rise strictly from one group to the next: the line ahead of a
    // group ends after its own start, which no earlier entry is later than.
    // So the first entered line found from the back is the last line of the
    // latest group to enter.
    const qint64 adjustedPosition = positionMs - offsetMs;
    for (qsizetype index = lines.size(); index > 0; --index) {
        const auto &line = lines[index - 1];
        if (lineEntryMs(lines, index - 1, leadMs) <= adjustedPosition) {
            return adjustedPosition < line.endMs ? static_cast<int>(index - 1) : -1;
        }
    }
    return -1;
}

int nextLineIndex(const LyricLines &lines, qint64 positionMs, int offsetMs, int leadMs)
{
    // Lines are in start order (every parser stable-sorts them), the same
    // assumption currentLineIndex() makes. "Not entered" is the complement of
    // its `entry <= adjustedPosition`: a line entering exactly at the position
    // is already current, not next. The answer only moves when the position
    // passes some line's entry, and every entry is among nextBoundaryMs()'s
    // candidates.
    const qint64 adjustedPosition = positionMs - offsetMs;
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (lineEntryMs(lines, index, leadMs) > adjustedPosition) {
            while (index + 1 < lines.size() && lines[index + 1].startMs == lines[index].startMs) {
                ++index;
            }
            return static_cast<int>(index);
        }
    }
    return -1;
}

std::optional<qint64> nextBoundaryMs(const LyricLines &lines, qint64 positionMs, int offsetMs,
                                     int leadMs)
{
    // Every index change happens on some line's entry or end, but ends are not
    // ordered -- an overlapping line can end after the next one begins -- so the
    // whole list is scanned rather than stopping at the first later start. With
    // no lead every entry is a start, and the candidates are starts and ends
    // alone (DESIGN.md decision 38); a lead adds each line's entry (decision 81).
    const qint64 adjustedPosition = positionMs - offsetMs;
    std::optional<qint64> boundary;
    const auto consider = [&](qint64 candidate) {
        if (candidate > adjustedPosition && (!boundary || candidate < *boundary)) {
            boundary = candidate;
        }
    };
    for (qsizetype index = 0; index < lines.size(); ++index) {
        consider(lines[index].startMs);
        consider(lines[index].endMs);
        if (leadMs > 0) {
            consider(lineEntryMs(lines, index, leadMs));
        }
    }
    if (!boundary) {
        return std::nullopt;
    }
    return *boundary + offsetMs;
}

bool looksLikeCredit(const LyricLine &line)
{
    // Credits reach us in two shapes. NetEase's structured entries are flagged
    // during parsing, which is authoritative. Plain timestamped lines such as
    // "[00:02.80]编曲/伴奏混音：闹闹丶" only have their shape to go on, and the
    // padding around the colon varies by endpoint -- /api/song/lyric renders the
    // same credit as "作词 : 爆音常安" -- so it is collapsed before matching.
    // See DESIGN.md 6.1 for the verified formats and the known gaps.
    static const QRegularExpression creditExpression(QStringLiteral(R"(^[^\s：:]{1,12}[：:]\s*.+$)"));
    static const QRegularExpression colonPadding(QStringLiteral(R"(\s*([：:])\s*)"));
    if (line.credit) {
        return true;
    }
    const QString collapsed = QString(line.text).replace(colonPadding, QStringLiteral("\\1"));
    return creditExpression.match(collapsed).hasMatch();
}

LyricLines filterLeadingCredits(const LyricLines &lines, qint64 introLimitMs)
{
    qsizetype firstLyric = 0;
    while (firstLyric < lines.size()) {
        const auto &line = lines[firstLyric];
        if (line.startMs > introLimitMs) {
            break;
        }
        if (!looksLikeCredit(line)) {
            break;
        }
        ++firstLyric;
    }
    return lines.sliced(firstLyric);
}

} // namespace PlasmaLyrics

