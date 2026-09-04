#pragma once

#include "providers/provider.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QUrl>

namespace PlasmaLyrics {

class NeteaseProvider final : public Provider
{
public:
    explicit NeteaseProvider(QUrl baseUrl = QUrl(QStringLiteral("https://music.163.com")),
                             int timeoutMs = 8000);

    QString id() const override;
    bool isConfigured() const override;
    QList<Candidate> search(const TrackQuery &query) override;
    std::optional<LyricDocument> fetch(const QString &trackId) override;
    QString lastError() const override;

    static QList<Candidate> parseSearchResponse(const QByteArray &payload, QString *error = nullptr);
    static std::optional<LyricDocument> parseLyricResponse(const QByteArray &payload, QString *error = nullptr);

private:
    std::optional<QByteArray> get(const QUrl &url);

    QUrl m_baseUrl;
    int m_timeoutMs;
    QString m_lastError;
    QNetworkAccessManager m_network;
};

} // namespace PlasmaLyrics

