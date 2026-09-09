#include "neteaseprovider.h"

#include "core/lyric/lrcparser.h"
#include "core/lyric/timeline.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
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

NeteaseProvider::~NeteaseProvider()
{
    const auto replies = m_replies;
    for (auto *reply : replies) {
        QObject::disconnect(reply, nullptr, nullptr, nullptr);
        reply->abort();
    }
}

QString NeteaseProvider::id() const
{
    return QStringLiteral("netease");
}

bool NeteaseProvider::isConfigured() const
{
    return m_baseUrl.isValid() && !m_baseUrl.isEmpty();
}

QString NeteaseProvider::cacheVersion() const
{
    return id() + QLatin1Char(':') + m_baseUrl.toString(QUrl::FullyEncoded);
}

int NeteaseProvider::timeoutForAttempt(int baseTimeoutMs, int attempt)
{
    if (attempt <= 1) {
        return baseTimeoutMs;
    }
    if (attempt == 2) {
        return baseTimeoutMs * 3 / 2;
    }
    return baseTimeoutMs * 2;
}

void NeteaseProvider::get(const QUrl &url, GetCallback callback)
{
    getAttempt(url, 1, std::move(callback));
}

void NeteaseProvider::getAttempt(const QUrl &url, int attempt, GetCallback callback)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("plasma-lyricsd/0.1"));
    request.setRawHeader("Referer", "https://music.163.com/");
    auto *reply = m_network.get(request);
    m_replies.insert(reply);
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    const auto timedOut = std::make_shared<bool>(false);
    QObject::connect(timeout, &QTimer::timeout, reply, [reply, timedOut] {
        *timedOut = true;
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [this, url, attempt, callback = std::move(callback), reply, timeout, timedOut]() mutable {
        timeout->stop();
        const auto error = reply->error();
        const auto payload = reply->readAll();
        const auto httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int httpStatusCode = httpStatus.toInt();
        const bool transportFailed = !httpStatus.isValid()
            || (httpStatusCode >= 200 && httpStatusCode < 300);
        const QString errorText = *timedOut
            ? QStringLiteral("request timed out after %1 ms").arg(timeoutForAttempt(m_timeoutMs, attempt))
            : reply->errorString();
        m_replies.remove(reply);
        reply->deleteLater();
        if (error == QNetworkReply::NoError) {
            callback(payload, {}, false);
            return;
        }
        // A response can have received its 2xx headers and still fail at the
        // transport layer (for example, a truncated body reports
        // RemoteHostClosedError).  That remains retryable.  Conversely,
        // QNetworkReply also represents non-success HTTP responses as
        // NetworkError values; those are server answers, not transient
        // transport failures.
        if (transportFailed && attempt < 3) {
            qInfo().noquote() << QStringLiteral("retry %1/3 after %2").arg(attempt + 1).arg(errorText);
            getAttempt(url, attempt + 1, std::move(callback));
            return;
        }
        callback(std::nullopt, errorText, transportFailed);
    });
    timeout->start(timeoutForAttempt(m_timeoutMs, attempt));
}

void NeteaseProvider::search(const TrackQuery &query, SearchCallback callback)
{
    QUrl url = m_baseUrl.resolved(QUrl(QStringLiteral("/api/search/get")));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem(QStringLiteral("s"), searchKeywords(query));
    urlQuery.addQueryItem(QStringLiteral("type"), QStringLiteral("1"));
    urlQuery.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    url.setQuery(urlQuery);
    get(url, [callback = std::move(callback)](std::optional<QByteArray> payload, QString error,
                                               bool transportFailed) mutable {
        if (!payload) {
            callback({{}, std::move(error), transportFailed});
            return;
        }
        QString parseError;
        auto candidates = parseSearchResponse(*payload, &parseError);
        callback({std::move(candidates), std::move(parseError), false});
    });
}

void NeteaseProvider::fetch(const QString &trackId, FetchCallback callback)
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
    get(url, [callback = std::move(callback)](std::optional<QByteArray> payload, QString error,
                                               bool transportFailed) mutable {
        if (!payload) {
            callback({std::nullopt, std::move(error), transportFailed});
            return;
        }
        QString parseError;
        auto document = parseLyricResponse(*payload, &parseError);
        callback({std::move(document), std::move(parseError), false});
    });
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
        // transNames carries the storefront's localized title (e.g. "偶像" for
        // "アイドル"); alias is deliberately not read here -- it's anime/show
        // tie-in text (e.g. "TV动画《我推的孩子》片头曲"), not a title translation.
        QStringList alternateTitles;
        for (const auto &alternate : song.value(QStringLiteral("transNames")).toArray()) {
            const auto text = alternate.toString();
            if (!text.isEmpty()) {
                alternateTitles.append(text);
            }
        }
        candidates.append({QString::number(song.value(QStringLiteral("id")).toInteger()),
                           song.value(QStringLiteral("name")).toString(),
                           artists,
                           albumObject.value(QStringLiteral("name")).toString(),
                           song.contains(QStringLiteral("duration"))
                               ? song.value(QStringLiteral("duration")).toInteger()
                               : song.value(QStringLiteral("dt")).toInteger(),
                           alternateTitles});
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
