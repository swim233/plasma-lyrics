#pragma once

#include "providers/provider.h"

#include <QNetworkAccessManager>
#include <QSet>
#include <QUrl>
#include <functional>

namespace PlasmaLyrics {

/// QQ Music. A full source in its own right -- its own track ids, its own
/// per-track offsets, its own place in the fallback chain -- not a word-level
/// top-up for another provider.
class QqProvider final : public Provider
{
public:
    /// transientRetryDelayMs is a constructor argument only so a test can
    /// drive the retry without waiting out the real backoff; nothing
    /// configures it.
    explicit QqProvider(QUrl searchBaseUrl = QUrl(QStringLiteral("https://u.y.qq.com")),
                        QUrl lyricBaseUrl = QUrl(QStringLiteral("https://c.y.qq.com")),
                        int timeoutMs = 4000, int transientRetryDelayMs = 4000);
    ~QqProvider() override;

    QString id() const override;
    bool isConfigured() const override;
    QString cacheVersion() const override;
    void search(const TrackQuery &query, SearchCallback callback) override;
    void fetch(const QString &trackId, FetchCallback callback) override;

    /// Why a search response could not be turned into results. The server
    /// reports "your request was rejected" and "this query has no matches"
    /// with the same zero result count, so the two are told apart by
    /// meta.is_filter -- conflating them would make a version cutoff
    /// indistinguishable from an unpopular song.
    enum class SearchStatus {
        Accepted,       ///< is_filter == 0. A zero count here really is zero matches.
        Transient,      ///< is_filter == -12 / req_code 2001. Clears on retry.
        ShapeRejected,  ///< Any other negative is_filter: the request form is no longer accepted.
        Malformed,      ///< Not JSON, or none of the expected envelope.
    };

    struct SearchResponse {
        SearchStatus status = SearchStatus::Malformed;
        QList<Candidate> candidates;
        qint64 total = 0;
        int isFilter = 0;
        int requestCode = 0;
        QString error;
    };

    /// Builds the search request body. The shape is load-bearing and was
    /// established by measurement: the mobile method, the full mobile comm
    /// block, and a present, non-empty QIMEI36. Exposed, and taking its
    /// searchId rather than generating one, so a test can reproduce the bytes.
    static QByteArray buildSearchBody(const QString &keywords, const QString &searchId,
                                      int limit = 20);
    /// The server rejects a searchId whose top field is out of range, so this
    /// is not a free-form nonce. See the comment on its implementation.
    static QString generateSearchId();
    static SearchResponse parseSearchResponse(const QByteArray &payload);
    static std::optional<LyricDocument> parseLyricResponse(const QByteArray &payload,
                                                           QString *error = nullptr);
    static int timeoutForAttempt(int baseTimeoutMs, int attempt);

private:
    using TransferCallback = std::function<void(std::optional<QByteArray>, QString, bool)>;
    void send(const QNetworkRequest &request, const QByteArray &body, TransferCallback callback);
    void sendAttempt(const QNetworkRequest &request, const QByteArray &body, int attempt,
                     TransferCallback callback);
    void searchAttempt(const TrackQuery &query, int attempt, SearchCallback callback);

    QUrl m_searchBaseUrl;
    QUrl m_lyricBaseUrl;
    int m_timeoutMs;
    int m_transientRetryDelayMs;
    QNetworkAccessManager m_network;
    QSet<QNetworkReply *> m_replies;
};

} // namespace PlasmaLyrics
