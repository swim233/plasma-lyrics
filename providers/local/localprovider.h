#pragma once

#include "providers/provider.h"

namespace PlasmaLyrics {

class LocalProvider final : public Provider
{
public:
    explicit LocalProvider(QString lyricsDirectory = {});

    QString id() const override;
    bool isConfigured() const override;
    QString cacheVersion() const override;
    void search(const TrackQuery &query, SearchCallback callback) override;
    void fetch(const QString &trackId, FetchCallback callback) override;

    QString lyricsDirectory() const;
    static QString defaultLyricsDirectory();

private:
    Candidate candidateForFile(const QString &path, const TrackQuery *sidecarQuery = nullptr) const;
    QString sidecarPath(const QString &mediaSrc) const;

    QString m_lyricsDirectory;
};

} // namespace PlasmaLyrics
