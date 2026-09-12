#pragma once

#include "lyricmodel.h"

#include <QStringList>
#include <QStringView>

namespace PlasmaLyrics {

struct ParsedQrc {
    LyricLines lines;
    QString title;
    QStringList artists;
    QString album;
    int embeddedOffsetMs = 0;
};

/// Parses QQ Music's QRC lyric format: an XML envelope whose LyricContent
/// attribute holds an LRC-like body where every line and every word carries
/// "[start,duration]" / "(start,duration)" in milliseconds.
///
/// Decryption is deliberately not here. QRC arrives encrypted, but unwrapping
/// it is a provider concern (see providers/qq/qrccipher.h); this stays a pure
/// parser so core keeps no cipher and no network.
class QrcParser
{
public:
    /// Parses one already-decrypted QRC document. A document with no
    /// LyricContent attribute yields an empty result rather than an error:
    /// callers treat "no word-level lyrics" as routine and fall back.
    static ParsedQrc parse(QStringView document);

    /// Builds the display document from the three payloads a lyric response
    /// carries. content is required; romanization is a second QRC document
    /// whose words are matched to content's by time, and translation is plain
    /// LRC. Either companion may be empty.
    static LyricDocument assemble(QStringView content, QStringView romanization,
                                  QStringView translation);
};

} // namespace PlasmaLyrics
