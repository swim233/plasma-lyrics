#pragma once

#include "lyricmodel.h"

#include <QStringList>
#include <QStringView>

namespace PlasmaLyrics {

struct ParsedLrc {
    LyricLines lines;
    int embeddedOffsetMs = 0;
    QString title;
    QStringList artists;
    QString album;
    qint64 lengthMs = 0;
};

class LrcParser
{
public:
    static ParsedLrc parse(QStringView source);
    static LyricDocument merge(QStringView origin, QStringView translation);
};

} // namespace PlasmaLyrics
