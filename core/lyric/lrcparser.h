#pragma once

#include "lyricmodel.h"

#include <QStringView>

namespace PlasmaLyrics {

struct ParsedLrc {
    LyricLines lines;
    int embeddedOffsetMs = 0;
};

class LrcParser
{
public:
    static ParsedLrc parse(QStringView source);
    static LyricDocument merge(QStringView origin, QStringView translation);
};

} // namespace PlasmaLyrics

