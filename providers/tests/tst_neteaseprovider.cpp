#include "providers/netease/neteaseprovider.h"

#include "core/lyric/timeline.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include <algorithm>

using namespace PlasmaLyrics;

namespace {

class ScriptedHttpServer : public QTcpServer
{
public:
    enum class Reply { Disconnect, TruncatedSuccess, NotFound, ServerError, InvalidJson, SearchSuccess, Hang };

    explicit ScriptedHttpServer(QList<Reply> replies)
        : m_replies(std::move(replies))
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    m_requests[socket].append(socket->readAll());
                    if (!m_requests[socket].contains("\r\n\r\n")) {
                        return;
                    }
                    m_requests.remove(socket);
                    const int index = m_requestCount++;
                    const Reply reply = m_replies.value(index, m_replies.constLast());
                    if (reply == Reply::Disconnect) {
                        socket->abort();
                        return;
                    }
                    if (reply == Reply::Hang) {
                        return;
                    }
                    if (reply == Reply::TruncatedSuccess) {
                        const QByteArray partialBody = R"({"result":{"songs":[)";
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                      + QByteArray::number(partialBody.size() + 100)
                                      + "\r\nConnection: close\r\n\r\n" + partialBody);
                        socket->disconnectFromHost();
                        return;
                    }
                    QByteArray body;
                    QByteArray status = "200 OK";
                    if (reply == Reply::NotFound) {
                        status = "404 Not Found";
                    } else if (reply == Reply::ServerError) {
                        status = "503 Service Unavailable";
                    } else if (reply == Reply::InvalidJson) {
                        body = "not json";
                    } else {
                        body = R"({"result":{"songs":[{"id":1,"name":"song","artists":[{"name":"artist"}],"album":{"name":"album"},"duration":1000}]}})";
                    }
                    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }

    bool start() { return listen(QHostAddress::LocalHost); }
    QUrl baseUrl() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort()));
    }
    int requestCount() const { return m_requestCount; }

private:
    QList<Reply> m_replies;
    QHash<QTcpSocket *, QByteArray> m_requests;
    int m_requestCount = 0;
};

ProviderSearchResult searchAndWait(NeteaseProvider &provider)
{
    std::optional<ProviderSearchResult> result;
    QEventLoop loop;
    const TrackQuery query{QStringLiteral("song"), {QStringLiteral("artist")}, QString(), 1000};
    provider.search(query, [&](ProviderSearchResult value) {
        result = std::move(value);
        loop.quit();
    });
    if (!result) {
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return result.value_or(ProviderSearchResult{{}, QStringLiteral("test timed out")});
}

} // namespace

class NeteaseProviderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesSearchCandidates();
    void parsesTransNamesIntoAlternateTitles();
    void handlesDirtyLyricAndEmptyWordData();
    void creditsFromThePreV1EndpointStillFilter();
    void timeoutStaircase();
    void retriesNetworkErrorsAndCanRecover();
    void retriesTruncated2xxResponsesAndCanRecover();
    void givesUpAfterThreeNetworkTimeouts();
    void doesNotRetryHttpErrors();
    void doesNotRetryJsonErrors();
    void destroyingProviderCancelsPendingRequest();
};

