#include "neteaseprovider.h"

#include "core/lyric/lrcparser.h"
#include "core/lyric/timeline.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace PlasmaLyrics {

NeteaseProvider::NeteaseProvider(QUrl baseUrl, int timeoutMs)
    : m_baseUrl(std::move(baseUrl))
    , m_timeoutMs(timeoutMs)
{
}

QString NeteaseProvider::id() const
{
    return QStringLiteral("netease");
}

bool NeteaseProvider::isConfigured() const
{
    return m_baseUrl.isValid() && !m_baseUrl.isEmpty();
}

QString NeteaseProvider::lastError() const
{
    return m_lastError;
}

std::optional<QByteArray> NeteaseProvider::get(const QUrl &url)
{
    m_lastError.clear();
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("plasma-lyricsd/0.1"));
    request.setRawHeader("Referer", "https://music.163.com/");
    auto *reply = m_network.get(request);
    QTimer timeout;
    timeout.setSingleShot(true);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout.start(m_timeoutMs);
    loop.exec();
    const auto error = reply->error();
    const auto payload = reply->readAll();
    if (error != QNetworkReply::NoError) {
        m_lastError = reply->errorString();
        reply->deleteLater();
        return std::nullopt;
    }
    reply->deleteLater();
    return payload;
}

QList<Candidate> NeteaseProvider::search(const TrackQuery &query)
{
    QUrl url = m_baseUrl.resolved(QUrl(QStringLiteral("/api/search/get")));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("s"), searchKeywords(query));
    urlQuery.addQueryItem(QStringLiteral("type"), QStringLiteral("1"));
    urlQuery.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    url.setQuery(urlQuery);
    const auto payload = get(url);
    if (!payload) {
        return {};
    }
    return parseSearchResponse(*payload, &m_lastError);
}

std::optional<LyricDocument> NeteaseProvider::fetch(const QString &trackId)
{
    // /api/song/lyric/v1 rather than /api/song/lyric: the v1 response returns
    // production credits as structured entries carrying the artist's links,
    // which lets the credit filter identify them without guessing at their
    // shape. The old endpoint flattens them into timestamped lines instead.
    // Word-level fields are not requested because this route never serves them
    // -- see DESIGN.md 1.2 for what was measured.
    QUrl url = m_baseUrl.resolved(QUrl(QStringLiteral("/api/song/lyric/v1")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), trackId);
    query.addQueryItem(QStringLiteral("cp"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("lv"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("tv"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("rv"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("kv"), QStringLiteral("0"));
    url.setQuery(query);
    const auto payload = get(url);
    if (!payload) {
        return std::nullopt;
    }
    return parseLyricResponse(*payload, &m_lastError);
}

QList<Candidate> NeteaseProvider::parseSearchResponse(const QByteArray &payload, QString *error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = parseError.errorString();
        }
        return {};
    }
    QList<Candidate> candidates;
    const auto songs = document.object().value(QStringLiteral("result")).toObject()
                           .value(QStringLiteral("songs")).toArray();
    for (const auto &value : songs) {
        const auto song = value.toObject();
        QStringList artists;
        const auto artistValues = song.contains(QStringLiteral("artists"))
            ? song.value(QStringLiteral("artists")).toArray()
            : song.value(QStringLiteral("ar")).toArray();
        for (const auto &artist : artistValues) {
            artists.append(artist.toObject().value(QStringLiteral("name")).toString());
        }
        const auto albumObject = song.contains(QStringLiteral("album"))
            ? song.value(QStringLiteral("album")).toObject()
            : song.value(QStringLiteral("al")).toObject();
        candidates.append({QString::number(song.value(QStringLiteral("id")).toInteger()),
                           song.value(QStringLiteral("name")).toString(),
                           artists,
                           albumObject.value(QStringLiteral("name")).toString(),
                           song.contains(QStringLiteral("duration"))
                               ? song.value(QStringLiteral("duration")).toInteger()
                               : song.value(QStringLiteral("dt")).toInteger()});
    }
    return candidates;
}

std::optional<LyricDocument> NeteaseProvider::parseLyricResponse(const QByteArray &payload, QString *error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = parseError.errorString();
        }
        return std::nullopt;
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("nolyric")).toBool() || root.value(QStringLiteral("uncollected")).toBool()) {
        return LyricDocument{};
    }
    const auto origin = root.value(QStringLiteral("lrc")).toObject().value(QStringLiteral("lyric")).toString();
    const auto translation = root.value(QStringLiteral("tlyric")).toObject().value(QStringLiteral("lyric")).toString();
    auto lyrics = LrcParser::merge(origin, translation);
    if (origin.isEmpty() && !root.contains(QStringLiteral("lrc"))) {
        if (error) {
            *error = QStringLiteral("response has no lrc field");
        }
        return std::nullopt;
    }
    return lyrics;
}

} // namespace PlasmaLyrics
