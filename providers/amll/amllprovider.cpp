#include "amllprovider.h"

#include "core/lyric/ttmlparser.h"
#include "core/match/matcher.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <memory>
#include <utility>

namespace PlasmaLyrics {
namespace {

constexpr qint64 maximumIndexBytes = 8 * 1024 * 1024;
constexpr qint64 maximumTtmlBytes = 2 * 1024 * 1024;

QString normalizedSourceUrl(QUrl url)
{
    url = url.adjusted(QUrl::NormalizePathSegments | QUrl::RemoveFragment);
    url.setScheme(url.scheme().toCaseFolded());
    url.setHost(url.host().toCaseFolded());
    if ((url.scheme() == QStringLiteral("http") && url.port() == 80)
        || (url.scheme() == QStringLiteral("https") && url.port() == 443)) {
        url.setPort(-1);
    }
    QString path = url.path();
    while (path.size() > 1 && path.endsWith(QLatin1Char('/'))) path.chop(1);
    url.setPath(path);
    return url.toString(QUrl::FullyEncoded);
}

QHash<QString, QStringList> metadataMap(const QJsonArray &metadata)
{
    QHash<QString, QStringList> result;
    for (const auto &itemValue : metadata) {
        const auto item = itemValue.toArray();
        if (item.size() != 2 || !item.first().isString() || !item.last().isArray()) continue;
        QStringList values;
        for (const auto &value : item.last().toArray()) {
            const QString text = value.toString().trimmed();
            if (!text.isEmpty() && !values.contains(text)) values.append(text);
        }
        if (!values.isEmpty()) result.insert(item.first().toString(), values);
    }
    return result;
}

QString stableId(const QHash<QString, QStringList> &metadata)
{
    static const QList<QPair<QString, QString>> platformNamespaces{
        {QStringLiteral("ncmMusicId"), QStringLiteral("netease")},
        {QStringLiteral("qqMusicId"), QStringLiteral("qqmusic")},
        {QStringLiteral("appleMusicId"), QStringLiteral("applemusic")},
        {QStringLiteral("spotifyId"), QStringLiteral("spotify")},
    };
    for (const auto &[key, nameSpace] : platformNamespaces) {
        if (!metadata.value(key).isEmpty()) {
            return nameSpace + QLatin1Char(':') + metadata.value(key).first();
        }
    }
    const QString fallback = metadata.value(QStringLiteral("musicName")).value(0)
        + QLatin1Char('\x1f') + metadata.value(QStringLiteral("artists")).join(QLatin1Char('/'))
        + QLatin1Char('\x1f') + metadata.value(QStringLiteral("album")).value(0);
    return QStringLiteral("metadata:")
        + QString::fromLatin1(QCryptographicHash::hash(fallback.toUtf8(), QCryptographicHash::Sha256).toHex());
}

qint64 contentRevision(const QString &fileName)
{
    bool ok = false;
    const qint64 revision = fileName.section(QLatin1Char('-'), 0, 0).toLongLong(&ok);
    return ok ? revision : 0;
}

QStringList metadataValuesCaseInsensitive(const QHash<QString, QStringList> &metadata,
                                          QStringView key)
{
    QStringList result;
    for (auto it = metadata.cbegin(); it != metadata.cend(); ++it) {
        if (it.key().compare(key, Qt::CaseInsensitive) != 0) continue;
        for (const auto &value : it.value()) {
            if (!result.contains(value, Qt::CaseInsensitive)) result.append(value);
        }
    }
    return result;
}

struct IndexRecord {
    Candidate candidate;
    qint64 revision = 0;
    QStringList associations;
    QStringList isrcs;
    QString conflictId;
};

bool hasIntersection(const QStringList &left, const QStringList &right)
{
    return std::any_of(left.cbegin(), left.cend(), [&right](const QString &value) {
        return right.contains(value);
    });
}

bool compatibleIdentity(const IndexRecord &left, const IndexRecord &right)
{
    QStringList leftTitles{normalizeSearchText(left.candidate.title)};
    QStringList rightTitles{normalizeSearchText(right.candidate.title)};
    for (const auto &title : left.candidate.alternateTitles) {
        const QString normalized = normalizeSearchText(title);
        if (!leftTitles.contains(normalized)) leftTitles.append(normalized);
    }
    for (const auto &title : right.candidate.alternateTitles) {
        const QString normalized = normalizeSearchText(title);
        if (!rightTitles.contains(normalized)) rightTitles.append(normalized);
    }
    if (!hasIntersection(leftTitles, rightTitles)
        || !hasIntersection(cleanArtists(left.candidate.artists),
                            cleanArtists(right.candidate.artists))) {
        return false;
    }
    // A reused/conflicting platform id is not enough to merge two known
    // recordings whose ISRCs disagree. Missing ISRC remains unknown rather
    // than becoming evidence against otherwise identical history records.
    return left.isrcs.isEmpty() || right.isrcs.isEmpty()
        || hasIntersection(left.isrcs, right.isrcs);
}

class DisjointSets
{
public:
    explicit DisjointSets(qsizetype size)
    {
        m_parent.reserve(size);
        for (qsizetype index = 0; index < size; ++index) {
            m_parent.append(static_cast<int>(index));
        }
    }

