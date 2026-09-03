// SPDX-License-Identifier: GPL-2.0-or-later

#include "netease/NeteaseParse.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Lyrics
{

QList<NeteaseSong> parseNeteaseSearch(const QByteArray &body)
{
    QList<NeteaseSong> songs;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        return songs;
    }
    const QJsonArray arr = doc.object()
                               .value(QLatin1String("result"))
                               .toObject()
                               .value(QLatin1String("songs"))
                               .toArray();
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        NeteaseSong song;
        song.trackId = QString::number(o.value(QLatin1String("id")).toVariant().toLongLong());
        song.title = o.value(QLatin1String("name")).toString();
        song.durationMs = o.value(QLatin1String("duration")).toVariant().toLongLong();
        song.album = o.value(QLatin1String("album")).toObject().value(QLatin1String("name")).toString();
        const QJsonArray artists = o.value(QLatin1String("artists")).toArray();
        for (const auto &a : artists) {
            const QString name = a.toObject().value(QLatin1String("name")).toString();
            if (!name.isEmpty()) {
                song.artists << name;
            }
        }
        if (!song.trackId.isEmpty() && !song.title.isEmpty()) {
            songs << song;
        }
    }
    return songs;
}

QString parseNeteaseLyric(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        return QString();
    }
    return doc.object()
        .value(QLatin1String("lrc"))
        .toObject()
        .value(QLatin1String("lyric"))
        .toString();
}

} // namespace Lyrics
