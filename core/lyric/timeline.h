#pragma once

#include "lyricmodel.h"

#include <optional>

namespace PlasmaLyrics {

void finalizeEndTimes(LyricLines &lines, qint64 maximumDisplayMs = 10000);
/// When lines[index] moves into the current place, in lyric time (offset not
/// applied), with the next line lead `leadMs` of DESIGN.md decision 81: `leadMs`
/// before its start, but not before the line ahead of its group ends nor after
/// its own start. The line ahead is the last one starting strictly earlier, so
/// every line of a group shares one entry. A negative lead counts as 0, where
/// the entry is the start itself.
qint64 lineEntryMs(const LyricLines &lines, qsizetype index, int leadMs);
/// The line in the current place at the offset-adjusted position: the last one
/// entered by then (lineEntryMs()), the last of a group, or -1 once it has
/// ended. With `leadMs` 0 a line enters at its start.
int currentLineIndex(const LyricLines &lines, qint64 positionMs, int offsetMs = 0, int leadMs = 0);
/// The line coming up (DESIGN.md decision 80): the first to enter after the
/// offset-adjusted position, the last of them when several share that start --
/// the one currentLineIndex() lands on once it enters -- or -1 when every line
/// has entered. Line ends play no part beyond their say in the entry, so it
/// still answers in the intro and inside a long interlude, where
/// currentLineIndex() is -1.
int nextLineIndex(const LyricLines &lines, qint64 positionMs, int offsetMs = 0, int leadMs = 0);
/// The next position at which currentLineIndex() or nextLineIndex() can return
/// something else, or nullopt once the timeline has nothing left to switch to.
/// A superset of the real change points: waking on a boundary that leaves both
/// indices alone costs one no-op recomputation, missing one would freeze the
/// display.
std::optional<qint64> nextBoundaryMs(const LyricLines &lines, qint64 positionMs, int offsetMs = 0,
                                     int leadMs = 0);
/// True when a line is a production credit either by provider-flagged
/// LyricLine::credit or by its shape ("head：rest", head <= 12 chars, no
/// embedded whitespace/colon). See DESIGN.md 6.1 for the verified formats
/// and known false-positive shapes.
bool looksLikeCredit(const LyricLine &line);
LyricLines filterLeadingCredits(const LyricLines &lines, qint64 introLimitMs = 30000);

} // namespace PlasmaLyrics

