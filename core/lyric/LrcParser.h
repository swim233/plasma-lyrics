// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * LRC parsing, at the shape the netease plaintext API actually returns it.
 * The fixtures under providers/tests/fixtures carry the traps this parser
 * must survive:
 *   - the first lines are JSON objects `{"t":..,"c":[{"tx":"作词: "},..]}`
 *     (credits), which a standard LRC parser chokes on;
 *   - timestamps with two or three millisecond digits (`[00:29.63]`,
 *     `[00:29.638]`);
 *   - several timestamps opening one line;
 *   - a global `[offset:]` tag shifting every timestamp.
 *
 * Run with: ctest --test-dir build --output-on-failure -R lrcparser
 */

#pragma once

#include "lyric/LyricLine.h"

#include <QString>

namespace Lyrics
{

struct LrcParseResult
{
    LyricLines lines;
    bool ok = false;
};

// Parses LRC as netease's plaintext API returns it: bracketed timestamps,
// leading JSON credit lines (`{"t":..,"c":[..]}`), 2- or 3-digit millisecond
// fractions, multiple timestamps per line, and a global `[offset:]` tag.
LrcParseResult parseLrc(const QString &lrc);

} // namespace Lyrics
