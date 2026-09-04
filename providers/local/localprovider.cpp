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

QList<Candidate> LocalProvider::search(const TrackQuery &)
{
    return {};
}

std::optional<LyricDocument> LocalProvider::fetch(const QString &)
{
    return std::nullopt;
}

QString LocalProvider::lastError() const
{
    return {};
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