    int root(int item)
    {
        if (m_parent[item] != item) m_parent[item] = root(m_parent[item]);
        return m_parent[item];
    }

    void unite(int left, int right)
    {
        left = root(left);
        right = root(right);
        if (left == right) return;
        // Preserve the oldest record as the group's stable-id owner.
        if (left > right) std::swap(left, right);
        m_parent[right] = left;
    }

private:
    QList<int> m_parent;
};

bool groupsHaveIsrcConflict(DisjointSets &sets, const QList<IndexRecord> &records,
                            int left, int right)
{
    left = sets.root(left);
    right = sets.root(right);
    QStringList leftIsrcs;
    QStringList rightIsrcs;
    for (int index = 0; index < records.size(); ++index) {
        QStringList *target = nullptr;
        const int root = sets.root(index);
        if (root == left) target = &leftIsrcs;
        if (root == right) target = &rightIsrcs;
        if (!target) continue;
        for (const auto &isrc : records[index].isrcs) {
            if (!target->contains(isrc)) target->append(isrc);
        }
    }
    return !leftIsrcs.isEmpty() && !rightIsrcs.isEmpty()
        && !hasIntersection(leftIsrcs, rightIsrcs);
}

bool validContentId(const QString &contentId)
{
    const QString decoded = QUrl::fromPercentEncoding(contentId.toUtf8());
    return decoded.endsWith(QStringLiteral(".ttml"))
        && !decoded.startsWith(QLatin1Char('/'))
        && !decoded.contains(QLatin1Char('\\'))
        && !decoded.split(QLatin1Char('/')).contains(QStringLiteral(".."))
        && QUrl(decoded).isRelative();
}

} // namespace

struct AmllProvider::Download {
    std::optional<QByteArray> payload;
    QString error;
    bool transportFailed = false;
    bool notModified = false;
    QByteArray etag;
    QByteArray lastModified;
};

AmllProvider::AmllProvider(QUrl indexUrl, QUrl contentBaseUrl, int timeoutMs,
                           QString cachePath, qint64 cacheMaxAgeSeconds)
    : m_indexUrl(std::move(indexUrl))
    , m_contentBaseUrl(std::move(contentBaseUrl))
    , m_timeoutMs(timeoutMs)
    , m_cachePath(cachePath.isEmpty() ? defaultCachePath() : std::move(cachePath))
    , m_cacheMaxAgeSeconds(cacheMaxAgeSeconds)
{
    QString base = m_contentBaseUrl.toString();
    if (!base.endsWith(QLatin1Char('/'))) {
        base += QLatin1Char('/');
        m_contentBaseUrl = QUrl(base);
    }
    m_normalizedIndexUrl = normalizedSourceUrl(m_indexUrl);
    m_normalizedContentBaseUrl = normalizedSourceUrl(m_contentBaseUrl);
    // Resolver consults cacheVersion() before calling search() when applying
    // provider-scoped misses. Load and validate the small local index now so
    // its content revision participates in that decision even after restart.
    loadCachedIndex();
}

AmllProvider::~AmllProvider()
{
    const auto replies = m_replies;
    for (auto *reply : replies) {
        QObject::disconnect(reply, nullptr, nullptr, nullptr);
        reply->abort();
    }
}

QString AmllProvider::id() const { return QStringLiteral("amll"); }
bool AmllProvider::isConfigured() const
{
    return m_indexUrl.isValid() && !m_indexUrl.isEmpty()
        && m_contentBaseUrl.isValid() && !m_contentBaseUrl.isEmpty();
}
MatchPolicy AmllProvider::matchPolicy() const { return MatchPolicy::PreserveVersions; }

QString AmllProvider::cacheVersion() const
{
    const QByteArray source = (m_normalizedIndexUrl + QLatin1Char('\x1f')
                               + m_normalizedContentBaseUrl).toUtf8();
    return id() + QLatin1Char(':')
        + QString::fromLatin1(QCryptographicHash::hash(
              source, QCryptographicHash::Sha256).toHex())
        + QLatin1Char(':') + (m_revision.isEmpty() ? QStringLiteral("unknown") : m_revision);
}

QString AmllProvider::defaultCachePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/plasma-lyrics/amll-index.jsonl");
}