void NeteaseProviderTest::parsesSearchCandidates()
{
    QFile fixture(QStringLiteral(PLASMA_LYRICS_FIXTURES_DIR "/netease-search-magic-cook.json"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const auto candidates = NeteaseProvider::parseSearchResponse(fixture.readAll());
    QCOMPARE(candidates.size(), 3);
    QCOMPARE(candidates.first().trackId, QStringLiteral("423776453"));
    QCOMPARE(candidates.first().lengthMs, 286066);
    // This fixture predates transNames; parsing it must not crash and must
    // leave alternateTitles empty rather than inventing anything.
    QVERIFY(candidates.first().alternateTitles.isEmpty());
}

void NeteaseProviderTest::parsesTransNamesIntoAlternateTitles()
{
    QFile fixture(QStringLiteral(PLASMA_LYRICS_FIXTURES_DIR "/netease-search-idol.json"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const auto candidates = NeteaseProvider::parseSearchResponse(fixture.readAll());
    QCOMPARE(candidates.size(), 4);

    const auto idol = std::find_if(candidates.cbegin(), candidates.cend(), [](const auto &candidate) {
        return candidate.trackId == QStringLiteral("2034742057");
    });
    QVERIFY(idol != candidates.cend());
    QCOMPARE(idol->alternateTitles, QStringList{QStringLiteral("偶像")});

    // D-6: alias is anime tie-in text ("TV动画《我推的孩子》片头曲"), not a
    // title translation, and must not leak into alternateTitles.
    QVERIFY(!idol->alternateTitles.contains(QStringLiteral("TV动画《我推的孩子》片头曲")));

    // 群青 (1472480890) has no transNames key at all in the real response --
    // parsing a missing field must not crash and must leave this empty.
    const auto gunjou = std::find_if(candidates.cbegin(), candidates.cend(), [](const auto &candidate) {
        return candidate.trackId == QStringLiteral("1472480890");
    });
    QVERIFY(gunjou != candidates.cend());
    QVERIFY(gunjou->alternateTitles.isEmpty());
}

void NeteaseProviderTest::handlesDirtyLyricAndEmptyWordData()
{
    QFile fixture(QStringLiteral(PLASMA_LYRICS_FIXTURES_DIR "/netease-lyric-dirty.json"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const auto document = NeteaseProvider::parseLyricResponse(fixture.readAll());
    QVERIFY(document.has_value());
    // Providers and the cache preserve the source response. Credit filtering
    // is a display policy applied by Resolver, so it can be disabled without
    // having already discarded data.
    QCOMPARE(document->lines.size(), 4);
    QCOMPARE(document->lines.first().text, QStringLiteral("作词: 初繁言"));
    QCOMPARE(document->lines.last().text, QStringLiteral("若能再相见"));
    QCOMPARE(*document->lines.last().translation, QStringLiteral("If we meet again"));
    QVERIFY(!document->hasWords);
}

void NeteaseProviderTest::creditsFromThePreV1EndpointStillFilter()
{
    // The provider asks for /api/song/lyric/v1, which marks credits structurally.
    // This fixture is the older route's real answer for the same song, kept
    // because cached rows and any future response shaped like it must still end
    // up showing the first real line rather than a staff list.
    QFile fixture(QStringLiteral(PLASMA_LYRICS_FIXTURES_DIR "/netease-lyric-old-endpoint.json"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const auto document = NeteaseProvider::parseLyricResponse(fixture.readAll());
    QVERIFY(document.has_value());
    QCOMPARE(document->lines.size(), 6);
    QCOMPARE(document->lines.first().text, QStringLiteral("作词 : 爆音常安"));

    const auto shown = filterLeadingCredits(document->lines);
    QCOMPARE(shown.size(), 2);
    QCOMPARE(shown.first().text, QStringLiteral("蝴蝶轻吻花瓣而颤动"));
}

void NeteaseProviderTest::timeoutStaircase()
{
    QCOMPARE(NeteaseProvider::timeoutForAttempt(4000, 1), 4000);
    QCOMPARE(NeteaseProvider::timeoutForAttempt(4000, 2), 6000);
    QCOMPARE(NeteaseProvider::timeoutForAttempt(4000, 3), 8000);
}

void NeteaseProviderTest::retriesNetworkErrorsAndCanRecover()
{
    ScriptedHttpServer server({ScriptedHttpServer::Reply::Disconnect,
                               ScriptedHttpServer::Reply::Disconnect,
                               ScriptedHttpServer::Reply::SearchSuccess});
    QVERIFY(server.start());
    NeteaseProvider provider(server.baseUrl(), 100);
    const auto result = searchAndWait(provider);
    QCOMPARE(server.requestCount(), 3);
    QCOMPARE(result.error, QString());
    QCOMPARE(result.candidates.size(), 1);
    QCOMPARE(result.candidates.first().trackId, QStringLiteral("1"));
}

void NeteaseProviderTest::retriesTruncated2xxResponsesAndCanRecover()
{
    // Receiving a successful status does not make a later transport failure
    // an HTTP error.  A short body with a larger Content-Length is reported by
    // QNetworkReply as RemoteHostClosedError and must take the retry path.
    ScriptedHttpServer server({ScriptedHttpServer::Reply::TruncatedSuccess,
                               ScriptedHttpServer::Reply::SearchSuccess});
    QVERIFY(server.start());
    NeteaseProvider provider(server.baseUrl(), 100);
    const auto result = searchAndWait(provider);
    QCOMPARE(server.requestCount(), 2);
    QCOMPARE(result.error, QString());
    QCOMPARE(result.candidates.size(), 1);
    QCOMPARE(result.candidates.first().trackId, QStringLiteral("1"));
}

void NeteaseProviderTest::givesUpAfterThreeNetworkTimeouts()
{
    ScriptedHttpServer server({ScriptedHttpServer::Reply::Hang});
    QVERIFY(server.start());
    NeteaseProvider provider(server.baseUrl(), 20);
    QElapsedTimer elapsed;
    elapsed.start();
    const auto result = searchAndWait(provider);
    QCOMPARE(server.requestCount(), 3);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.candidates.isEmpty());
    QVERIFY(elapsed.elapsed() >= 60);
    QVERIFY(elapsed.elapsed() < 1000);
}

void NeteaseProviderTest::doesNotRetryHttpErrors()
{
    for (const auto scriptedReply : {ScriptedHttpServer::Reply::NotFound,
                                     ScriptedHttpServer::Reply::ServerError}) {
        ScriptedHttpServer server({scriptedReply});
        QVERIFY(server.start());
        NeteaseProvider provider(server.baseUrl(), 100);
        const auto result = searchAndWait(provider);
        QCOMPARE(server.requestCount(), 1);
        QVERIFY(!result.error.isEmpty());
    }
}

void NeteaseProviderTest::doesNotRetryJsonErrors()
{
    ScriptedHttpServer server({ScriptedHttpServer::Reply::InvalidJson});
    QVERIFY(server.start());
    NeteaseProvider provider(server.baseUrl(), 100);
    const auto result = searchAndWait(provider);
    QCOMPARE(server.requestCount(), 1);
    QVERIFY(!result.error.isEmpty());
}

void NeteaseProviderTest::destroyingProviderCancelsPendingRequest()
{
    ScriptedHttpServer server({ScriptedHttpServer::Reply::Hang});
    QVERIFY(server.start());
    bool callbackCalled = false;
    {
        NeteaseProvider provider(server.baseUrl(), 1000);
        provider.search({QStringLiteral("song"), {}, {}, 0},
                        [&](ProviderSearchResult) { callbackCalled = true; });
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 500);
    }
    QTest::qWait(20);
    QVERIFY(!callbackCalled);
}

QTEST_GUILESS_MAIN(NeteaseProviderTest)
#include "tst_neteaseprovider.moc"
