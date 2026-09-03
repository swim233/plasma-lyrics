// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Lyrics
{

// One candidate song from netease search (DESIGN §1.2: clean artists array).
struct NeteaseSong
{
    QString trackId;
    QString title;
    QStringList artists;
    QString album;
    qint64 durationMs = 0;
};

// Parses the body of GET /api/search/get (type=1): result.songs[].{id, name,
// duration, artists[].name, album.name}. Returns an empty list on any shape
// mismatch -- search "查不到就算了" (DESIGN #4).
QList<NeteaseSong> parseNeteaseSearch(const QByteArray &body);

// Extracts the lrc.lyric text from a /api/song/lyric response body. The lrc
// field leads with JSON credit lines which parseLrc() handles; klyric is
// always empty and yrc is unreachable (DESIGN §1.2). Returns a null string
// when the body carries no lyric.
QString parseNeteaseLyric(const QByteArray &body);

} // namespace Lyrics
