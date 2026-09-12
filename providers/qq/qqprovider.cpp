#include "qqprovider.h"

#include "core/log/logformat.h"
#include "core/lyric/lrcparser.h"
#include "core/lyric/qrcparser.h"
#include "providers/logging.h"
#include "providers/qq/qrccipher.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>

namespace PlasmaLyrics {
namespace {

const QString &searchModule()
{
    static const QString value = QStringLiteral("music.search.SearchCgiService");
    return value;
}

const QString &searchMethod()
{
    // The desktop method is rejected no matter what else the request carries.
    static const QString value = QStringLiteral("DoSearchForQQMusicMobile");
    return value;
}

QString searchKey()
{
    return searchModule() + QLatin1Char('.') + searchMethod();
}

// The server checks that QIMEI36 is present and non-empty and never looks at
// its value: "x", "abc" and thirty-six zeroes all work, an empty string and a
// missing key both fail. So this is a placeholder, not an identity, and the
// device registration that would produce a real one is deliberately not
// implemented -- it would buy nothing.
const QString &qimeiPlaceholder()
{
    static const QString value = QString(36, QLatin1Char('0'));
    return value;
}

// Pinned client version. If Tencent ever retires it the search starts
// answering with a rejection rather than an error, which is why
// SearchStatus tells rejection apart from an empty result.
constexpr int kClientVersion = 13020508;
constexpr int kClientType = 11;
const QString &userAgent()
{
    static const QString value = QStringLiteral("QQMusic 14.8.0.8");
    return value;
}

// meta.is_filter values. 0 accepts; every negative is an infrastructure
// fault wearing an empty result's clothes.
constexpr int kFilterAccepted = 0;
constexpr int kFilterTransient = -12;

constexpr int kMaximumSearchAttempts = 3;
// Transport-level retries, counted independently of the search-level ones
// above: a timeout or a dropped connection is retried here, a throttle
// rejection is retried there. They compose multiplicatively, so state the
// worst case rather than leaving it to be multiplied out --
// kMaximumSearchAttempts * kMaximumTransportAttempts = 9 requests, plus the
// two throttle backoffs between the search attempts (4 s each by default,
// see the constructor), before a search finally gives up. That counts the
// provider's own attempts; on the lyric path Qt adds one transparent retry
// of its own, because fetch is a GET and Qt re-sends an idempotent request
// once when the connection closes unanswered -- so a fetch that keeps losing
// its connection puts 4 requests on the wire, not 3. Measured, not assumed:
// the POST search produces exactly 3 under the same conditions.
constexpr int kMaximumTransportAttempts = 3;

// QQ returns this single line in place of lyrics for a track it holds no
// words for. Left alone it would render as the song's one lyric line.
bool isInstrumentalPlaceholder(const LyricLines &lines)
{
    return lines.size() == 1 && lines.first().text.contains(QStringLiteral("纯音乐"));
}

bool isHexDigit(QChar character)
{
    return (character >= u'0' && character <= u'9') || (character >= u'a' && character <= u'f')
        || (character >= u'A' && character <= u'F');
}

QString extractCdata(const QString &payload, const QString &tag)
{
    // Not an XML parser: the whole response is wrapped in an XML comment, so
    // a conforming parser sees no elements at all.
    const QRegularExpression expression(
        QStringLiteral(R"(<%1\b[^>]*><!\[CDATA\[(.*?)\]\]></%1>)").arg(tag),
        QRegularExpression::DotMatchesEverythingOption);
    const auto match = expression.match(payload);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

} // namespace

// The transient rejection is sporadic and recovers on retry. It appeared on
// 3 of 12 searches spaced about 3 s apart and on none of 16 spaced 4 s or
// more, but those measurements are non-monotonic (0.5 s gave 12%, 3 s gave
// 25%) and not significant, so this delay is the most conservative observed
// spacing rather than a rate model -- there is no evidence for one.
QqProvider::QqProvider(QUrl searchBaseUrl, QUrl lyricBaseUrl, int timeoutMs,
                       int transientRetryDelayMs)
    : m_searchBaseUrl(std::move(searchBaseUrl))
    , m_lyricBaseUrl(std::move(lyricBaseUrl))
    , m_timeoutMs(timeoutMs)
    , m_transientRetryDelayMs(transientRetryDelayMs)
{
}

QqProvider::~QqProvider()
{
    const auto replies = m_replies;
    for (auto *reply : replies) {
        QObject::disconnect(reply, nullptr, nullptr, nullptr);
        reply->abort();
    }
}

QString QqProvider::id() const
{
    return QStringLiteral("qq");
}

bool QqProvider::isConfigured() const
{
    return m_searchBaseUrl.isValid() && !m_searchBaseUrl.isEmpty() && m_lyricBaseUrl.isValid()
        && !m_lyricBaseUrl.isEmpty();
}

QString QqProvider::cacheVersion() const
{
    return id() + QLatin1Char(':') + m_searchBaseUrl.toString(QUrl::FullyEncoded) + QLatin1Char('|')
        + m_lyricBaseUrl.toString(QUrl::FullyEncoded);
}

int QqProvider::timeoutForAttempt(int baseTimeoutMs, int attempt)
{
    if (attempt <= 1) {
        return baseTimeoutMs;
    }
    if (attempt == 2) {
        return baseTimeoutMs * 3 / 2;
    }
    return baseTimeoutMs * 2;
}

QString QqProvider::generateSearchId()
{
    // Not a free-form nonce: the value is three packed fields, and the server
    // rejects the request outright (is_filter -9) when the top one is zero.
    // A plain counter or timestamp lands in that hole. The layout is
    // <1..20> * 2^54 + <random 22 bits> * 2^32 + <milliseconds into the day>.
    const quint64 leading = QRandomGenerator::global()->bounded(1, 21);
    const quint64 middle = QRandomGenerator::global()->bounded(0, 4194304);
    const quint64 withinDay = static_cast<quint64>(QTime::currentTime().msecsSinceStartOfDay());
    return QString::number(leading * Q_UINT64_C(18014398509481984)
                           + middle * Q_UINT64_C(4294967296) + withinDay);
}

QByteArray QqProvider::buildSearchBody(const QString &keywords, const QString &searchId, int limit)
{
    // Every field of comm is required together with the mobile method: drop
    // the block, or drop QIMEI36 alone, and the server answers with an empty
    // result rather than an error.
    const QJsonObject comm{{QStringLiteral("cv"), kClientVersion},
                           {QStringLiteral("ct"), QString::number(kClientType)},
                           {QStringLiteral("v"), kClientVersion},
                           {QStringLiteral("uid"), QStringLiteral("3931641530")},
                           {QStringLiteral("QIMEI36"), qimeiPlaceholder()},
                           {QStringLiteral("tmeAppID"), QStringLiteral("qqmusic")},
                           {QStringLiteral("format"), QStringLiteral("json")},
                           {QStringLiteral("inCharset"), QStringLiteral("utf-8")},
                           {QStringLiteral("outCharset"), QStringLiteral("utf-8")}};
    const QJsonObject parameters{{QStringLiteral("searchid"), searchId},
                                 {QStringLiteral("query"), keywords},
                                 {QStringLiteral("search_type"), 0},
                                 {QStringLiteral("num_per_page"), limit},
                                 {QStringLiteral("page_num"), 1},
                                 {QStringLiteral("highlight"), 1},
                                 {QStringLiteral("grp"), 1}};
    const QJsonObject request{{QStringLiteral("module"), searchModule()},
                              {QStringLiteral("method"), searchMethod()},
                              {QStringLiteral("param"), parameters}};
    const QJsonObject root{{QStringLiteral("comm"), comm}, {searchKey(), request}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QqProvider::SearchResponse QqProvider::parseSearchResponse(const QByteArray &payload)
{
    SearchResponse response;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        response.error = parseError.errorString();
        return response;
    }
    const auto envelope = document.object().value(searchKey()).toObject();
    if (envelope.isEmpty()) {
        response.error = QStringLiteral("response carries no %1 section").arg(searchKey());
        return response;
    }
    response.requestCode = envelope.value(QStringLiteral("code")).toInt();
    const auto data = envelope.value(QStringLiteral("data")).toObject();
    const auto meta = data.value(QStringLiteral("meta")).toObject();
    if (!meta.contains(QStringLiteral("is_filter"))) {
        response.error = QStringLiteral("response carries no meta.is_filter");
        return response;
    }
    response.isFilter = meta.value(QStringLiteral("is_filter")).toInt();
    // sum, never estimate_sum: the latter is uninitialised on a rejected
    // response (139924516684792 was observed) and meaningless on an accepted
    // one.
    response.total = meta.value(QStringLiteral("sum")).toInteger();
    // Branch on is_filter and never on req_code. req_code 2001 has two
    // unrelated causes -- a deterministic method/comm mismatch and a
    // transient throttle -- and only is_filter separates them. Retrying on
    // the code alone would retry a permanent misconfiguration forever, which
    // surfaces to the user as "lyrics are slow, then absent" rather than as
    // a fault anyone can find. req_code is kept for the log line only.
    if (response.isFilter == kFilterTransient) {
        response.status = SearchStatus::Transient;
        return response;
    }
    if (response.isFilter != kFilterAccepted) {
        response.status = SearchStatus::ShapeRejected;
        return response;
    }
    response.status = SearchStatus::Accepted;
    // title and singer[].name are the plain fields. The highlighted copies
    // live in search_title and singer[].title, which carry <em> markup and
    // are deliberately not read.
    for (const auto &value : data.value(QStringLiteral("body")).toObject()
                                 .value(QStringLiteral("item_song")).toArray()) {
        const auto song = value.toObject();
        const qint64 songId = song.value(QStringLiteral("id")).toInteger();
        if (songId <= 0) {
            continue;
        }
        QStringList artists;
        for (const auto &singer : song.value(QStringLiteral("singer")).toArray()) {
            const QString name = singer.toObject().value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) {
                artists.append(name);
            }
        }
        QString title = song.value(QStringLiteral("title")).toString();
        if (title.isEmpty()) {
            title = song.value(QStringLiteral("name")).toString();
        }
        Candidate candidate;
        // The numeric id, not mid: the lyric endpoint answers musicid="0" for
        // a songmid.
        candidate.trackId = QString::number(songId);
        candidate.title = title;
        candidate.artists = artists;
        candidate.album = song.value(QStringLiteral("album")).toObject()
                              .value(QStringLiteral("name")).toString();
        // interval is whole seconds.
        candidate.lengthMs = song.value(QStringLiteral("interval")).toInteger() * 1000;
        response.candidates.append(std::move(candidate));
    }
    return response;
}

std::optional<LyricDocument> QqProvider::parseLyricResponse(const QByteArray &payload, QString *error)
{
    const QString text = QString::fromUtf8(payload);
    static const QRegularExpression lyricElement(QStringLiteral(R"(<lyric\b[^>]*>)"));
    static const QRegularExpression scrollAttribute(QStringLiteral(R"re(\bscroll="(\d+)")re"));
    static const QRegularExpression reasonElement(QStringLiteral(R"(<reason>(.*?)</reason>)"));
    const auto lyricMatch = lyricElement.match(text);
    if (!lyricMatch.hasMatch()) {
        const auto reasonMatch = reasonElement.match(text);
        if (error) {
            *error = reasonMatch.hasMatch()
                ? QStringLiteral("lyric request failed: %1").arg(reasonMatch.captured(1))
                : QStringLiteral("response carries no lyric element");
        }
        return std::nullopt;
    }
    const auto scrollMatch = scrollAttribute.match(lyricMatch.captured(0));
    const QString content = extractCdata(text, QStringLiteral("content"));
    const QString translation = extractCdata(text, QStringLiteral("contentts"));
    if (content.isEmpty()) {
        // A known state rather than a failure: the track exists and simply
        // carries no words.
        return LyricDocument{};
    }
    // Plain LRC, not hex. This is what the word-level path degrades to, and
    // it is the common case rather than an error.
    const auto lineLevelDocument = [&content, &translation] {
        auto document = LrcParser::merge(content, translation);
        if (isInstrumentalPlaceholder(document.lines)) {
            return LyricDocument{};
        }
        return document;
    };
    // The body decides, in both directions. scroll predicted QRC correctly on
    // all 22 measured samples and is worth having -- but it is a prediction,
    // and either way of being wrong would cost the whole song, which
    // §C2.3.10 forbids: scroll saying QRC over a plain body would fail to
    // decrypt, and scroll saying plain over a hex body would parse as zero
    // LRC lines and lose it silently, with no error at all. Deciding from
    // the payload is just as cheap -- the scan costs less than the decrypt it
    // guards -- so scroll is kept only to notice and log a disagreement.
    //
    // "Hex or not" does separate them, but the margin is thinner than that
    // sounds, which is why this tests the whole string rather than sniffing a
    // prefix: the census holds a plain-text body (毛茸茸大决战！, 223
    // characters) whose first 193 characters are all hex digits -- Python's
    // fromhex reported the first non-hex one at position 193 -- leaving 30
    // characters of margin. A body that was hex the whole way would read as
    // encrypted and yield a silent empty document. That is unchanged from
    // before rather than a regression, and it is the one soft spot left here;
    // it is also close to unconstructible, since any real LRC line carries
    // '[', ':' or '.'.
    //
    // Order matters: std::all_of over an empty range is true, which would
    // route an empty body straight into the decrypt. The content.isEmpty()
    // check above is what makes that unreachable, so the two must not be
    // reordered.
    const bool encrypted = std::all_of(content.cbegin(), content.cend(),
                                       [](QChar character) { return isHexDigit(character); });
    if (scrollMatch.hasMatch() && (scrollMatch.capturedView(1).toInt() != 0) != encrypted) {
        // Debug rather than warning: whichever way it disagrees, the outcome
        // is a usable document, and degrading to line-level lyrics is the
        // documented normal case however it is arrived at.
        qCDebug(lcQq).noquote()
            << QStringLiteral("scroll=%1 disagrees with the body, which is %2; trusting the body")
                   .arg(scrollMatch.capturedView(1),
                        encrypted ? QStringLiteral("hex") : QStringLiteral("plain text"));
    }
    if (!encrypted) {
        return lineLevelDocument();
    }
    QString decryptError;
    const auto decrypted = QrcCipher::decryptHex(content.toLatin1(), &decryptError);
    if (!decrypted) {
        // The body is hex and still failed: a corrupt or truncated payload,
        // not a misprediction, and there is no line-level text to fall back
        // to.
        if (error) {
            *error = QStringLiteral("could not decrypt the QRC payload: %1").arg(decryptError);
        }
        return std::nullopt;
    }
    QString romanization;
    const QString encryptedRomanization = extractCdata(text, QStringLiteral("contentroma"));
    if (!encryptedRomanization.isEmpty()) {
        // Romanization is optional and its absence is unremarkable, so a
        // failure here loses the romanization rather than the song.
        if (const auto value = QrcCipher::decryptHex(encryptedRomanization.toLatin1())) {
            romanization = *value;
        }
    }
    auto document = QrcParser::assemble(decrypted.value(), romanization, translation);
    if (document.lines.isEmpty()) {
        return LyricDocument{};
    }
    return document;
}

void QqProvider::send(const QNetworkRequest &request, const QByteArray &body,
                      TransferCallback callback)
{
    sendAttempt(request, body, 1, std::move(callback));
}

void QqProvider::sendAttempt(const QNetworkRequest &request, const QByteArray &body, int attempt,
                             TransferCallback callback)
{
    auto *reply = body.isNull() ? m_network.get(request) : m_network.post(request, body);
    m_replies.insert(reply);
    QElapsedTimer timer;
    timer.start();
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    const auto timedOut = std::make_shared<bool>(false);
    QObject::connect(timeout, &QTimer::timeout, reply, [reply, timedOut] {
        *timedOut = true;
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [this, request, body, attempt, callback = std::move(callback), reply, timeout,
                      timedOut, timer]() mutable {
        timeout->stop();
        const auto error = reply->error();
        const auto payload = reply->readAll();
        const auto httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int httpStatusCode = httpStatus.toInt();
        // Matches the netease provider: a reply that reached 2xx and still
        // failed did so in transport (a truncated body, say) and is worth
        // retrying; a non-2xx answer is the server's considered response.
        const bool transportFailed = !httpStatus.isValid()
            || (httpStatusCode >= 200 && httpStatusCode < 300);
        const qint64 elapsedMs = timer.elapsed();
        const QString statusText = httpStatus.isValid() ? QString::number(httpStatusCode)
                                                        : QStringLiteral("-");
        const QString errorText = *timedOut
            ? QStringLiteral("request timed out after %1 ms").arg(timeoutForAttempt(m_timeoutMs, attempt))
            : reply->errorString();
        const QString method = body.isNull() ? QStringLiteral("GET") : QStringLiteral("POST");
        if (error == QNetworkReply::NoError) {
            qCDebug(lcQq).noquote()
                << QStringLiteral("http %1 %2 status=%3 bytes=%4 elapsed=%5ms attempt=%6/%7")
                       .arg(method, request.url().toString(QUrl::EncodeSpaces), statusText)
                       .arg(payload.size()).arg(elapsedMs).arg(attempt)
                       .arg(kMaximumTransportAttempts);
        } else {
            qCDebug(lcQq).noquote()
                << QStringLiteral("http %1 %2 status=%3 bytes=%4 elapsed=%5ms attempt=%6/%7 error=%8")
                       .arg(method, request.url().toString(QUrl::EncodeSpaces), statusText)
                       .arg(payload.size()).arg(elapsedMs).arg(attempt)
                       .arg(kMaximumTransportAttempts).arg(quoted(errorText));
        }
        m_replies.remove(reply);
        reply->deleteLater();
        if (error == QNetworkReply::NoError) {
            callback(payload, {}, false);
            return;
        }
        if (transportFailed && attempt < kMaximumTransportAttempts) {
            qCInfo(lcQq).noquote()
                << QStringLiteral("retry %1/%2 status=%3 elapsed=%4ms error=%5")
                       .arg(attempt + 1).arg(kMaximumTransportAttempts).arg(statusText)
                       .arg(elapsedMs).arg(quoted(errorText));
            sendAttempt(request, body, attempt + 1, std::move(callback));
            return;
        }
        callback(std::nullopt, errorText, transportFailed);
    });
    timeout->start(timeoutForAttempt(m_timeoutMs, attempt));
}

void QqProvider::search(const TrackQuery &query, SearchCallback callback)
{
    searchAttempt(query, 1, std::move(callback));
}

void QqProvider::searchAttempt(const TrackQuery &query, int attempt, SearchCallback callback)
{
    QUrl url = m_searchBaseUrl.resolved(QUrl(QStringLiteral("/cgi-bin/musicu.fcg")));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setRawHeader("Referer", "https://y.qq.com/");
    const QByteArray body = buildSearchBody(searchKeywords(query), generateSearchId());
    send(request, body, [this, query, attempt, callback = std::move(callback)](
                            std::optional<QByteArray> payload, QString error,
                            bool transportFailed) mutable {
        if (!payload) {
            callback({.candidates = {}, .error = std::move(error), .transportFailed = transportFailed, .cacheVersion = QString()});
            return;
        }
        const auto response = parseSearchResponse(*payload);
        switch (response.status) {
        case SearchStatus::Accepted:
            qCDebug(lcQq).noquote()
                << QStringLiteral("search accepted: candidates=%1 total=%2")
                       .arg(response.candidates.size()).arg(response.total);
            callback({.candidates = response.candidates, .error = {}, .transportFailed = false, .cacheVersion = QString()});
            return;
        case SearchStatus::Transient:
            if (attempt < kMaximumSearchAttempts) {
                qCInfo(lcQq).noquote()
                    << QStringLiteral("search throttled: req_code=%1 is_filter=%2 retry=%3/%4 in %5ms")
                           .arg(response.requestCode).arg(response.isFilter).arg(attempt + 1)
                           .arg(kMaximumSearchAttempts).arg(m_transientRetryDelayMs);
                QTimer::singleShot(m_transientRetryDelayMs, &m_network,
                                   [this, query, attempt, callback = std::move(callback)]() mutable {
                    searchAttempt(query, attempt + 1, std::move(callback));
                });
                return;
            }
            qCWarning(lcQq).noquote()
                << QStringLiteral("search still throttled after %1 attempts: req_code=%2 is_filter=%3")
                       .arg(kMaximumSearchAttempts).arg(response.requestCode).arg(response.isFilter);
            callback({.candidates = {},
                      .error = QStringLiteral("search was throttled (is_filter=%1)").arg(response.isFilter),
                      .transportFailed = false,
                      .cacheVersion = QString()});
            return;
        case SearchStatus::ShapeRejected:
            // Louder than an empty result on purpose. This is what a retired
            // client version looks like, and it answers with the same zero
            // count a genuinely unknown song does.
            qCWarning(lcQq).noquote()
                << QStringLiteral("search request form was rejected: is_filter=%1 req_code=%2 -- "
                                  "the pinned client version may have been retired")
                       .arg(response.isFilter).arg(response.requestCode);
            callback({.candidates = {},
                      .error = QStringLiteral("search request form was rejected (is_filter=%1)")
                              .arg(response.isFilter),
                      .transportFailed = false,
                      .cacheVersion = QString()});
            return;
        case SearchStatus::Malformed:
            qCWarning(lcQq).noquote()
                << QStringLiteral("search response could not be read: %1").arg(quoted(response.error));
            callback({.candidates = {}, .error = response.error, .transportFailed = false, .cacheVersion = QString()});
            return;
        }
    });
}

void QqProvider::fetch(const QString &trackId, FetchCallback callback)
{
    QUrl url = m_lyricBaseUrl.resolved(QUrl(QStringLiteral("/qqmusic/fcgi-bin/lyric_download.fcg")));
    QUrlQuery parameters;
    // version/miniversion are the client revision this endpoint expects;
    // lrctype=4 is what returns the encrypted QRC alongside the translation
    // and romanization.
    parameters.addQueryItem(QStringLiteral("version"), QStringLiteral("15"));
    parameters.addQueryItem(QStringLiteral("miniversion"), QStringLiteral("82"));
    parameters.addQueryItem(QStringLiteral("lrctype"), QStringLiteral("4"));
    parameters.addQueryItem(QStringLiteral("musicid"), trackId);
    url.setQuery(parameters);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setRawHeader("Referer", "https://y.qq.com/portal/player.html");
    send(request, {}, [callback = std::move(callback)](std::optional<QByteArray> payload,
                                                       QString error, bool transportFailed) mutable {
        if (!payload) {
            callback({std::nullopt, std::move(error), transportFailed});
            return;
        }
        QString parseError;
        auto document = parseLyricResponse(*payload, &parseError);
        if (document && !document->hasWords && !document->lines.isEmpty()) {
            // Routine, not a fault: most tracks carry no QRC at all.
            qCDebug(lcQq) << "no word-level QRC for this track; using the line-level lyrics";
        }
        callback({std::move(document), std::move(parseError), false});
    });
}

} // namespace PlasmaLyrics
