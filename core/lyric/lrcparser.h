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
    // Same-timestamp double-line convention used by LDDC/163MusicLyrics
    // bilingual exports: two adjacent lines sharing a startMs are original
    // and translation. Runs a credit-shaped or provider-flagged line takes
    // part in are left untouched. droppedLines, when given, receives the
    // count of lines this collapsed away.
    static LyricDocument parseBilingual(QStringView source, int *droppedLines = nullptr);
};

} // namespace PlasmaLyrics
