#pragma once

#include "providers/provider.h"

#include <QFileInfoList>

namespace PlasmaLyrics {

class LocalProvider final : public Provider
{
public:
    explicit LocalProvider(QString lyricsDirectory = {});

    QString id() const override;
    bool isConfigured() const override;
    QString cacheVersion() const override;
    QString knownCacheVersion() const override;
    void search(const TrackQuery &query, SearchCallback callback) override;
    void searchPrepared(const TrackQuery &query, const QString &cacheVersion,
                        SearchCallback callback) override;
    void fetch(const QString &trackId, FetchCallback callback) override;

    QString lyricsDirectory() const;
    static QString defaultLyricsDirectory();

private:
    std::optional<Candidate> candidateForFile(
        const QString &path, const TrackQuery *sidecarQuery = nullptr) const;
    QFileInfoList lyricFiles() const;
    QString versionForFiles(const QFileInfoList &files) const;
    void refreshIndex() const;
    QList<Candidate> candidatesForQuery(const TrackQuery &query) const;
    QString sidecarPath(const QString &mediaSrc) const;

    QString m_lyricsDirectory;
    mutable QList<Candidate> m_indexCandidates;
    mutable QString m_indexVersion;
    mutable bool m_indexLoaded = false;
};

} // namespace PlasmaLyrics
