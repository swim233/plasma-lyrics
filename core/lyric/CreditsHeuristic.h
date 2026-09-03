// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QStringList>

namespace Lyrics
{

// Returns the index of the first line that does not carry a credit marker
// (a short "label: value" prefix), or -1 when every line is credits.
// DESIGN §2.2 test 4.
int firstRealLyricLine(const QStringList &lines);

} // namespace Lyrics
