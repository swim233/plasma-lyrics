#include "providers/amll/amllprovider.h"
#include "core/store/lyricstore.h"

#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <utility>

using namespace PlasmaLyrics;

namespace {

const QByteArray indexPayload = QByteArrayLiteral(
    "{\"metadata\":[[\"album\",[\"Album\"]],[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song (Live)\",\"Song Live\"]],[\"ncmMusicId\",[\"42\"]],[\"appleMusicId\",[\"99\"]],[\"ttmlAuthorGithubLogin\",[\"author\"]]],\"rawLyricFile\":\"1000-a-old.ttml\"}\n"
    "{\"metadata\":[[\"album\",[\"Album\"]],[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song (Live)\",\"Song Live\"]],[\"ncmMusicId\",[\"42\"]],[\"appleMusicId\",[\"99\"]],[\"ttmlAuthorGithubLogin\",[\"author\"]]],\"rawLyricFile\":\"2000-a-new.ttml\"}\n"
    "{\"metadata\":[[\"album\",[\"Other\"]],[\"artists\",[\"Other Artist\"]],[\"musicName\",[\"Other Song\"]],[\"spotifyId\",[\"spotify-id\"]]],\"rawLyricFile\":\"1500-b.ttml\"}\n");

const QByteArray ttmlPayload = QStringLiteral(
    "<tt xmlns=\"http://www.w3.org/ns/ttml\" xmlns:ttm=\"http://www.w3.org/ns/ttml#metadata\">"
    "<body><div><p begin=\"0s\" end=\"2s\"><span begin=\"0s\" end=\"1s\">Test</span>"
    "<span ttm:role=\"x-translation\" xml:lang=\"zh-CN\">测试</span>"
    "</p></div></body></tt>").toUtf8();

class HttpServer : public QTcpServer
{
public:
    HttpServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    QByteArray &request = m_requests[socket];
                    request += socket->readAll();
                    if (!request.contains("\r\n\r\n")) return;
                    const QByteArray firstLine = request.left(request.indexOf("\r\n"));
                    m_receivedRequests.append(request);
                    m_requests.remove(socket);
                    ++m_requestCount;
                    const bool indexRequest = firstLine.contains("/index");
                    const QByteArray body = indexRequest ? m_indexBody : m_contentBody;
                    const int status = indexRequest ? m_indexStatus : m_contentStatus;
                    const bool truncated = indexRequest ? m_truncatedIndex : false;
                    const QByteArray reason = status == 200 ? QByteArrayLiteral("OK")
                        : status == 304 ? QByteArrayLiteral("Not Modified")
                        : status == 404 ? QByteArrayLiteral("Not Found")
                                        : QByteArrayLiteral("Server Error");
                    const qint64 declaredLength = status == 304 ? 0
                        : body.size() + (truncated ? 32 : 0);
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + ' ' + reason
                                  + "\r\nContent-Length: " + QByteArray::number(declaredLength)
                                  + "\r\nETag: test-etag\r\nLast-Modified: Tue, 08 Sep 2026 00:00:00 GMT"
                                    "\r\nConnection: close\r\n\r\n"
                                  + (status == 304 ? QByteArray() : body));
                    socket->disconnectFromHost();
                });
            }
        });
    }

    bool start() { return listen(QHostAddress::LocalHost); }
    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));
    }
    int requestCount() const { return m_requestCount; }
    void setIndexResponse(int status, QByteArray body = indexPayload,
                          bool truncated = false)
    {
        m_indexStatus = status;
        m_indexBody = std::move(body);
        m_truncatedIndex = truncated;
    }
    QList<QByteArray> receivedRequests() const { return m_receivedRequests; }

private:
    QHash<QTcpSocket *, QByteArray> m_requests;
    int m_requestCount = 0;
    int m_indexStatus = 200;
    int m_contentStatus = 200;
    QByteArray m_indexBody = indexPayload;
    QByteArray m_contentBody = ttmlPayload;
    bool m_truncatedIndex = false;
    QList<QByteArray> m_receivedRequests;
};

QByteArray requestHeader(const QByteArray &request, const QByteArray &name)
{
    const auto lines = request.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r')) line.chop(1);
        const qsizetype separator = line.indexOf(':');
        if (separator < 0
            || line.left(separator).trimmed().compare(name, Qt::CaseInsensitive) != 0) {
            continue;
        }
        return line.mid(separator + 1).trimmed();
    }
    return {};
}

