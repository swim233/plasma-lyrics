#include "lyricoverridestore.h"

#include "core/lyric/lrcparser.h"

#include <QFile>
#include <QStandardPaths>

namespace PlasmaLyrics {

LyricOverrideStore::LyricOverrideStore(QString directory)
    : m_directory(directory.isEmpty() ? defaultDirectory() : std::move(directory))
{
}

QString LyricOverrideStore::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasma-lyrics/overrides");
}

std::optional<LyricDocument> LyricOverrideStore::lyric(const TrackRef &ref, int *droppedLines) const
{
    QFile file(m_directory + QLatin1Char('/') + ref.provider
               + QLatin1Char(':') + ref.trackId + QStringLiteral(".lrc"));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return LrcParser::parseBilingual(QString::fromUtf8(file.readAll()), droppedLines);
}

QString LyricOverrideStore::directory() const
{
    return m_directory;
}

} // namespace PlasmaLyrics
