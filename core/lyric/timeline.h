#pragma once

#include "lyricmodel.h"

#include <optional>

namespace PlasmaLyrics {

void finalizeEndTimes(LyricLines &lines, qint64 maximumDisplayMs = 10000);
int currentLineIndex(const LyricLines &lines, qint64 positionMs, int offsetMs = 0);
/// The next position at which currentLineIndex() can return something else, or
/// nullopt once the timeline has nothing left to switch to. A superset of the
/// real change points: waking on a boundary that leaves the index alone costs
/// one no-op recomputation, missing one would freeze the display.
std::optional<qint64> nextBoundaryMs(const LyricLines &lines, qint64 positionMs, int offsetMs = 0);
/// True when a line is a production credit either by provider-flagged
/// LyricLine::credit or by its shape ("head：rest", head <= 12 chars, no
/// embedded whitespace/colon). See DESIGN.md 6.1 for the verified formats
/// and known false-positive shapes.
bool looksLikeCredit(const LyricLine &line);
LyricLines filterLeadingCredits(const LyricLines &lines, qint64 introLimitMs = 30000);

} // namespace PlasmaLyrics

