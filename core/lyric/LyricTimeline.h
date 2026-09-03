// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QList>
#include <QString>

namespace Lyrics
{

// A single lyric line (DESIGN §2.2): start, end, and current text.
struct LyricLine
{
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
};

using LyricLines = QList<LyricLine>;

// Answers "which line is current at positionMs?" over sorted,
// parser-normalized lines. Returns -1 before the first line or inside an
// interlude (a gap between one line's endMs and the next startMs). The last
// line holds to the end of the track, because its endMs is open.
class LyricTimeline
{
public:
    explicit LyricTimeline(const LyricLines &lines);

    int lineAt(qint64 positionMs) const;
    const LyricLines &lines() const { return m_lines; }

private:
    LyricLines m_lines;
};

} // namespace Lyrics