QList<Candidate> AmllProvider::parseIndex(const QByteArray &payload, QString *error,
                                          int maximumEntries)
{
    QList<IndexRecord> records;
    const auto lines = payload.split('\n');
    if (lines.size() > maximumEntries + 1) {
        if (error) *error = QStringLiteral("AMLL index has too many records");
        return {};
    }
    int parsed = 0;
    for (const auto &line : lines) {
        if (line.trimmed().isEmpty()) continue;
        if (line.size() > 256 * 1024) {
            if (error) *error = QStringLiteral("AMLL index record is too large");
            return {};
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error) *error = QStringLiteral("invalid AMLL index record %1: %2")
                .arg(parsed + 1).arg(parseError.errorString());
            return {};
        }
        const auto root = document.object();
        const auto metadata = metadataMap(root.value(QStringLiteral("metadata")).toArray());
        const QStringList titles = metadata.value(QStringLiteral("musicName"));
        const QString rawFile = root.value(QStringLiteral("rawLyricFile")).toString();
        if (titles.isEmpty() || metadata.value(QStringLiteral("artists")).isEmpty()
            || rawFile.isEmpty() || !validContentId(rawFile)) {
            ++parsed;
            continue;
        }
        const QList<QPair<QString, QString>> metadataNamespaces{
            {QStringLiteral("ncmMusicId"), QStringLiteral("netease")},
            {QStringLiteral("qqMusicId"), QStringLiteral("qqmusic")},
            {QStringLiteral("appleMusicId"), QStringLiteral("applemusic")},
            {QStringLiteral("spotifyId"), QStringLiteral("spotify")},
        };
        IndexRecord record;
        Candidate candidate;
        candidate.trackId = stableId(metadata);
        candidate.title = titles.first();
        candidate.alternateTitles = titles.mid(1);
        candidate.artists = metadata.value(QStringLiteral("artists"));
        candidate.album = metadata.value(QStringLiteral("album")).value(0);
        candidate.contentId = rawFile;
        candidate.platformIds.insert(QStringLiteral("netease"), metadata.value(QStringLiteral("ncmMusicId")));
        candidate.platformIds.insert(QStringLiteral("qqmusic"), metadata.value(QStringLiteral("qqMusicId")));
        candidate.platformIds.insert(QStringLiteral("applemusic"), metadata.value(QStringLiteral("appleMusicId")));
        candidate.platformIds.insert(QStringLiteral("spotify"), metadata.value(QStringLiteral("spotifyId")));
        candidate.authors = metadata.value(QStringLiteral("ttmlAuthorGithubLogin"));
        candidate.authors.append(metadata.value(QStringLiteral("ttmlAuthorLyricify")));
        candidate.authors.removeDuplicates();
        for (const auto &[metadataKey, nameSpace] : metadataNamespaces) {
            for (const auto &id : metadata.value(metadataKey)) {
                record.associations.append(nameSpace + QLatin1Char(':') + id);
            }
        }
        if (record.associations.isEmpty()) {
            record.associations.append(QStringLiteral("identity:") + candidate.trackId);
        }
        record.candidate = std::move(candidate);
        record.revision = contentRevision(rawFile);
        record.isrcs = metadataValuesCaseInsensitive(metadata, QStringLiteral("isrc"));
        for (auto &isrc : record.isrcs) isrc = isrc.trimmed().toCaseFolded();
        record.isrcs.removeDuplicates();
        const QString conflictIdentity = normalizeSearchText(record.candidate.title)
            + QLatin1Char('\x1f') + cleanArtists(record.candidate.artists).join(QLatin1Char('/'))
            + QLatin1Char('\x1f') + normalizeSearchText(record.candidate.album)
            + QLatin1Char('\x1f') + record.isrcs.join(QLatin1Char('/'));
        record.conflictId = QStringLiteral("metadata:")
            + QString::fromLatin1(QCryptographicHash::hash(
                  conflictIdentity.toUtf8(), QCryptographicHash::Sha256).toHex());
        records.append(std::move(record));
        ++parsed;
    }
    if (parsed == 0 || records.isEmpty()) {
        if (error) *error = QStringLiteral("AMLL index contains no usable records");
        return {};
    }

    DisjointSets sets(records.size());
    QHash<QString, QList<int>> associationOwners;
    for (int index = 0; index < records.size(); ++index) {
        for (const auto &association : records[index].associations) {
            for (const int owner : associationOwners.value(association)) {
                if (compatibleIdentity(records[index], records[owner])
                    && !groupsHaveIsrcConflict(sets, records, index, owner)) {
                    sets.unite(index, owner);
                }
            }
            associationOwners[association].append(index);
        }
    }

    QHash<int, QList<int>> groups;
    for (int index = 0; index < records.size(); ++index) {
        groups[sets.root(index)].append(index);
    }
    QList<Candidate> result;
    result.reserve(groups.size());
    QList<int> roots = groups.keys();
    std::sort(roots.begin(), roots.end());
    QSet<QString> usedTrackIds;
    for (const int root : roots) {
        const auto &members = groups[root];
        int newestIndex = members.first();
        for (const int index : members) {
            if (records[index].revision >= records[newestIndex].revision) {
                newestIndex = index;
            }
        }
        Candidate merged = records[newestIndex].candidate;
        merged.trackId = records[root].candidate.trackId;
        if (usedTrackIds.contains(merged.trackId)) {
            // Conflicting upstream ids remain separate candidates and must
            // not collide in persisted refs. The oldest compatible owner
            // keeps the platform key; later conflicts use metadata identity.
            merged.trackId = records[root].conflictId;
        }
        usedTrackIds.insert(merged.trackId);
        for (const int index : members) {
            const Candidate &record = records[index].candidate;
            if (record.title != merged.title && !merged.alternateTitles.contains(record.title)) {
                merged.alternateTitles.append(record.title);
            }
            for (const auto &title : record.alternateTitles) {
                if (title != merged.title && !merged.alternateTitles.contains(title)) {
                    merged.alternateTitles.append(title);
                }
            }
            for (auto ids = record.platformIds.cbegin(); ids != record.platformIds.cend(); ++ids) {
                auto &target = merged.platformIds[ids.key()];
                for (const auto &id : ids.value()) {
                    if (!target.contains(id)) target.append(id);
                }
            }
            for (const auto &author : record.authors) {
                if (!merged.authors.contains(author)) merged.authors.append(author);
            }
        }
        result.append(std::move(merged));
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        return left.trackId < right.trackId;
    });
    return result;
}

