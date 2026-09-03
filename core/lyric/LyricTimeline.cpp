// SPDX-License-Identifier: GPL-2.0-or-later

#include "lyric/LyricTimeline.h"

#include <algorithm>

namespace Lyrics
{

LyricTimeline::LyricTimeline(const LyricLines &lines)
    : m_lines(lines)
{
    // The parser guarantees sorted lines; assert the contract in debug builds
    // rather than silently re-sorting (which would hide parser bugs).
    Q_ASSERT(std::is_sorted(m_lines.cbegin(), m_lines.cend(),
                            [](const LyricLine &a, const LyricLine &b) {
                                return a.startMs < b.startMs;
                            }));
}

int LyricTimeline::lineAt(qint64 positionMs) const
{
    // Upper bound by startMs: the candidate line is the last one whose
    // startMs is <= positionMs. Before the first line this is cbegin().
    const auto it = std::upper_bound(m_lines.cbegin(), m_lines.cend(), positionMs,
                                     [](qint64 pos, const LyricLine &line) {
                                         return pos < line.startMs;
                                     });
    if (it == m_lines.cbegin()) {
        return -1;
    }
    const auto &line = *std::prev(it);
    // An interlude (a gap between one line's endMs and the next startMs)
    // shows nothing -- unless the line's end is open (endMs == startMs), the
    // parser's marker for "hold until the track ends".
    if (line.endMs != line.startMs && positionMs > line.endMs) {
        return -1;
    }
    return static_cast<int>(std::distance(m_lines.cbegin(), it) - 1);
}

} // namespace Lyrics
