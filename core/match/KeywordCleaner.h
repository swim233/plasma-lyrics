// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Keyword cleaning (DESIGN #12 / §1.1). MPRIS hands xesam:artist as a
 * single-element array with one slash-joined DOMString:
 *   ['闹闹丶/FFF君/欧Ωhm/洛天依Official']
 * Searching that string against netease (whose artists are a clean array)
 * fails to match. Split on slashes, trim, drop empties.
 */

#pragma once

#include <QString>
#include <QStringList>

namespace Lyrics
{

// Cleans MPRIS artist keywords for search (DESIGN #12). pbi hands
// xesam:artist as a single-element array holding one slash-joined DOMString;
// netease's artists are a clean array, so the joined string never matches.
// Split on slashes, trim whitespace, drop empties. Order is preserved: it is
// the recorded payload's order.
QStringList cleanArtists(const QStringList &raw);

} // namespace Lyrics
