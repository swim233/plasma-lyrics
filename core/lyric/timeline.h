#pragma once

#include "lyricmodel.h"

namespace PlasmaLyrics {

void finalizeEndTimes(LyricLines &lines, qint64 maximumDisplayMs = 10000);
int currentLineIndex(const LyricLines &lines, qint64 positionMs, int offsetMs = 0);
LyricLines filterLeadingCredits(const LyricLines &lines, qint64 introLimitMs = 30000);

} // namespace PlasmaLyrics

