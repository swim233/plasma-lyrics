#pragma once

#include "providers/provider.h"

#include <QNetworkAccessManager>
#include <QSet>
#include <QUrl>
#include <functional>

namespace PlasmaLyrics {

class AmllProvider final : public Provider
{
public:
    explicit AmllProvider(
        QUrl indexUrl = QUrl(QStringLiteral("https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/metadata/raw-lyrics-index.jsonl")),
        QUrl contentBaseUrl = QUrl(QStringLiteral("https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/")),
        int timeoutMs = 8000, QString cachePath = {}, qint64 cacheMaxAgeSeconds = 24 * 60 * 60);
    ~AmllProvider() override;

    QString id() const override;
    bool isConfigured() const override;
    MatchPolicy matchPolicy() const override;
    QString cacheVersion() const override;
    void search(const TrackQuery &query, SearchCallback callback) override;
    void fetch(const QString &trackId, FetchCallback callback) override;

    static QList<Candidate> parseIndex(const QByteArray &payload, QString *error = nullptr,
                                       int maximumEntries = 50000);
    static QString defaultCachePath();

private:
    struct Download;
    struct PendingSearch {
        TrackQuery query;
        SearchCallback callback;
    };
    using DownloadCallback = std::function<void(Download)>;

    bool loadCachedIndex();
    void refreshIndex();
    void finishRefresh(Download download);
    QList<Candidate> localSearch(const TrackQuery &query) const;
    void completePending(const QString &error = {}, bool transportFailed = false);
    void get(const QUrl &url, bool conditional, qint64 maximumBytes,
             DownloadCallback callback);
    void getAttempt(const QUrl &url, bool conditional, qint64 maximumBytes,
                    int attempt, DownloadCallback callback);
    void loadCacheMetadata();
    void saveCacheMetadata();
    void setIndex(QList<Candidate> candidates, const QByteArray &payload,
                  const QByteArray &etag = {}, const QByteArray &lastModified = {});

    QUrl m_indexUrl;
    QUrl m_contentBaseUrl;
    QString m_normalizedIndexUrl;
    QString m_normalizedContentBaseUrl;
    int m_timeoutMs;
    QString m_cachePath;
    qint64 m_cacheMaxAgeSeconds;
    QNetworkAccessManager m_network;
    QSet<QNetworkReply *> m_replies;
    QList<Candidate> m_candidates;
    QList<PendingSearch> m_pending;
    bool m_cacheAttempted = false;
    bool m_indexLoaded = false;
    bool m_refreshing = false;
    qint64 m_fetchedAt = 0;
    QByteArray m_etag;
    QByteArray m_lastModified;
    QString m_revision;
};

} // namespace PlasmaLyrics
