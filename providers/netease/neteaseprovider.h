#pragma once

#include "providers/provider.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QSet>
#include <QUrl>
#include <functional>

namespace PlasmaLyrics {

class NeteaseProvider final : public Provider
{
public:
    explicit NeteaseProvider(QUrl baseUrl = QUrl(QStringLiteral("https://music.163.com")),
                             int timeoutMs = 4000);
    ~NeteaseProvider() override;

    QString id() const override;
    bool isConfigured() const override;
    void search(const TrackQuery &query, SearchCallback callback) override;
    void fetch(const QString &trackId, FetchCallback callback) override;

    static QList<Candidate> parseSearchResponse(const QByteArray &payload, QString *error = nullptr);
    static std::optional<LyricDocument> parseLyricResponse(const QByteArray &payload, QString *error = nullptr);
    static int timeoutForAttempt(int baseTimeoutMs, int attempt);

private:
    using GetCallback = std::function<void(std::optional<QByteArray>, QString)>;
    void get(const QUrl &url, GetCallback callback);
    void getAttempt(const QUrl &url, int attempt, GetCallback callback);

    QUrl m_baseUrl;
    int m_timeoutMs;
    QNetworkAccessManager m_network;
    QSet<QNetworkReply *> m_replies;
};

} // namespace PlasmaLyrics
