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

int currentLineIndex(const LyricLines &lines, qint64 positionMs, int offsetMs)
{
    const qint64 adjustedPosition = positionMs - offsetMs;
    for (qsizetype index = lines.size(); index > 0; --index) {
        const auto &line = lines[index - 1];
        if (line.startMs <= adjustedPosition) {
            return adjustedPosition < line.endMs ? static_cast<int>(index - 1) : -1;
        }
    }
    return -1;
}

LyricLines filterLeadingCredits(const LyricLines &lines, qint64 introLimitMs)
{
    // Credits reach us in two shapes. NetEase's structured entries are flagged
    // during parsing, which is authoritative. Plain timestamped lines such as
    // "[00:02.80]编曲/伴奏混音：闹闹丶" only have their shape to go on, and the
    // padding around the colon varies by endpoint -- /api/song/lyric renders the
    // same credit as "作词 : 爆音常安" -- so it is collapsed before matching.
    // See DESIGN.md 6.1 for the verified formats and the known gaps.
    static const QRegularExpression creditExpression(QStringLiteral(R"(^[^\s：:]{1,12}[：:]\s*.+$)"));
    static const QRegularExpression colonPadding(QStringLiteral(R"(\s*([：:])\s*)"));
    qsizetype firstLyric = 0;
    while (firstLyric < lines.size()) {
        const auto &line = lines[firstLyric];
        if (line.startMs > introLimitMs) {
            break;
        }
        if (!line.credit) {
            const QString collapsed =
                QString(line.text).replace(colonPadding, QStringLiteral("\\1"));
            if (!creditExpression.match(collapsed).hasMatch()) {
                break;
            }
        }
        ++firstLyric;
    }
    return lines.sliced(firstLyric);
}

} // namespace PlasmaLyrics