void AmllProvider::loadCacheMetadata()
{
    m_fetchedAt = 0;
    m_etag.clear();
    m_lastModified.clear();
    QFile file(m_cachePath + QStringLiteral(".meta"));
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    const bool compatibleSource =
        object.value(QStringLiteral("sourceIndexUrl")).toString() == m_normalizedIndexUrl
        && object.value(QStringLiteral("sourceContentBaseUrl")).toString()
            == m_normalizedContentBaseUrl;
    if (!compatibleSource) {
        qInfo().noquote() << "AMLL cache source changed; validators discarded";
        return;
    }
    m_fetchedAt = object.value(QStringLiteral("fetchedAt")).toInteger();
    m_etag = object.value(QStringLiteral("etag")).toString().toLatin1();
    m_lastModified = object.value(QStringLiteral("lastModified")).toString().toLatin1();
}

void AmllProvider::saveCacheMetadata()
{
    QSaveFile file(m_cachePath + QStringLiteral(".meta"));
    if (!file.open(QIODevice::WriteOnly)) return;
    const QJsonObject object{{QStringLiteral("fetchedAt"), m_fetchedAt},
                             {QStringLiteral("etag"), QString::fromLatin1(m_etag)},
                             {QStringLiteral("lastModified"), QString::fromLatin1(m_lastModified)},
                             {QStringLiteral("sourceIndexUrl"), m_normalizedIndexUrl},
                             {QStringLiteral("sourceContentBaseUrl"),
                              m_normalizedContentBaseUrl},
                             {QStringLiteral("revision"), m_revision}};
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.commit();
}

