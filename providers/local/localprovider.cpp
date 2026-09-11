#include "localprovider.h"

#include "core/log/logformat.h"
#include "core/lyric/lrcparser.h"
#include "providers/logging.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

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

std::optional<Candidate> LocalProvider::candidateForFile(
    const QString &path, const TrackQuery *sidecarQuery) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    const ParsedLrc parsed = LrcParser::parse(QString::fromUtf8(file.readAll()));
    if (parsed.lines.isEmpty()) return std::nullopt;
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
    searchPrepared(query, cacheVersion(), std::move(callback));
}

void LocalProvider::searchPrepared(const TrackQuery &query,
                                   const QString &preparedCacheVersion,
                                   SearchCallback callback)
{
    if (!m_indexLoaded || m_indexVersion != preparedCacheVersion) {
        refreshIndex();
    }
    const QString sidecar = sidecarPath(query.mediaSrc);
    if (!sidecar.isEmpty()) {
        if (const auto candidate = candidateForFile(sidecar, &query)) {
            // The directory revision does not identify this adjacent file. Its
            // contents can be repaired in place without changing cacheVersion().
            callback({{*candidate}, {}, false, m_indexVersion, false});
            return;
        }
    }
    auto candidates = candidatesForQuery(query);
    // A local-file request without a sidecar cannot use a provider-wide miss:
    // adding or repairing that sidecar later would not change the lyrics-directory version.
    callback({std::move(candidates), {}, false, m_indexVersion, query.mediaSrc.isEmpty()
                  || !QUrl(query.mediaSrc).isLocalFile()});
}

void LocalProvider::fetch(const QString &contentId, FetchCallback callback)
{
    QFile file(contentId);
    if (!file.open(QIODevice::ReadOnly)) {
        callback({std::nullopt, file.errorString(), false});
        return;
    }
    int droppedLines = 0;
    auto document = LrcParser::parseBilingual(QString::fromUtf8(file.readAll()), &droppedLines);
    if (droppedLines > 0) {
        qCDebug(lcLocal).noquote() << QStringLiteral("bilingual pairing: path=%1 collapsed=%2")
            .arg(quoted(contentId)).arg(droppedLines);
    }
    document.metadata.insert(QStringLiteral("source"), QStringLiteral("local"));
    document.metadata.insert(QStringLiteral("path"), QFileInfo(contentId).absoluteFilePath());
    callback({std::move(document), {}, false});
}

QString LocalProvider::cacheVersion() const
{
    refreshIndex();
    return m_indexVersion;
}

QString LocalProvider::knownCacheVersion() const
{
    return m_indexLoaded ? m_indexVersion : cacheVersion();
}

QFileInfoList LocalProvider::lyricFiles() const
{
    QFileInfoList files;
    QDirIterator iterator(m_lyricsDirectory,
                          {QStringLiteral("*.lrc"), QStringLiteral("*.LRC")},
                          QDir::Files | QDir::Readable,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        files.append(iterator.fileInfo());
    }
    const QDir root(m_lyricsDirectory);
    std::sort(files.begin(), files.end(), [&root](const QFileInfo &left,
                                                  const QFileInfo &right) {
        const QString leftPath = root.relativeFilePath(left.absoluteFilePath());
        const QString rightPath = root.relativeFilePath(right.absoluteFilePath());
        const int folded = leftPath.compare(rightPath, Qt::CaseInsensitive);
        return folded == 0 ? leftPath < rightPath : folded < 0;
    });
    return files;
}

QString LocalProvider::versionForFiles(const QFileInfoList &files) const
{
    constexpr QByteArrayView separator("\0", 1);
    const QDir directory(m_lyricsDirectory);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(directory.absolutePath().toUtf8());
    hash.addData(separator);
    hash.addData(QByteArray::number(files.size()));
    hash.addData(separator);
    for (const auto &file : files) {
        hash.addData(QDir::fromNativeSeparators(
                         directory.relativeFilePath(file.absoluteFilePath())).toUtf8());
        hash.addData(separator);
        hash.addData(QByteArray::number(file.size()));
        hash.addData(separator);
        hash.addData(QByteArray::number(file.lastModified().toMSecsSinceEpoch()));
        hash.addData(separator);
    }
    return QStringLiteral("local:") + QString::fromLatin1(hash.result().toHex());
}

void LocalProvider::refreshIndex() const
{
    // Started before the directory walk and version hash below so a slow
    // scan (the usual complaint when "the local source is slow") shows up
    // in elapsed, not just the candidate-building loop.
    QElapsedTimer timer;
    timer.start();
    const QFileInfoList files = lyricFiles();
    const QString version = versionForFiles(files);
    if (m_indexLoaded && version == m_indexVersion) return;

    QList<Candidate> candidates;
    candidates.reserve(files.size());
    for (const auto &file : files) {
        if (auto candidate = candidateForFile(file.absoluteFilePath())) {
            candidates.append(std::move(*candidate));
        }
    }
    m_indexCandidates = std::move(candidates);
    m_indexVersion = version;
    m_indexLoaded = true;
    qCDebug(lcLocal).noquote() << QStringLiteral("index rebuilt files=%1 elapsed=%2ms dir=%3")
        .arg(files.size()).arg(timer.elapsed()).arg(quoted(m_lyricsDirectory));
}

QList<Candidate> LocalProvider::candidatesForQuery(const TrackQuery &query) const
{
    constexpr qsizetype maximumCandidates = 50;
    const auto ranked = rankCandidates(query, m_indexCandidates);
    QList<Candidate> result;
    result.reserve(std::min(maximumCandidates, ranked.size()));
    for (const auto &candidate : ranked) {
        result.append(candidate.candidate);
        if (result.size() == maximumCandidates) break;
    }
    return result;
}

} // namespace PlasmaLyrics
