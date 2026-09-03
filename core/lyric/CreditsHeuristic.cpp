// SPDX-License-Identifier: GPL-2.0-or-later

#include "lyric/CreditsHeuristic.h"

#include <QRegularExpression>

namespace Lyrics
{

// Colon-structure: 1-12 non-space, non-colon characters, then a full-width or
// ASCII colon, then anything. The {1,12} upper bound is measured against the
// real library (§6.1): "编曲/伴奏混音" (7 chars) is credits, "Mix&Mastering"
// (13 chars) is a documented miss we accept rather than widen into.
static const QRegularExpression kCreditsLine(
    QStringLiteral("^[^\\s：:]{1,12}[：:]\\s*.+$"));

int firstRealLyricLine(const QStringList &lines)
{
    for (int i = 0; i < lines.size(); ++i) {
        if (!kCreditsLine.match(lines.at(i)).hasMatch()) {
            return i;
        }
    }
    return -1;
}

} // namespace Lyrics