void AmllProvider::setIndex(QList<Candidate> candidates, const QByteArray &payload,
                            const QByteArray &etag, const QByteArray &lastModified)
{
    m_candidates = std::move(candidates);
    m_indexLoaded = true;
    m_revision = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    if (!etag.isEmpty()) m_etag = etag;
    if (!lastModified.isEmpty()) m_lastModified = lastModified;
    m_fetchedAt = QDateTime::currentSecsSinceEpoch();
}

bool AmllProvider::loadCachedIndex()
{
    m_cacheAttempted = true;
    QFile file(m_cachePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > maximumIndexBytes) {
        return false;
    }
    const QByteArray payload = file.readAll();
    QString error;
    auto candidates = parseIndex(payload, &error);
    if (candidates.isEmpty()) {
        qWarning().noquote() << QStringLiteral("AMLL cached index rejected: ") + error;
        return false;
    }
    loadCacheMetadata();
    const qint64 persistedFetchedAt = m_fetchedAt;
    setIndex(std::move(candidates), payload, m_etag, m_lastModified);
    // Preserve the persisted freshness time; setIndex records "now" for a
    // freshly downloaded payload, not for an old file merely read at startup.
    m_fetchedAt = persistedFetchedAt;
    return true;
}

QList<Candidate> AmllProvider::localSearch(const TrackQuery &query) const
{
    QList<Candidate> exactIds;
    for (const auto &candidate : m_candidates) {
        bool exact = false;
        for (auto it = query.platformIds.cbegin(); it != query.platformIds.cend() && !exact; ++it) {
            for (const auto &id : it.value()) {
                if (candidate.platformIds.value(it.key()).contains(id)) {
                    exact = true;
                    break;
                }
            }
        }
        if (exact) exactIds.append(candidate);
    }
    if (!exactIds.isEmpty()) return exactIds;

    const auto ranked = rankCandidates(query, m_candidates, MatchPolicy::PreserveVersions);
    QList<Candidate> result;
    result.reserve(std::min<qsizetype>(ranked.size(), 50));
    for (const auto &item : ranked) {
        if (item.score.versionTier == VersionTier::Conflict) continue;
        if (item.score.title < 0.45 || item.score.total < 0.5) continue;
        result.append(item.candidate);
        if (result.size() == 50) break;
    }
    return result;
}

