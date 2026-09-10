#include "localprovider.h"

#include "core/lyric/lrcparser.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

namespace PlasmaLyrics {

LocalProvider::LocalProvider(QString lyricsDirectory)
    : m_lyricsDirectory(lyricsDirectory.isEmpty()
          ? defaultLyricsDirectory() : std::move(lyricsDirectory))
{
}

QString LocalProvider::defaultLyricsDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasma-lyrics/lyrics");
}

QString LocalProvider::id() const
{
    return QStringLiteral("local");
}

bool LocalProvider::isConfigured() const
{
    return true;
}

QString LocalProvider::lyricsDirectory() const
{
    return m_lyricsDirectory;
}

QString LocalProvider::sidecarPath(const QString &mediaSrc) const
{
    const QUrl url(mediaSrc);
    if (!url.isLocalFile()) {
        return {};
    }
    const QFileInfo audio(url.toLocalFile());
    if (!audio.exists() || !audio.isFile()) {
        return {};
    }
    const QString conventional = audio.dir().filePath(audio.completeBaseName()
                                                       + QStringLiteral(".lrc"));
    if (QFileInfo::exists(conventional)) {
        return conventional;
    }
    const QString appended = audio.filePath() + QStringLiteral(".lrc");
    return QFileInfo::exists(appended) ? appended : QString();
}

Candidate LocalProvider::candidateForFile(const QString &path,
                                          const TrackQuery *sidecarQuery) const
{
    QFile file(path);
    ParsedLrc parsed;
    if (file.open(QIODevice::ReadOnly)) {
        parsed = LrcParser::parse(QString::fromUtf8(file.readAll()));
    }
    const QFileInfo info(path);
    QString fallbackTitle = info.completeBaseName();
    QStringList fallbackArtists;
    const qsizetype separator = fallbackTitle.indexOf(QStringLiteral(" - "));
    if (separator > 0) {
        fallbackArtists.append(fallbackTitle.left(separator).trimmed());
        fallbackTitle = fallbackTitle.mid(separator + 3).trimmed();
    }
    const QString canonical = info.canonicalFilePath().isEmpty()
        ? info.absoluteFilePath() : info.canonicalFilePath();
    const QString trackId = QString::fromLatin1(
        QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex());
    Candidate candidate;
    candidate.trackId = trackId;
    // A sidecar is an exact association established by its path. Embedded ID
    // tags are often copied from another file and must not cause Matcher to
    // reject that stronger evidence. Directory candidates, by contrast, rely
    // on their tags/filename for matching.
    candidate.title = sidecarQuery ? sidecarQuery->title
                                   : !parsed.title.isEmpty() ? parsed.title : fallbackTitle;
    candidate.artists = sidecarQuery ? sidecarQuery->artists
                                     : !parsed.artists.isEmpty() ? parsed.artists : fallbackArtists;
    candidate.album = sidecarQuery ? sidecarQuery->album : parsed.album;
    candidate.lengthMs = sidecarQuery ? sidecarQuery->lengthMs : parsed.lengthMs;
    candidate.contentId = info.absoluteFilePath();
    return candidate;
}

void LocalProvider::search(const TrackQuery &query, SearchCallback callback)
{
    QList<Candidate> candidates;
    const QString sidecar = sidecarPath(query.mediaSrc);
    if (!sidecar.isEmpty()) {
        candidates.append(candidateForFile(sidecar, &query));
        // The directory revision does not identify this adjacent file. Its
        // contents can be repaired in place without changing cacheVersion().
        callback({std::move(candidates), {}, false, cacheVersion(), false});
        return;
    }
    const QDir directory(m_lyricsDirectory);
    const auto files = directory.entryInfoList({QStringLiteral("*.lrc"), QStringLiteral("*.LRC")},
                                               QDir::Files | QDir::Readable,
                                               QDir::Name | QDir::IgnoreCase);
    for (const auto &file : files) {
        candidates.append(candidateForFile(file.absoluteFilePath()));
    }
    // A local-file request without a sidecar cannot use a provider-wide miss:
    // adding that sidecar later would not change the lyrics-directory version.
    callback({std::move(candidates), {}, false, cacheVersion(), query.mediaSrc.isEmpty()
                  || !QUrl(query.mediaSrc).isLocalFile()});
}

void LocalProvider::fetch(const QString &contentId, FetchCallback callback)
{
    QFile file(contentId);
    if (!file.open(QIODevice::ReadOnly)) {
        callback({std::nullopt, file.errorString(), false});
        return;
    }
    auto document = LrcParser::merge(QString::fromUtf8(file.readAll()), QString());
    document.metadata.insert(QStringLiteral("source"), QStringLiteral("local"));
    document.metadata.insert(QStringLiteral("path"), QFileInfo(contentId).absoluteFilePath());
    callback({std::move(document), {}, false});
}

QString LocalProvider::cacheVersion() const
{
    constexpr QByteArrayView separator("\0", 1);
    const QDir directory(m_lyricsDirectory);
    const auto files = directory.entryInfoList({QStringLiteral("*.lrc"), QStringLiteral("*.LRC")},
                                               QDir::Files, QDir::Name | QDir::IgnoreCase);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(directory.absolutePath().toUtf8());
    hash.addData(separator);
    hash.addData(QByteArray::number(files.size()));
    hash.addData(separator);
    for (const auto &file : files) {
        hash.addData(file.fileName().toUtf8());
        hash.addData(separator);
        hash.addData(QByteArray::number(file.size()));
        hash.addData(separator);
        hash.addData(QByteArray::number(file.lastModified().toMSecsSinceEpoch()));
        hash.addData(separator);
    }
    return QStringLiteral("local:") + QString::fromLatin1(hash.result().toHex());
}

} // namespace PlasmaLyrics