ProviderSearchResult searchAndWait(AmllProvider &provider, const TrackQuery &query)
{
    std::optional<ProviderSearchResult> result;
    QEventLoop loop;
    provider.search(query, [&](auto value) { result = std::move(value); loop.quit(); });
    if (!result) {
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return result.value_or(ProviderSearchResult{{}, QStringLiteral("timeout"), true});
}

QString normalizedCacheSourceUrl(QUrl url)
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

void writeCache(const QString &path, qint64 fetchedAt, const QByteArray &etag = {},
                const QUrl &sourceIndexUrl = {}, const QUrl &sourceContentBaseUrl = {})
{
    QFile cache(path);
    QVERIFY(cache.open(QIODevice::WriteOnly));
    QCOMPARE(cache.write(indexPayload), indexPayload.size());
    cache.close();
    QFile metadata(path + QStringLiteral(".meta"));
    QVERIFY(metadata.open(QIODevice::WriteOnly));
    const QJsonObject object{{QStringLiteral("fetchedAt"), fetchedAt},
                             {QStringLiteral("etag"), QString::fromLatin1(etag)},
                             {QStringLiteral("sourceIndexUrl"),
                              normalizedCacheSourceUrl(sourceIndexUrl)},
                             {QStringLiteral("sourceContentBaseUrl"),
                              normalizedCacheSourceUrl(sourceContentBaseUrl)}};
    QVERIFY(metadata.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) > 0);
}

ProviderFetchResult fetchAndWait(AmllProvider &provider, const QString &contentId)
{
    std::optional<ProviderFetchResult> result;
    QEventLoop loop;
    provider.fetch(contentId, [&](auto value) { result = std::move(value); loop.quit(); });
    if (!result) {
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return result.value_or(ProviderFetchResult{std::nullopt, QStringLiteral("timeout"), true});
}

} // namespace

class AmllProviderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesDeduplicatesAndPreservesAliases()
    {
        QString error;
        const auto candidates = AmllProvider::parseIndex(indexPayload, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(candidates.size(), 2);
        const auto found = std::find_if(candidates.cbegin(), candidates.cend(), [](const auto &candidate) {
            return candidate.trackId == QStringLiteral("netease:42");
        });
        QVERIFY(found != candidates.cend());
        QCOMPARE(found->contentId, QStringLiteral("2000-a-new.ttml"));
        QCOMPARE(found->alternateTitles, QStringList{QStringLiteral("Song Live")});
        QCOMPARE(found->platformIds.value(QStringLiteral("applemusic")),
                 QStringList{QStringLiteral("99")});
        QCOMPARE(found->authors, QStringList{QStringLiteral("author")});
    }

    void addedPlatformAssociationDoesNotCreateANewCandidateOrChangeTheStableId()
    {
        const QByteArray history = QByteArrayLiteral(
            "{\"metadata\":[[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song\"]],[\"appleMusicId\",[\"99\"]]],\"rawLyricFile\":\"1000-old.ttml\"}\n"
            "{\"metadata\":[[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song\"]],[\"ncmMusicId\",[\"42\"]],[\"appleMusicId\",[\"99\"]]],\"rawLyricFile\":\"2000-new.ttml\"}\n");
        QString error;

        const auto candidates = AmllProvider::parseIndex(history, &error);

        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(candidates.size(), 1);
        QCOMPARE(candidates.first().trackId, QStringLiteral("applemusic:99"));
        QCOMPARE(candidates.first().contentId, QStringLiteral("2000-new.ttml"));
        QCOMPARE(candidates.first().platformIds.value(QStringLiteral("netease")),
                 QStringList{QStringLiteral("42")});
        QCOMPARE(candidates.first().platformIds.value(QStringLiteral("applemusic")),
                 QStringList{QStringLiteral("99")});
    }

    void bridgeRecordMergesPreviouslyIndependentIdentityOwners()
    {
        const QByteArray history = QByteArrayLiteral(
            "{\"metadata\":[[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song\"]],[\"ncmMusicId\",[\"42\"]],[\"isrc\",[\"JP-AAA-00-00001\"]]],\"rawLyricFile\":\"1000-ncm.ttml\"}\n"
            "{\"metadata\":[[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song\"]],[\"appleMusicId\",[\"99\"]],[\"isrc\",[\"JP-AAA-00-00001\"]]],\"rawLyricFile\":\"1500-apple.ttml\"}\n"
            "{\"metadata\":[[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song\"]],[\"ncmMusicId\",[\"42\"]],[\"appleMusicId\",[\"99\"]],[\"isrc\",[\"JP-AAA-00-00001\"]]],\"rawLyricFile\":\"2000-bridge.ttml\"}\n");
        QString error;

        const auto candidates = AmllProvider::parseIndex(history, &error);

        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(candidates.size(), 1);
        QCOMPARE(candidates.first().trackId, QStringLiteral("netease:42"));
        QCOMPARE(candidates.first().contentId, QStringLiteral("2000-bridge.ttml"));
        QCOMPARE(candidates.first().platformIds.value(QStringLiteral("netease")),
                 QStringList{QStringLiteral("42")});
        QCOMPARE(candidates.first().platformIds.value(QStringLiteral("applemusic")),
                 QStringList{QStringLiteral("99")});
    }

    void conflictingSharedPlatformIdDoesNotMergeDifferentIdentity()
    {
        const QByteArray conflict = QByteArrayLiteral(
            "{\"metadata\":[[\"artists\",[\"Artist A\"]],[\"musicName\",[\"Song A\"]],[\"ncmMusicId\",[\"42\"]],[\"isrc\",[\"JP-AAA-00-00001\"]]],\"rawLyricFile\":\"1000-a.ttml\"}\n"
            "{\"metadata\":[[\"artists\",[\"Artist B\"]],[\"musicName\",[\"Song B\"]],[\"ncmMusicId\",[\"42\"]],[\"isrc\",[\"JP-BBB-00-00002\"]]],\"rawLyricFile\":\"2000-b.ttml\"}\n");
        QString error;

        const auto candidates = AmllProvider::parseIndex(conflict, &error);

        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(candidates.size(), 2);
        QVERIFY(candidates[0].trackId != candidates[1].trackId);
        const auto contents = QStringList{candidates[0].contentId, candidates[1].contentId};
        QVERIFY(contents.contains(QStringLiteral("1000-a.ttml")));
        QVERIFY(contents.contains(QStringLiteral("2000-b.ttml")));
    }

    void conflictingIsrcPreventsMergeEvenWhenTextIdentityMatches()
    {
        const QByteArray conflict = QByteArrayLiteral(
            "{\"metadata\":[[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song\"]],[\"ncmMusicId\",[\"42\"]],[\"isrc\",[\"JP-AAA-00-00001\"]]],\"rawLyricFile\":\"1000-a.ttml\"}\n"
            "{\"metadata\":[[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song\"]],[\"ncmMusicId\",[\"42\"]],[\"ISRC\",[\"JP-BBB-00-00002\"]]],\"rawLyricFile\":\"2000-b.ttml\"}\n");

        QCOMPARE(AmllProvider::parseIndex(conflict).size(), 2);
    }

    void bridgeWithoutIsrcCannotJoinConflictingIsrcGroups()
    {
        const QByteArray conflict = QByteArrayLiteral(
            "{\"metadata\":[[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song\"]],[\"ncmMusicId\",[\"42\"]],[\"isrc\",[\"JP-AAA-00-00001\"]]],\"rawLyricFile\":\"1000-a.ttml\"}\n"
            "{\"metadata\":[[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song\"]],[\"appleMusicId\",[\"99\"]],[\"isrc\",[\"JP-BBB-00-00002\"]]],\"rawLyricFile\":\"1100-b.ttml\"}\n"
            "{\"metadata\":[[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song\"]],[\"ncmMusicId\",[\"42\"]],[\"appleMusicId\",[\"99\"]]],\"rawLyricFile\":\"2000-bridge.ttml\"}\n");

        QCOMPARE(AmllProvider::parseIndex(conflict).size(), 2);
    }

    void rejectsBadAndOversizedLogicalIndexes()
    {
        QString error;
        QVERIFY(AmllProvider::parseIndex(QByteArray("not-json"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(AmllProvider::parseIndex(indexPayload, &error, 1).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void concurrentFirstSearchSharesOneIndexDownload()
    {
        HttpServer server;
        QVERIFY(server.start());
        QTemporaryDir directory;
        AmllProvider provider(server.url(QStringLiteral("/index")),
                              server.url(QStringLiteral("/")), 500,
                              directory.filePath(QStringLiteral("index.jsonl")), 86400);
        int callbacks = 0;
        QList<ProviderSearchResult> results;
        const TrackQuery query{QStringLiteral("Song (Live)"), {QStringLiteral("Artist")},
                               QStringLiteral("Album"), 0};
        provider.search(query, [&](auto result) { results.append(std::move(result)); ++callbacks; });
        provider.search(query, [&](auto result) { results.append(std::move(result)); ++callbacks; });
        QTRY_COMPARE(callbacks, 2);
        QCOMPARE(server.requestCount(), 1);
        QCOMPARE(results.first().candidates.size(), 1);
        QCOMPARE(results.first().candidates.first().trackId, QStringLiteral("netease:42"));
        QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("index.jsonl"))));
    }

    void searchKeepsOnlyTheMatchingMultilingualAnniversaryOrdinal()
    {
        HttpServer server;
        QVERIFY(server.start());
        server.setIndexResponse(200, QByteArrayLiteral(
            "{\"metadata\":[[\"artists\",[\"Singer\"]],[\"musicName\",[\"Example Song (25周年纪念版)\",\"Example Song (20th Anniversary Edition)\"]],[\"ncmMusicId\",[\"25\"]]],\"rawLyricFile\":\"2500-wrong.ttml\"}\n"
            "{\"metadata\":[[\"artists\",[\"Singer\"]],[\"musicName\",[\"Example Song (20周年記念版)\",\"Example Song (20th Anniversary Edition)\"]],[\"ncmMusicId\",[\"20\"]]],\"rawLyricFile\":\"2000-right.ttml\"}\n"));
        QTemporaryDir directory;
        AmllProvider provider(server.url(QStringLiteral("/index")),
                              server.url(QStringLiteral("/")), 500,
                              directory.filePath(QStringLiteral("index.jsonl")), 86400);
        const TrackQuery query{QStringLiteral("Example Song (20th Anniversary Edition)"),
                               {QStringLiteral("Singer")}, QString(), 0};

        const auto result = searchAndWait(provider, query);

        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.candidates.size(), 1);
        QCOMPARE(result.candidates.first().trackId, QStringLiteral("netease:20"));
    }

    void exactPlatformIdFindsCandidateWithoutTitleEvidence()
    {
        QTemporaryDir directory;
        const QString cachePath = directory.filePath(QStringLiteral("index.jsonl"));
        const QUrl indexUrl(QStringLiteral("https://example.invalid/index"));
        const QUrl contentUrl(QStringLiteral("https://example.invalid/"));
        writeCache(cachePath, QDateTime::currentSecsSinceEpoch(), {}, indexUrl,
                   contentUrl);
        AmllProvider provider(indexUrl, contentUrl, 500,
                              cachePath, 86400);
        QVERIFY(!provider.cacheVersion().endsWith(QStringLiteral(":unknown")));
        TrackQuery query{QStringLiteral("Unrelated"), {QStringLiteral("Nobody")},
                         QString(), 0};
        query.platformIds.insert(QStringLiteral("applemusic"),
                                 {QStringLiteral("99")});

        const auto result = searchAndWait(provider, query);

        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.candidates.size(), 1);
        QCOMPARE(result.candidates.first().trackId, QStringLiteral("netease:42"));
    }

    void staleCacheIsReturnedWhileFailedRefreshKeepsOldIndex()
    {
        HttpServer server;
        QVERIFY(server.start());
        server.setIndexResponse(500, QByteArrayLiteral("failed"));
        QTemporaryDir directory;
        const QString cachePath = directory.filePath(QStringLiteral("index.jsonl"));
        writeCache(cachePath, 1, QByteArrayLiteral("old-etag"),
                   server.url(QStringLiteral("/index")), server.url(QStringLiteral("/")));
        AmllProvider provider(server.url(QStringLiteral("/index")),
                              server.url(QStringLiteral("/")), 500,
                              cachePath, 1);
        const TrackQuery query{QStringLiteral("Song (Live)"),
                               {QStringLiteral("Artist")}, QStringLiteral("Album"), 0};

        const auto result = searchAndWait(provider, query);

        QCOMPARE(result.candidates.size(), 1);
        QVERIFY(result.error.isEmpty());
        QTRY_COMPARE(server.requestCount(), 1);
        QCOMPARE(requestHeader(server.receivedRequests().first(),
                               QByteArrayLiteral("If-None-Match")),
                 QByteArrayLiteral("old-etag"));
        const auto stillUsable = searchAndWait(provider, query);
        QCOMPARE(stillUsable.candidates.size(), 1);
    }

    void staleSearchResultRetainsTheRevisionThatProducedItsCandidates()
    {
        HttpServer server;
        QVERIFY(server.start());
        const QByteArray refreshedIndex = QByteArrayLiteral(
            "{\"metadata\":[[\"album\",[\"Album\"]],[\"artists\",[\"Artist\"]],[\"musicName\",[\"Song (Live)\"]]],\"rawLyricFile\":\"3000-refreshed.ttml\"}\n");
        server.setIndexResponse(200, refreshedIndex);
        QTemporaryDir directory;
        const QString cachePath = directory.filePath(QStringLiteral("index.jsonl"));
        writeCache(cachePath, 1, QByteArrayLiteral("old-etag"),
                   server.url(QStringLiteral("/index")), server.url(QStringLiteral("/")));
        AmllProvider provider(server.url(QStringLiteral("/index")),
                              server.url(QStringLiteral("/")), 500,
                              cachePath, 1);
        const QString oldCacheVersion = provider.cacheVersion();

        const auto stale = searchAndWait(
            provider, {QStringLiteral("Song (Live)"),
                       {QStringLiteral("Artist")}, QStringLiteral("Album"), 0});

        QCOMPARE(stale.cacheVersion, oldCacheVersion);
        QCOMPARE(stale.candidates.first().contentId,
                 QStringLiteral("2000-a-new.ttml"));
        QTRY_VERIFY(provider.cacheVersion() != oldCacheVersion);
        QCOMPARE(stale.cacheVersion, oldCacheVersion);
        QCOMPARE(searchAndWait(provider, {QStringLiteral("Song (Live)"),
                                          {QStringLiteral("Artist")},
                                          QStringLiteral("Album"), 0})
                     .candidates.first().contentId,
                 QStringLiteral("3000-refreshed.ttml"));
    }

    void notModifiedRefreshUpdatesFreshnessAndAvoidsImmediateRefetch()
    {
        HttpServer server;
        QVERIFY(server.start());
        server.setIndexResponse(304);
        QTemporaryDir directory;
        const QString cachePath = directory.filePath(QStringLiteral("index.jsonl"));
        writeCache(cachePath, 1, QByteArrayLiteral("old-etag"),
                   server.url(QStringLiteral("/index")), server.url(QStringLiteral("/")));
        AmllProvider provider(server.url(QStringLiteral("/index")),
                              server.url(QStringLiteral("/")), 500,
                              cachePath, 3600);
        const TrackQuery query{QStringLiteral("Song (Live)"),
                               {QStringLiteral("Artist")}, QStringLiteral("Album"), 0};

        QCOMPARE(searchAndWait(provider, query).candidates.size(), 1);
        QTRY_COMPARE(server.requestCount(), 1);
        QCOMPARE(searchAndWait(provider, query).candidates.size(), 1);
        QTest::qWait(50);
        QCOMPARE(server.requestCount(), 1);
    }

    void firstIndex404IsAProviderFailureWithoutTransportRetry()
    {
        HttpServer server;
        QVERIFY(server.start());
        server.setIndexResponse(404, QByteArrayLiteral("missing"));
        QTemporaryDir directory;
        AmllProvider provider(server.url(QStringLiteral("/index")),
                              server.url(QStringLiteral("/")), 500,
                              directory.filePath(QStringLiteral("index.jsonl")), 86400);

        const auto result = searchAndWait(
            provider, {QStringLiteral("Song"), {QStringLiteral("Artist")}, QString(), 0});

        QVERIFY(result.candidates.isEmpty());
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!result.transportFailed);
        QCOMPARE(server.requestCount(), 1);
    }

    void truncatedFirstIndexResponseUsesBoundedTransportRetries()
    {
        HttpServer server;
        QVERIFY(server.start());
        server.setIndexResponse(200, indexPayload, true);
        QTemporaryDir directory;
        AmllProvider provider(server.url(QStringLiteral("/index")),
                              server.url(QStringLiteral("/")), 500,
                              directory.filePath(QStringLiteral("index.jsonl")), 86400);

        const auto result = searchAndWait(
            provider, {QStringLiteral("Song"), {QStringLiteral("Artist")}, QString(), 0});

        QVERIFY(result.candidates.isEmpty());
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.transportFailed);
        QCOMPARE(server.requestCount(), 3);
    }

    void fetchesAndParsesTtml()
    {
        HttpServer server;
        QVERIFY(server.start());
        QTemporaryDir directory;
        AmllProvider provider(server.url(QStringLiteral("/index")),
                              server.url(QStringLiteral("/")), 500,
                              directory.filePath(QStringLiteral("index.jsonl")), 86400);
        const auto result = fetchAndWait(provider, QStringLiteral("2000-a-new.ttml"));
        QVERIFY2(result.document.has_value(), qPrintable(result.error));
        QCOMPARE(result.document->lines.first().text, QStringLiteral("Test"));
        QCOMPARE(result.document->lines.first().translation,
                 std::optional<QString>(QStringLiteral("测试")));
        QCOMPARE(result.document->metadata.value(QStringLiteral("source")).toArray().first().toString(),
                 QStringLiteral("AMLL TTML DB"));
    }

    void cacheVersionUsesNormalizedIndexAndContentUrls()
    {
        QTemporaryDir directory;
        AmllProvider normalized(
            QUrl(QStringLiteral("HTTP://Example.COM:80/a/../index#fragment")),
            QUrl(QStringLiteral("HTTP://Example.COM:80/content/")), 500,
            directory.filePath(QStringLiteral("first.jsonl")));
        AmllProvider equivalent(
            QUrl(QStringLiteral("http://example.com/index")),
            QUrl(QStringLiteral("http://example.com/content")), 500,
            directory.filePath(QStringLiteral("second.jsonl")));
        AmllProvider otherContent(
            QUrl(QStringLiteral("http://example.com/index")),
            QUrl(QStringLiteral("http://mirror.example/content")), 500,
            directory.filePath(QStringLiteral("third.jsonl")));

        QCOMPARE(normalized.cacheVersion(), equivalent.cacheVersion());
        QVERIFY(normalized.cacheVersion() != otherContent.cacheVersion());
    }

    void sourceChangeDropsValidatorsRefreshesAndInvalidatesOldMiss()
    {
        HttpServer server;
        QVERIFY(server.start());
        QTemporaryDir directory;
        const QString cachePath = directory.filePath(QStringLiteral("index.jsonl"));
        const QUrl oldIndex(QStringLiteral("https://old.example/index"));
        const QUrl oldContent(QStringLiteral("https://old.example/content"));
        writeCache(cachePath, QDateTime::currentSecsSinceEpoch(),
                   QByteArrayLiteral("old-etag"), oldIndex, oldContent);
        AmllProvider oldProvider(oldIndex, oldContent, 500, cachePath, 86400);
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        QVERIFY(store.recordProviderMiss(QStringLiteral("fingerprint"),
                                         QStringLiteral("amll"),
                                         QStringLiteral("fetch-error"),
                                         oldProvider.cacheVersion()));

        const QUrl newIndex = server.url(QStringLiteral("/index"));
        const QUrl newContent = server.url(QStringLiteral("/"));
        AmllProvider newProvider(newIndex, newContent, 500, cachePath, 86400);
        QVERIFY(!store.freshProviderMiss(QStringLiteral("fingerprint"),
                                         QStringLiteral("amll"),
                                         newProvider.cacheVersion()).has_value());

        const auto result = searchAndWait(
            newProvider, {QStringLiteral("Song (Live)"),
                          {QStringLiteral("Artist")}, QStringLiteral("Album"), 0});

        QCOMPARE(result.candidates.size(), 1);
        QTRY_COMPARE(server.requestCount(), 1);
        QVERIFY(requestHeader(server.receivedRequests().first(),
                              QByteArrayLiteral("If-None-Match")).isNull());
        QVERIFY(requestHeader(server.receivedRequests().first(),
                              QByteArrayLiteral("If-Modified-Since")).isNull());
        QFile metadata(cachePath + QStringLiteral(".meta"));
        QVERIFY(metadata.open(QIODevice::ReadOnly));
        const auto object = QJsonDocument::fromJson(metadata.readAll()).object();
        QCOMPARE(object.value(QStringLiteral("sourceIndexUrl")).toString(),
                 normalizedCacheSourceUrl(newIndex));
        QCOMPARE(object.value(QStringLiteral("sourceContentBaseUrl")).toString(),
                 normalizedCacheSourceUrl(newContent));
    }
};

QTEST_GUILESS_MAIN(AmllProviderTest)
#include "tst_amllprovider.moc"