void AmllProvider::search(const TrackQuery &query, SearchCallback callback)
{
    if (!m_cacheAttempted) loadCachedIndex();
    if (m_indexLoaded) {
        // Snapshot both pieces before invoking client code. The callback can
        // start an asynchronous content fetch while the stale-while-revalidate
        // refresh below replaces m_candidates and m_revision.
        const QString resultCacheVersion = cacheVersion();
        const auto candidates = localSearch(query);
        callback({candidates, {}, false, resultCacheVersion});
        if (!m_refreshing
            && QDateTime::currentSecsSinceEpoch() - m_fetchedAt >= m_cacheMaxAgeSeconds) {
            refreshIndex();
        }
        return;
    }
    m_pending.append({query, std::move(callback)});
    refreshIndex();
}

void AmllProvider::completePending(const QString &error, bool transportFailed)
{
    auto pending = std::exchange(m_pending, {});
    for (auto &item : pending) {
        item.callback({m_indexLoaded ? localSearch(item.query) : QList<Candidate>{},
                       m_indexLoaded ? QString() : error,
                       !m_indexLoaded && transportFailed,
                       m_indexLoaded ? cacheVersion() : QString()});
    }
}

void AmllProvider::refreshIndex()
{
    if (m_refreshing) return;
    m_refreshing = true;
    get(m_indexUrl, m_indexLoaded, maximumIndexBytes, [this](Download download) {
        finishRefresh(std::move(download));
    });
}

void AmllProvider::finishRefresh(Download download)
{
    m_refreshing = false;
    if (download.notModified && m_indexLoaded) {
        m_fetchedAt = QDateTime::currentSecsSinceEpoch();
        saveCacheMetadata();
        completePending();
        return;
    }
    if (!download.payload) {
        qWarning().noquote() << QStringLiteral("AMLL index refresh failed: ") + download.error;
        completePending(download.error, download.transportFailed);
        return;
    }
    if (download.payload->size() > maximumIndexBytes) {
        completePending(QStringLiteral("AMLL index exceeds size limit"), false);
        return;
    }
    QString parseError;
    auto candidates = parseIndex(*download.payload, &parseError);
    if (candidates.isEmpty()) {
        qWarning().noquote() << QStringLiteral("AMLL index refresh rejected: ") + parseError;
        completePending(parseError, false);
        return;
    }

    if (QDir().mkpath(QFileInfo(m_cachePath).absolutePath())) {
        QSaveFile file(m_cachePath);
        if (file.open(QIODevice::WriteOnly)
            && file.write(*download.payload) == download.payload->size() && file.commit()) {
            // The cache has been atomically replaced only after full parsing.
        } else {
            qWarning().noquote() << QStringLiteral("cannot atomically save AMLL index cache: ")
                                      + file.errorString();
        }
    }
    setIndex(std::move(candidates), *download.payload, download.etag, download.lastModified);
    saveCacheMetadata();
    completePending();
}

