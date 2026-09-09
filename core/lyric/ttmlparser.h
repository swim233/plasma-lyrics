#pragma once

#include "core/lyric/lyricmodel.h"

#include <QByteArray>
#include <QString>
#include <optional>

namespace PlasmaLyrics {

class TtmlParser
{
public:
    static std::optional<LyricDocument> parse(const QByteArray &payload,
                                              QString *error = nullptr);
    static std::optional<qint64> parseTime(const QString &value);
};

} // namespace PlasmaLyrics
