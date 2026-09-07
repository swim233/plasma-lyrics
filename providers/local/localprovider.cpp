#include "localprovider.h"

#include "core/lyric/lrcparser.h"

#include <QFile>
#include <QStandardPaths>

namespace PlasmaLyrics {

LocalProvider::LocalProvider(QString overrideDirectory)
    : m_overrideDirectory(overrideDirectory.isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
              + QStringLiteral("/plasma-lyrics/overrides")
          : std::move(overrideDirectory))
{
}

QString LocalProvider::id() const
{
    return QStringLiteral("local");
}

bool LocalProvider::isConfigured() const
{
    return true;
}

bool LocalProvider::supportsSearch() const
{
    return false;
}

void LocalProvider::search(const TrackQuery &, SearchCallback callback)
{
    callback({{}, {}});
}

void LocalProvider::fetch(const QString &, FetchCallback callback)
{
    callback({std::nullopt, {}});
}

std::optional<LyricDocument> LocalProvider::overrideFor(const QString &providerId,
                                                        const QString &trackId)
{
    QFile file(m_overrideDirectory + QLatin1Char('/') + providerId
               + QLatin1Char(':') + trackId + QStringLiteral(".lrc"));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return LrcParser::merge(QString::fromUtf8(file.readAll()), QString());
}

} // namespace PlasmaLyrics