void AmllProvider::fetch(const QString &trackId, FetchCallback callback)
{
    QString contentId = trackId;
    if (!contentId.endsWith(QStringLiteral(".ttml"))) {
        const auto found = std::find_if(m_candidates.cbegin(), m_candidates.cend(), [&](const auto &candidate) {
            return candidate.trackId == trackId;
        });
        if (found == m_candidates.cend()) {
            callback({std::nullopt, QStringLiteral("AMLL content revision is unknown"), false});
            return;
        }
        contentId = found->contentId;
    }
    if (!validContentId(contentId)) {
        callback({std::nullopt, QStringLiteral("AMLL content revision is invalid"), false});
        return;
    }
    QUrl url = m_contentBaseUrl.resolved(QUrl(QStringLiteral("raw-lyrics/") + contentId));
    get(url, false, maximumTtmlBytes,
        [callback = std::move(callback), contentId](Download download) mutable {
        if (!download.payload) {
            callback({std::nullopt, std::move(download.error), download.transportFailed});
            return;
        }
        if (download.payload->size() > maximumTtmlBytes) {
            callback({std::nullopt, QStringLiteral("AMLL TTML exceeds size limit"), false});
            return;
        }
        QString parseError;
        auto document = TtmlParser::parse(*download.payload, &parseError);
        if (!document) {
            callback({std::nullopt, std::move(parseError), false});
            return;
        }
        document->metadata.insert(QStringLiteral("source"),
                                  QJsonArray{QStringLiteral("AMLL TTML DB")});
        document->metadata.insert(QStringLiteral("rawLyricFile"),
                                  QJsonArray{contentId});
        callback({std::move(document), {}, false});
    });
}

void AmllProvider::get(const QUrl &url, bool conditional, qint64 maximumBytes,
                       DownloadCallback callback)
{
    getAttempt(url, conditional, maximumBytes, 1, std::move(callback));
}

void AmllProvider::getAttempt(const QUrl &url, bool conditional, qint64 maximumBytes,
                              int attempt,
                              DownloadCallback callback)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("plasma-lyricsd/0.2"));
    if (conditional && !m_etag.isEmpty()) request.setRawHeader("If-None-Match", m_etag);
    if (conditional && !m_lastModified.isEmpty()) request.setRawHeader("If-Modified-Since", m_lastModified);
    auto *reply = m_network.get(request);
    m_replies.insert(reply);
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    const auto timedOut = std::make_shared<bool>(false);
    const auto sizeExceeded = std::make_shared<bool>(false);
    QObject::connect(timeout, &QTimer::timeout, reply, [reply, timedOut] {
        *timedOut = true;
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                     [reply, maximumBytes, sizeExceeded](qint64 received, qint64) {
        if (received > maximumBytes) {
            *sizeExceeded = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [this, url, conditional, maximumBytes, attempt,
                      callback = std::move(callback), reply, timeout, timedOut,
                      sizeExceeded]() mutable {
        timeout->stop();
        const auto replyError = reply->error();
        const QByteArray payload = reply->readAll();
        const auto statusValue = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int status = statusValue.toInt();
        const bool transportFailed = !*sizeExceeded && (!statusValue.isValid()
            || (status >= 200 && status < 300));
        const QString errorText = *sizeExceeded
            ? QStringLiteral("response exceeds size limit")
            : *timedOut ? QStringLiteral("request timed out after %1 ms").arg(m_timeoutMs)
                        : reply->errorString();
        const QByteArray etag = reply->rawHeader("ETag");
        const QByteArray lastModified = reply->rawHeader("Last-Modified");
        m_replies.remove(reply);
        reply->deleteLater();
        if (status == 304) {
            callback({std::nullopt, {}, false, true, etag, lastModified});
            return;
        }
        if (replyError == QNetworkReply::NoError) {
            callback({payload, {}, false, false, etag, lastModified});
            return;
        }
        if (transportFailed && attempt < 3) {
            getAttempt(url, conditional, maximumBytes, attempt + 1,
                       std::move(callback));
            return;
        }
        callback({std::nullopt, errorText, transportFailed, false, etag, lastModified});
    });
    timeout->start(std::max(1, m_timeoutMs));
}

} // namespace PlasmaLyrics
