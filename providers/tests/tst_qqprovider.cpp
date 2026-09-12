#include "providers/qq/qqprovider.h"
#include "providers/qq/qrccipher.h"

#include "core/lyric/timeline.h"
#include "core/match/matcher.h"

#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

using namespace PlasmaLyrics;

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QByteArray fixture(const QString &name)
{
    return readFile(QStringLiteral(PLASMA_LYRICS_FIXTURES_DIR) + QLatin1Char('/') + name);
}

// The decrypted counterparts live beside the core parser's tests. Reading
// them from here is what pins the cipher's output to the bytes that parser
// is tested against, rather than leaving two independent copies of the truth.
QByteArray decryptedFixture(const QString &name)
{
    return readFile(QStringLiteral(PLASMA_LYRICS_QRC_FIXTURES_DIR) + QLatin1Char('/') + name);
}

QString cdata(const QByteArray &payload, const QString &tag)
{
    const QString text = QString::fromUtf8(payload);
    const QRegularExpression expression(
        QStringLiteral(R"(<%1\b[^>]*><!\[CDATA\[(.*?)\]\]></%1>)").arg(tag),
        QRegularExpression::DotMatchesEverythingOption);
    const auto match = expression.match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

// A SYNTHESISED search reply -- unlike every qq-*.json fixture beside this
// test, which are all recorded responses. Used for the value combinations
// that cannot practically be recorded: is_filter -12 is a sporadic throttle
// (the C0 probe saw it 3 times in 12 requests at ~3 s spacing and 0 times in
// 16 at >=4 s, with no established rate model), so provoking it means
// hammering the endpoint for an unreliable payoff, and the req_code/is_filter
// cross-products below do not all occur naturally. The two rejections that
// ARE deterministic -- -2 and -9 -- are recorded instead, in
// qq-search-rejected-shape.json and qq-search-rejected-malformed.json.
// The body is irrelevant here: classification happens before any candidate
// is read.
QByteArray searchReply(int requestCode, int isFilter, qint64 sum = 0)
{
    return QStringLiteral(R"({"code":0,"music.search.SearchCgiService.DoSearchForQQMusicMobile":)"
                          R"({"code":%1,"data":{"meta":{"is_filter":%2,"sum":%3},"body":{"item_song":[]}}}})")
        .arg(requestCode).arg(isFilter).arg(sum).toUtf8();
}

// One scripted reply. A plain body is the common case; status covers HTTP
// failures and abort covers a transport failure, which the provider has to
// tell apart -- see §C2.3.3.
struct ScriptedReply {
    QByteArray body;
    int status = 200;
    bool abort = false;

    ScriptedReply(QByteArray value = {}) : body(std::move(value)) {}
    static ScriptedReply http(int code) { return ScriptedReply({}, code, false); }
    static ScriptedReply aborted() { return ScriptedReply({}, 0, true); }

private:
    ScriptedReply(QByteArray value, int code, bool cut)
        : body(std::move(value)), status(code), abort(cut) {}
};

class ScriptedHttpServer : public QTcpServer
{
public:
    explicit ScriptedHttpServer(QList<ScriptedReply> replies)
        : m_replies(std::move(replies))
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    m_pending[socket].append(socket->readAll());
                    const QByteArray &buffered = m_pending[socket];
                    const int headerEnd = buffered.indexOf("\r\n\r\n");
                    if (headerEnd < 0) {
                        return;
                    }
                    // The search is a POST, so the body has to be waited for
                    // as well; a reply sent on headers alone would race it.
                    const QByteArray headers = buffered.left(headerEnd);
                    int contentLength = 0;
                    for (const auto &line : headers.split('\n')) {
                        const QByteArray trimmed = line.trimmed();
                        if (trimmed.startsWith("Content-Length:")) {
                            contentLength = trimmed.mid(15).trimmed().toInt();
                        }
                    }
                    if (buffered.size() < headerEnd + 4 + contentLength) {
                        return;
                    }
                    m_requests.append(buffered);
                    // Drop the buffer before acting on the reply, as the
                    // netease harness does: aborting returns early, and a
                    // QTcpSocket address can be reused by a later connection,
                    // so leaving a stale entry keyed by it invites a request
                    // that was never sent.
                    m_pending.remove(socket);
                    const ScriptedReply reply = m_replies.value(m_requests.size() - 1,
                                                                m_replies.constLast());
                    if (reply.abort) {
                        // Dropped mid-exchange: reaches the provider as a
                        // transport failure, not as a server answer.
                        socket->abort();
                        return;
                    }
                    const QByteArray status = reply.status == 200
                        ? QByteArrayLiteral("200 OK")
                        : QByteArray::number(reply.status) + " Error";
                    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\n"
                                  "Content-Length: " + QByteArray::number(reply.body.size())
                                  + "\r\nConnection: close\r\n\r\n" + reply.body);
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
    int requestCount() const { return static_cast<int>(m_requests.size()); }
    QByteArray request(int index) const { return m_requests.value(index); }

private:
    QList<ScriptedReply> m_replies;
    QList<QByteArray> m_requests;
    QHash<QTcpSocket *, QByteArray> m_pending;
};

ProviderSearchResult searchAndWait(QqProvider &provider, const TrackQuery &query)
{
    std::optional<ProviderSearchResult> result;
    QEventLoop loop;
    provider.search(query, [&](ProviderSearchResult value) {
        result = std::move(value);
        loop.quit();
    });
    if (!result) {
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return result.value_or(ProviderSearchResult{{}, QStringLiteral("test timed out"), true});
}

ProviderFetchResult fetchAndWait(QqProvider &provider)
{
    std::optional<ProviderFetchResult> result;
    QEventLoop loop;
    provider.fetch(QStringLiteral("1"), [&](ProviderFetchResult value) {
        result = std::move(value);
        loop.quit();
    });
    if (!result) {
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return result.value_or(ProviderFetchResult{std::nullopt, QStringLiteral("test timed out"), true});
}

// A port nothing is listening on: bound and then closed, so the number is
// known to have been free and the connection is refused rather than hanging.
quint16 closedPort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost);
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

} // namespace

class QqProviderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // --- the cipher -------------------------------------------------------

    void keyScheduleMatchesTheReferenceVectors()
    {
        // Isolates two of the four deviations, because both live in the
        // schedule and nowhere else: the key read as two little-endian words,
        // and PC-2's D half indexed at (pos - 27) rather than (pos - 28). The
        // expected bytes are the upstream project's own published schedule.
        const auto schedule = QrcCipher::keySchedule(QrcCipher::key1(), false);
        const std::array<const char *, 16> expected{
            "400c26102808", "40a620140415", "c094268b00c0", "e0824200e201",
            "20d222320404", "a01152c80082", "244251046209", "075110721040",
            "0641494a8016", "0b4111054488", "0d0989081041", "132089c2c024",
            "190c80000e88", "50288c981011", "10a404434220", "d02c0400ca82"};
        for (int round = 0; round < 16; ++round) {
            const QByteArray actual(reinterpret_cast<const char *>(schedule[round].data()), 6);
            QCOMPARE(actual.toHex(), QByteArray(expected[round]));
        }
        // Decryption is the same rounds in reverse order.
        const auto reversed = QrcCipher::keySchedule(QrcCipher::key1(), true);
        for (int round = 0; round < 16; ++round) {
            QCOMPARE(reversed[round], schedule[15 - round]);
        }
    }

    void blockOutputMatchesTheReferenceImplementation()
    {
        // With the schedule pinned above, a single block pins the other two
        // deviations -- the two altered S-box entries (S2[23], S4[53]) and
        // the non-standard initial and inverse permutations -- since those
        // are the only remaining inputs to this result.
        const std::array<quint8, 8> input{0, 1, 2, 3, 4, 5, 6, 7};
        const auto single = QrcCipher::cryptBlock(input, QrcCipher::keySchedule(QrcCipher::key1(), true));
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(single.data()), 8).toHex(),
                 QByteArray("9a9c55b68d45f21f"));
        const auto tripled = QrcCipher::tripleDesBlock(input);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(tripled.data()), 8).toHex(),
                 QByteArray("60be2354cdfdf5f2"));
        // DO NOT DELETE THIS AS REDUNDANT. Under S2[23] -> the standard 14,
        // both vectors above still pass, so this is the only single-block
        // vector in this suite that catches that row. The block is not
        // special -- about half of all blocks would catch it (1018 of 2000
        // random ones, measured) -- 0x0001020304050607 simply is not one of
        // them, and neither is cryptBlock(zeroes) under key1-decrypt: only
        // the triple pass over zeroes catches it. Delete this and the patched
        // S-box is covered end to end only.
        const std::array<quint8, 8> zeroes{};
        const auto zeroed = QrcCipher::tripleDesBlock(zeroes);
        QCOMPARE(QByteArray(reinterpret_cast<const char *>(zeroed.data()), 8).toHex(),
                 QByteArray("d4f80aa34ca0b59c"));
    }

    // Data-driven rather than a loop: QVERIFY2 returns from the test
    // function, so a loop stops at the first failing payload and the later
    // ones are never exercised. Each payload has to fail independently for
    // "every payload catches every deviation" to be checkable here at all.
    void decryptsRealPayloadsExactly_data()
    {
        QTest::addColumn<QString>("response");
        QTest::addColumn<QString>("tag");
        QTest::addColumn<QString>("decrypted");
        QTest::newRow("hoshiloop content")
            << QStringLiteral("qq-lyric-eve-hoshiloop.xml") << QStringLiteral("content")
            << QStringLiteral("qq-qrc-hoshiloop.qrc");
        QTest::newRow("hoshiloop romanization")
            << QStringLiteral("qq-lyric-eve-hoshiloop.xml") << QStringLiteral("contentroma")
            << QStringLiteral("qq-qrc-hoshiloop-roma.qrc");
        QTest::newRow("budaoren content")
            << QStringLiteral("qq-lyric-msr-budaoren.xml") << QStringLiteral("content")
            << QStringLiteral("qq-qrc-budaoren.qrc");
        // The fourth payload exists because qq-qrc-wanwuyouling.qrc is used
        // by the parser tests and would otherwise have no recorded response
        // behind it in the repo at all. It has no census counterpart to
        // cross-check against, and the reason is the same false match the
        // matching tests pin: the census matched this track to qq_id
        // 719971180 -- 小十三's 万物有灵人有情, which has clen 0 -- rather than
        // to 洛天依's 万物有灵 recorded here.
        QTest::newRow("wanwuyouling content")
            << QStringLiteral("qq-lyric-wanwuyouling.xml") << QStringLiteral("content")
            << QStringLiteral("qq-qrc-wanwuyouling.qrc");
    }

    void decryptsRealPayloadsExactly()
    {
        QFETCH(QString, response);
        QFETCH(QString, tag);
        QFETCH(QString, decrypted);
        const QString hex = cdata(fixture(response), tag);
        QVERIFY(!hex.isEmpty());
        QString error;
        const auto plain = QrcCipher::decryptHex(hex.toLatin1(), &error);
        QVERIFY2(plain.has_value(), qPrintable(error));
        QCOMPARE(plain->toUtf8(), decryptedFixture(decrypted));
    }

    void rejectsPayloadsThatAreNotEncryptedQrc()
    {
        QString error;
        // A plain-text body: QByteArray::fromHex would quietly drop every
        // non-hex character and hand back a short, meaningless buffer.
        QVERIFY(!QrcCipher::decryptHex(QByteArrayLiteral("[00:00.00]plain lrc"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QrcCipher::decryptHex(QByteArrayLiteral(""), &error));
        QVERIFY(!QrcCipher::decryptHex(QByteArrayLiteral("abc"), &error));
        // Valid hex, valid block count, but not a zlib stream underneath.
        QVERIFY(!QrcCipher::decryptHex(QByteArrayLiteral("00112233445566778899aabbccddeeff"), &error));
    }

    // --- the search request ----------------------------------------------

    void buildsTheMeasuredSearchRequestShape()
    {
        const QByteArray body = QqProvider::buildSearchBody(QStringLiteral("万物有灵 洛天依"),
                                                            QStringLiteral("18014398509481984"));
        const auto document = QJsonDocument::fromJson(body);
        QVERIFY(document.isObject());
        const auto root = document.object();
        const auto comm = root.value(QStringLiteral("comm")).toObject();
        // Every one of these was required to get a non-empty result; the
        // request is rejected without the full block.
        QCOMPARE(comm.value(QStringLiteral("cv")).toInt(), 13020508);
        QCOMPARE(comm.value(QStringLiteral("v")).toInt(), 13020508);
        QCOMPARE(comm.value(QStringLiteral("ct")).toString(), QStringLiteral("11"));
        QCOMPARE(comm.value(QStringLiteral("tmeAppID")).toString(), QStringLiteral("qqmusic"));
        QCOMPARE(comm.value(QStringLiteral("format")).toString(), QStringLiteral("json"));
        QCOMPARE(comm.value(QStringLiteral("inCharset")).toString(), QStringLiteral("utf-8"));
        QCOMPARE(comm.value(QStringLiteral("outCharset")).toString(), QStringLiteral("utf-8"));
        QVERIFY(!comm.value(QStringLiteral("uid")).toString().isEmpty());
        // Present and non-empty is the whole requirement; the value is never
        // checked, which is why no device registration exists here.
        const QString qimei = comm.value(QStringLiteral("QIMEI36")).toString();
        QCOMPARE(qimei.size(), 36);
        QVERIFY(!qimei.isEmpty());

        const QString key = QStringLiteral("music.search.SearchCgiService.DoSearchForQQMusicMobile");
        const auto request = root.value(key).toObject();
        QCOMPARE(request.value(QStringLiteral("module")).toString(),
                 QStringLiteral("music.search.SearchCgiService"));
        // The desktop method is refused whatever else the request carries.
        QCOMPARE(request.value(QStringLiteral("method")).toString(),
                 QStringLiteral("DoSearchForQQMusicMobile"));
        const auto parameters = request.value(QStringLiteral("param")).toObject();
        QCOMPARE(parameters.value(QStringLiteral("query")).toString(),
                 QStringLiteral("万物有灵 洛天依"));
        QCOMPARE(parameters.value(QStringLiteral("searchid")).toString(),
                 QStringLiteral("18014398509481984"));
        QCOMPARE(parameters.value(QStringLiteral("search_type")).toInt(), 0);
        QCOMPARE(parameters.value(QStringLiteral("num_per_page")).toInt(), 20);
        QCOMPARE(parameters.value(QStringLiteral("page_num")).toInt(), 1);

        // Byte-for-byte reproducible for a given searchid.
        QCOMPARE(QqProvider::buildSearchBody(QStringLiteral("万物有灵 洛天依"),
                                             QStringLiteral("18014398509481984")),
                 body);
    }

    void generatesSearchIdsTheServerAccepts()
    {
        // A searchid whose top field is zero is rejected outright, so this is
        // a structured value rather than a nonce.
        for (int attempt = 0; attempt < 200; ++attempt) {
            bool ok = false;
            const quint64 value = QqProvider::generateSearchId().toULongLong(&ok);
            QVERIFY(ok);
            const quint64 leading = value / Q_UINT64_C(18014398509481984);
            QVERIFY(leading >= 1);
            QVERIFY(leading <= 20);
        }
    }

    // --- the search response ---------------------------------------------

    void readsCandidatesFromARealSearchResponse()
    {
        const auto response = QqProvider::parseSearchResponse(
            fixture(QStringLiteral("qq-search-wanwuyouling.json")));
        QCOMPARE(response.status, QqProvider::SearchStatus::Accepted);
        QCOMPARE(response.isFilter, 0);
        QCOMPARE(response.candidates.size(), 20);
        const auto &first = response.candidates.first();
        // The numeric id, not the mid: the lyric endpoint answers
        // musicid="0" when given a songmid.
        QCOMPARE(first.trackId, QStringLiteral("109359911"));
        QCOMPARE(first.title, QStringLiteral("万物有灵"));
        QCOMPARE(first.artists, QStringList({QStringLiteral("洛天依")}));
        // interval is seconds in the response and milliseconds here.
        QCOMPARE(first.lengthMs, 146000);
        for (const auto &candidate : response.candidates) {
            QVERIFY(!candidate.title.contains(QStringLiteral("<em>")));
        }
    }

    void separatesRejectionFromAGenuinelyEmptyResult()
    {
        // is_filter == 0 is the only state in which sum is worth believing.
        // Conflating a rejection with an empty result would make a retired
        // client version look exactly like a song nobody has heard of.
        QCOMPARE(QqProvider::parseSearchResponse(searchReply(0, 0, 0)).status,
                 QqProvider::SearchStatus::Accepted);
        QCOMPARE(QqProvider::parseSearchResponse(searchReply(0, -12)).status,
                 QqProvider::SearchStatus::Transient);
        // -2 is a rejected request form, -9 a malformed field; both must be
        // reported rather than swallowed, and so must any unseen negative --
        // a future version cutoff would plausibly arrive as one.
        QCOMPARE(QqProvider::parseSearchResponse(searchReply(0, -2)).status,
                 QqProvider::SearchStatus::ShapeRejected);
        QCOMPARE(QqProvider::parseSearchResponse(searchReply(0, -9)).status,
                 QqProvider::SearchStatus::ShapeRejected);
        QCOMPARE(QqProvider::parseSearchResponse(searchReply(0, -77)).status,
                 QqProvider::SearchStatus::ShapeRejected);
        QCOMPARE(QqProvider::parseSearchResponse(QByteArrayLiteral("not json")).status,
                 QqProvider::SearchStatus::Malformed);
        QCOMPARE(QqProvider::parseSearchResponse(QByteArrayLiteral("{}")).status,
                 QqProvider::SearchStatus::Malformed);
        // Right envelope, no meta at all.
        QCOMPARE(QqProvider::parseSearchResponse(
                     QByteArrayLiteral(R"({"music.search.SearchCgiService.DoSearchForQQMusicMobile":)"
                                       R"({"code":0,"data":{}}})")).status,
                 QqProvider::SearchStatus::Malformed);
    }

    void classifiesByIsFilterAndNeverByRequestCode()
    {
        // req_code 2001 has two unrelated causes and only is_filter tells
        // them apart. Reading the code instead would retry a permanent
        // method/comm mismatch until the retry budget ran out, every single
        // time, and report it as a throttle.
        QCOMPARE(QqProvider::parseSearchResponse(searchReply(2001, -12)).status,
                 QqProvider::SearchStatus::Transient);
        QCOMPARE(QqProvider::parseSearchResponse(searchReply(2001, -2)).status,
                 QqProvider::SearchStatus::ShapeRejected);
        // And the converse: an accepted response stays accepted whatever
        // req_code happens to say.
        QCOMPARE(QqProvider::parseSearchResponse(searchReply(2001, 0, 5)).status,
                 QqProvider::SearchStatus::Accepted);
    }

    void readsTheRecordedMalformedFieldRejection()
    {
        // Recorded, not synthesised. It came from a malformed searchid (its
        // top field must be 1..20) while QIMEI36 was perfectly valid, which is
        // why -9 cannot be read as "QIMEI36 is empty" specifically -- only as
        // "some field is malformed". req_code is 0 here: the code that would
        // look like success, which is why is_filter has to be consulted.
        const auto response = QqProvider::parseSearchResponse(
            fixture(QStringLiteral("qq-search-rejected-malformed.json")));
        QCOMPARE(response.status, QqProvider::SearchStatus::ShapeRejected);
        QCOMPARE(response.isFilter, -9);
        QCOMPARE(response.requestCode, 0);
        QVERIFY(response.candidates.isEmpty());
        QCOMPARE(response.total, 0);
    }

    void answersToAMethodWeNeverSendAreNotResults()
    {
        // The recorded -2 is keyed to DoSearchForQQMusicDesktop, because
        // producing a real -2 requires sending the desktop method with the old
        // comm block -- which is exactly the shape this provider never sends.
        // So -2 is not reachable through our own requests, and the recording's
        // value is the wire facts it pins rather than a path through the
        // classifier. What the classifier does with it is still worth fixing
        // in place: a reply answering a method we did not ask for is reported
        // as unreadable, never quietly as an empty result.
        const auto response = QqProvider::parseSearchResponse(
            fixture(QStringLiteral("qq-search-rejected-shape.json")));
        QCOMPARE(response.status, QqProvider::SearchStatus::Malformed);
        QVERIFY(response.candidates.isEmpty());
        QVERIFY(response.error.contains(QStringLiteral("DoSearchForQQMusicMobile")));

        // The wire facts the recording exists to pin.
        const auto meta = QJsonDocument::fromJson(
                              fixture(QStringLiteral("qq-search-rejected-shape.json"))).object()
                              .value(QStringLiteral("music.search.SearchCgiService.DoSearchForQQMusicDesktop"))
                              .toObject().value(QStringLiteral("data")).toObject()
                              .value(QStringLiteral("meta")).toObject();
        QCOMPARE(meta.value(QStringLiteral("is_filter")).toInt(), -2);
        QCOMPARE(meta.value(QStringLiteral("sum")).toInt(), 0);
    }

    void doesNotUseTheBodySchemaToDetectRejection()
    {
        // The body schema tracks which method/comm was SENT, not whether the
        // request was honoured, so it cannot stand in for is_filter.
        // Both recorded rejections prove it: the -2 one fell back to the old
        // desktop schema (no item_song) because the desktop method is what
        // caused it, while the -9 one carries item_song exactly like an
        // accepted response, because its method and comm were correct and
        // only a field value was wrong.
        const auto body = [](const QByteArray &payload, const QString &method) {
            return QJsonDocument::fromJson(payload).object()
                .value(QStringLiteral("music.search.SearchCgiService.") + method)
                .toObject().value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("body")).toObject();
        };
        const QString mobile = QStringLiteral("DoSearchForQQMusicMobile");
        const auto accepted = body(fixture(QStringLiteral("qq-search-wanwuyouling.json")), mobile);
        QVERIFY(accepted.contains(QStringLiteral("item_song")));
        QVERIFY(accepted.contains(QStringLiteral("item_album")));
        QVERIFY(accepted.contains(QStringLiteral("ambi_group")));

        const auto malformed = body(fixture(QStringLiteral("qq-search-rejected-malformed.json")), mobile);
        QVERIFY(malformed.contains(QStringLiteral("item_song")));

        const auto shape = body(fixture(QStringLiteral("qq-search-rejected-shape.json")),
                                QStringLiteral("DoSearchForQQMusicDesktop"));
        QVERIFY(!shape.contains(QStringLiteral("item_song")));
        QVERIFY(shape.contains(QStringLiteral("song")));
        QVERIFY(shape.contains(QStringLiteral("singer")));
    }

    void neverReadsEstimateSum()
    {
        // estimate_sum is unusable on a rejected response, and not in a way a
        // sanity check could catch: the -9 recording carries the obviously
        // absurd 140200128144376, but the -2 recording carries 9129, which
        // looks entirely plausible next to a real result count. Only sum, and
        // only once is_filter says the request was processed.
        const auto value = [](const QByteArray &payload, const QString &method) {
            return QJsonDocument::fromJson(payload).object()
                .value(QStringLiteral("music.search.SearchCgiService.") + method)
                .toObject().value(QStringLiteral("data")).toObject()
                .value(QStringLiteral("meta")).toObject()
                .value(QStringLiteral("estimate_sum")).toDouble();
        };
        QVERIFY(value(fixture(QStringLiteral("qq-search-rejected-malformed.json")),
                      QStringLiteral("DoSearchForQQMusicMobile")) > 1e12);
        QCOMPARE(value(fixture(QStringLiteral("qq-search-rejected-shape.json")),
                       QStringLiteral("DoSearchForQQMusicDesktop")), 9129.0);
        // Whatever the classifier makes of either recording, total comes from
        // sum and never from estimate_sum.
        const auto malformed = QqProvider::parseSearchResponse(
            fixture(QStringLiteral("qq-search-rejected-malformed.json")));
        QCOMPARE(malformed.status, QqProvider::SearchStatus::ShapeRejected);
        QCOMPARE(malformed.total, 0);
    }

    void retriesTheTransientThrottleAndSucceeds()
    {
        ScriptedHttpServer server({searchReply(2001, -12),
                                   fixture(QStringLiteral("qq-search-wanwuyouling.json"))});
        QVERIFY(server.start());
        QqProvider provider(server.baseUrl(), server.baseUrl(), 2000, 0);
        const auto result = searchAndWait(
            provider, {QStringLiteral("万物有灵"), {QStringLiteral("洛天依")}, QString(), 146000});
        QCOMPARE(server.requestCount(), 2);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.candidates.size(), 20);
        // The request really is the measured shape on the wire, body included.
        QVERIFY(server.request(0).contains("DoSearchForQQMusicMobile"));
        QVERIFY(server.request(0).contains("QIMEI36"));
    }

    void givesUpAfterTheRetryBudget()
    {
        ScriptedHttpServer server({searchReply(2001, -12)});
        QVERIFY(server.start());
        QqProvider provider(server.baseUrl(), server.baseUrl(), 2000, 0);
        const auto result = searchAndWait(provider, {QStringLiteral("song"), {}, QString(), 0});
        QCOMPARE(server.requestCount(), 3);
        QVERIFY(result.candidates.isEmpty());
        QVERIFY(!result.error.isEmpty());
        // Not a transport failure: the server answered every time.
        QVERIFY(!result.transportFailed);
    }

    void doesNotRetryARejectedRequestForm()
    {
        ScriptedHttpServer server({searchReply(0, -2)});
        QVERIFY(server.start());
        QqProvider provider(server.baseUrl(), server.baseUrl(), 2000, 0);
        // The log line is the point of the whole distinction: a retired
        // client version surfaces here and nowhere else, so pin its text and
        // its level, not just the returned status.
        QTest::ignoreMessage(QtWarningMsg,
                             "search request form was rejected: is_filter=-2 req_code=0 -- "
                             "the pinned client version may have been retired");
        const auto result = searchAndWait(provider, {QStringLiteral("song"), {}, QString(), 0});
        QCOMPARE(server.requestCount(), 1);
        QVERIFY(result.candidates.isEmpty());
        QVERIFY(result.error.contains(QStringLiteral("rejected")));
    }

    void reportsNoResultsWithoutAnError()
    {
        ScriptedHttpServer server({searchReply(0, 0, 0)});
        QVERIFY(server.start());
        QqProvider provider(server.baseUrl(), server.baseUrl(), 2000, 0);
        const auto result = searchAndWait(provider, {QStringLiteral("song"), {}, QString(), 0});
        QCOMPARE(server.requestCount(), 1);
        QVERIFY(result.candidates.isEmpty());
        // An accepted request that found nothing is not a failure, and must
        // not be reported as one.
        QVERIFY(result.error.isEmpty());
        QVERIFY(!result.transportFailed);
    }

    // --- transport and HTTP failures --------------------------------------

    void doesNotRetryAnHttpFailure()
    {
        // A non-2xx answer is the server's considered response, not a
        // transport fault: one request, no retry, and transportFailed stays
        // false so the resolver caches the miss normally.
        ScriptedHttpServer server({ScriptedReply::http(500)});
        QVERIFY(server.start());
        QqProvider provider(server.baseUrl(), server.baseUrl(), 2000, 0);
        const auto result = searchAndWait(provider, {QStringLiteral("song"), {}, QString(), 0});
        QCOMPARE(server.requestCount(), 1);
        QVERIFY(result.candidates.isEmpty());
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!result.transportFailed);
    }

    void retriesADroppedConnectionAndReportsItAsTransport()
    {
        // Exhausts kMaximumTransportAttempts, which is the guard on
        // sendAttempt's hand-rolled self-recursion.
        ScriptedHttpServer server({ScriptedReply::aborted()});
        QVERIFY(server.start());
        QqProvider provider(server.baseUrl(), server.baseUrl(), 2000, 0);
        const auto result = searchAndWait(provider, {QStringLiteral("song"), {}, QString(), 0});
        QCOMPARE(server.requestCount(), 3);
        QVERIFY(result.candidates.isEmpty());
        QVERIFY(result.transportFailed);
    }

    void recoversWhenOnlyTheFirstConnectionDrops()
    {
        ScriptedHttpServer server({ScriptedReply::aborted(),
                                   ScriptedReply(fixture(QStringLiteral("qq-search-wanwuyouling.json")))});
        QVERIFY(server.start());
        QqProvider provider(server.baseUrl(), server.baseUrl(), 2000, 0);
        const auto result = searchAndWait(
            provider, {QStringLiteral("万物有灵"), {QStringLiteral("洛天依")}, QString(), 146000});
        QCOMPARE(server.requestCount(), 2);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.candidates.size(), 20);
    }

    void reportsARefusedConnectionAsTransport()
    {
        const QUrl dead = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(closedPort()));
        QqProvider provider(dead, dead, 2000, 0);
        const auto result = searchAndWait(provider, {QStringLiteral("song"), {}, QString(), 0});
        QVERIFY(result.candidates.isEmpty());
        QVERIFY(result.transportFailed);
    }

    void fetchSeparatesHttpFailureFromTransportFailure()
    {
        // fetch() shares sendAttempt with search() but was never driven over
        // the wire, so neither half of the split was covered on this path.
        {
            ScriptedHttpServer server({ScriptedReply::http(404)});
            QVERIFY(server.start());
            QqProvider provider(server.baseUrl(), server.baseUrl(), 2000, 0);
            const auto result = fetchAndWait(provider);
            QCOMPARE(server.requestCount(), 1);
            QVERIFY(!result.document.has_value());
            QVERIFY(!result.transportFailed);
        }
        {
            ScriptedHttpServer server({ScriptedReply::aborted()});
            QVERIFY(server.start());
            QqProvider provider(server.baseUrl(), server.baseUrl(), 2000, 0);
            const auto result = fetchAndWait(provider);
            // Four, not kMaximumTransportAttempts. fetch is a GET, and Qt
            // retries an idempotent request once on its own when the
            // connection closes without a response, so the server sees one
            // more request than the provider makes attempts. Measured both
            // ways: the provider logs attempt=1/3..3/3 here, while a POST
            // search against the same scripted abort produces exactly 3
            // connections. Asserting 3 here would be asserting the provider's
            // bookkeeping rather than what actually reaches the network.
            QCOMPARE(server.requestCount(), 4);
            QVERIFY(!result.document.has_value());
            QVERIFY(result.transportFailed);
        }
    }

    void fetchParsesARealResponseOverTheWire()
    {
        ScriptedHttpServer server({ScriptedReply(fixture(QStringLiteral("qq-lyric-msr-budaoren.xml")))});
        QVERIFY(server.start());
        QqProvider provider(server.baseUrl(), server.baseUrl(), 2000, 0);
        const auto result = fetchAndWait(provider);
        QCOMPARE(server.requestCount(), 1);
        QVERIFY(result.error.isEmpty());
        QVERIFY(result.document.has_value());
        QVERIFY(result.document->hasWords);
        QCOMPARE(result.document->lines.size(), 30);
        // The request really carried the documented lyric-endpoint shape.
        QVERIFY(server.request(0).contains("lyric_download.fcg"));
        QVERIFY(server.request(0).contains("lrctype=4"));
        QVERIFY(server.request(0).contains("musicid=1"));
    }

    // --- matching ---------------------------------------------------------

    void resolvesTheRightArtistOutOfARealSearch()
    {
        const auto response = QqProvider::parseSearchResponse(
            fixture(QStringLiteral("qq-search-wanwuyouling.json")));
        const TrackQuery query{QStringLiteral("万物有灵"), {QStringLiteral("洛天依")},
                               QString(), 146000};
        const auto ranked = rankCandidates(query, response.candidates);
        const auto chosen = chooseMatch(ranked, false);
        QVERIFY(chosen.has_value());
        QCOMPARE(chosen->candidate.trackId, QStringLiteral("109359911"));
        QCOMPARE(chosen->candidate.artists, QStringList({QStringLiteral("洛天依")}));
        // The response also holds a same-titled track by a different artist;
        // it must not be the one picked.
        QVERIFY(std::any_of(response.candidates.cbegin(), response.candidates.cend(),
                            [](const Candidate &candidate) {
                                return candidate.trackId == QStringLiteral("723179888");
                            }));
    }

    void rejectsTheCensusFalseMatchOnTotalRatherThanTitle()
    {
        // The census's one false match: a different song by a different
        // artist whose title merely contains the query's. It must not be
        // accepted -- but it is worth being exact about WHY, because the
        // obvious reading is wrong. textSimilarity's containment fast path
        // scores 4/7 = 0.571, which CLEARS the 0.55 title gate; containment
        // is not disallowed here. What rejects this candidate is the artist
        // score of 0 dragging the total under the 0.58 bar. Pinning both
        // numbers keeps the next reader from mistaking the title gate for
        // the protection and quietly removing the one that matters.
        const TrackQuery query{QStringLiteral("万物有灵"), {QStringLiteral("洛天依")},
                               QString(), 146000};
        Candidate wrong;
        wrong.trackId = QStringLiteral("999");
        wrong.title = QStringLiteral("万物有灵人有情");
        wrong.artists = QStringList({QStringLiteral("小十三")});
        wrong.lengthMs = 251000;
        const auto ranked = rankCandidates(query, {wrong});
        const auto &score = ranked.first().score;
        QVERIFY(score.title >= 0.55);
        QVERIFY(score.total < 0.58);
        QCOMPARE(candidateRejectionReason(ranked.first()), QStringLiteral("total-threshold"));
        QVERIFY(!chooseMatch(ranked, false).has_value());

        // And the worst shape the census could have produced: the same
        // containment match with the artist score maxed out. Still rejected,
        // but only just -- 0.486 against the 0.58 bar.
        Candidate sameArtist = wrong;
        sameArtist.artists = QStringList({QStringLiteral("洛天依")});
        const auto rankedSameArtist = rankCandidates(query, {sameArtist});
        QVERIFY(rankedSameArtist.first().score.title >= 0.55);
        QVERIFY(rankedSameArtist.first().score.artists == 1.0);
        QVERIFY(rankedSameArtist.first().score.total < 0.58);
        QVERIFY(!chooseMatch(rankedSameArtist, false).has_value());
    }

    void doesNotMatchAnUnrelatedResultSet()
    {
        // A query the catalogue does not hold still comes back with loosely
        // related songs rather than nothing at all.
        const auto response = QqProvider::parseSearchResponse(
            fixture(QStringLiteral("qq-search-unrelated.json")));
        QCOMPARE(response.status, QqProvider::SearchStatus::Accepted);
        QVERIFY(!response.candidates.isEmpty());
        const TrackQuery query{QStringLiteral("zzqxwvunonsensequery12345"), {QStringLiteral("nobody")},
                               QString(), 200000};
        QVERIFY(!chooseMatch(rankCandidates(query, response.candidates), false).has_value());
    }

    // --- the lyric response ----------------------------------------------

    void buildsAWordLevelDocumentFromARealResponse()
    {
        QString error;
        const auto document = QqProvider::parseLyricResponse(
            fixture(QStringLiteral("qq-lyric-eve-hoshiloop.xml")), &error);
        QVERIFY2(document.has_value(), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(document->hasWords);
        QCOMPARE(document->lines.size(), 68);
        QCOMPARE(document->lines.at(5).text, QStringLiteral("銀河の隅で 惑星はグルグル周る"));
        QCOMPARE(*document->lines.at(5).translation, QStringLiteral("行星在银河一隅 一圈圈地旋转"));
        QCOMPARE(*document->lines.at(5).words->at(0).romanization, QStringLiteral("gin"));
        QVERIFY(document->lines.at(5).romanization.has_value());
        // The title and credit lines arrive as ordinary timed body lines and
        // must not reach the display.
        const auto shown = filterLeadingCredits(document->lines);
        QCOMPARE(shown.size(), 65);
        QVERIFY(!shown.first().text.contains(QStringLiteral("Eve")));
    }

    void keepsTranslationWithoutRomanization()
    {
        QString error;
        const auto document = QqProvider::parseLyricResponse(
            fixture(QStringLiteral("qq-lyric-msr-budaoren.xml")), &error);
        QVERIFY2(document.has_value(), qPrintable(error));
        QVERIFY(document->hasWords);
        QCOMPARE(document->lines.size(), 30);
        const auto shown = filterLeadingCredits(document->lines);
        QCOMPARE(shown.size(), 26);
        QCOMPARE(shown.first().text, QStringLiteral("Ex tenuī filo lucis texor"));
        QCOMPARE(*shown.first().translation, QStringLiteral("我由一缕细光织就"));
        QVERIFY(!shown.first().romanization.has_value());
    }

    void treatsAnInstrumentalPlaceholderAsNoLyrics()
    {
        // scroll="0" means plain text rather than encrypted QRC, and this
        // track's whole "lyric" is QQ's instrumental notice. Rendering it
        // would put that sentence on screen as the song's only line.
        QString error;
        const auto document = QqProvider::parseLyricResponse(
            fixture(QStringLiteral("qq-lyric-instrumental.xml")), &error);
        QVERIFY(document.has_value());
        QVERIFY(error.isEmpty());
        QVERIFY(document->lines.isEmpty());
    }

    void fallsBackToLineLevelLyricsWhenThereIsNoQrc()
    {
        // The documented degradation: a scroll="0" response carries ordinary
        // LRC in the same field, and the whole song must still work.
        const QByteArray response = QByteArrayLiteral(
            "<!--\n<cmd value=\"1031\"><result>0</result><reason>success</reason>"
            "<lyric musicid=\"1\" scroll=\"0\" encode=\"1\">"
            "<content type=\"file\"><![CDATA[[00:01.00]first line\n[00:05.00]second line]]></content>"
            "<contentts type=\"file\"><![CDATA[[00:01.00]translated]]></contentts>"
            "<contentroma type=\"file\"><![CDATA[]]></contentroma>"
            "</lyric></cmd>\n-->");
        QString error;
        const auto document = QqProvider::parseLyricResponse(response, &error);
        QVERIFY2(document.has_value(), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(document->lines.size(), 2);
        QVERIFY(!document->hasWords);
        QCOMPARE(document->lines.first().text, QStringLiteral("first line"));
        QCOMPARE(*document->lines.first().translation, QStringLiteral("translated"));
    }

    void reportsNothingForATrackWithAnEmptyBody()
    {
        // Seen in the census: scroll says QRC exists, the payload is empty.
        const QByteArray response = QByteArrayLiteral(
            "<!--<lyric musicid=\"1\" scroll=\"4\"><content><![CDATA[]]></content></lyric>-->");
        QString error;
        const auto document = QqProvider::parseLyricResponse(response, &error);
        QVERIFY(document.has_value());
        QVERIFY(document->lines.isEmpty());
        QVERIFY(error.isEmpty());
    }

    void fallsBackToInspectingThePayloadWhenScrollIsMissing()
    {
        // Never seen in the wild, but the alternative is reading a lyric
        // element without the attribute as "no lyric element" and losing a
        // song that is right there in the response.
        const QByteArray plain = QByteArrayLiteral(
            "<!--<lyric musicid=\"1\" encode=\"1\">"
            "<content><![CDATA[[00:01.00]first line]]></content></lyric>-->");
        QString error;
        const auto lrc = QqProvider::parseLyricResponse(plain, &error);
        QVERIFY2(lrc.has_value(), qPrintable(error));
        QCOMPARE(lrc->lines.size(), 1);
        QVERIFY(!lrc->hasWords);

        const QString hex = cdata(fixture(QStringLiteral("qq-lyric-msr-budaoren.xml")),
                                  QStringLiteral("content"));
        const QByteArray encrypted = "<!--<lyric musicid=\"1\" encode=\"1\"><content><![CDATA["
            + hex.toLatin1() + "]]></content></lyric>-->";
        error.clear();
        const auto qrc = QqProvider::parseLyricResponse(encrypted, &error);
        QVERIFY2(qrc.has_value(), qPrintable(error));
        QVERIFY(qrc->hasWords);
        QCOMPARE(qrc->lines.size(), 30);
    }

    void reportsAResponseThatIsNotALyricAtAll()
    {
        QString error;
        QVERIFY(!QqProvider::parseLyricResponse(QByteArrayLiteral("<!--<cmd><reason>no such song"
                                                                 "</reason></cmd>-->"), &error));
        QVERIFY(error.contains(QStringLiteral("no such song")));
        error.clear();
        QVERIFY(!QqProvider::parseLyricResponse(QByteArrayLiteral(""), &error));
        QVERIFY(!error.isEmpty());
    }

    void keepsTheSongWhenScrollAndTheBodyDisagreeEitherWay()
    {
        // scroll was right on all 22 measured samples, but it predicts rather
        // than guarantees, and BOTH ways of being wrong used to cost the whole
        // song, which §C2.3.10 forbids. Claiming a QRC over a plain body
        // failed to decrypt; claiming plain over a hex body was worse -- it
        // parsed to zero LRC lines and returned an empty document with no
        // error at all, so the song vanished silently. Both directions are
        // asserted here because only testing the first would leave the
        // direction this test exists for uncovered.
        const QByteArray response = QByteArrayLiteral(
            "<!--<lyric musicid=\"1\" scroll=\"4\" encode=\"1\">"
            "<content><![CDATA[[00:01.00]first line\n[00:05.00]second line]]></content>"
            "<contentts><![CDATA[[00:01.00]translated]]></contentts></lyric>-->");
        QString error;
        const auto document = QqProvider::parseLyricResponse(response, &error);
        QVERIFY2(document.has_value(), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(document->lines.size(), 2);
        QVERIFY(!document->hasWords);
        QCOMPARE(*document->lines.first().translation, QStringLiteral("translated"));
        // The instrumental notice is still recognised down this path.
        const QByteArray instrumental = QByteArrayLiteral(
            "<!--<lyric musicid=\"1\" scroll=\"4\" encode=\"1\"><content><![CDATA["
            "[00:00:00]此歌曲为没有填词的纯音乐，请您欣赏]]></content></lyric>-->");
        const auto none = QqProvider::parseLyricResponse(instrumental, &error);
        QVERIFY(none.has_value());
        QVERIFY(none->lines.isEmpty());

        // The other direction: scroll says plain, the body is a real
        // encrypted QRC. This is the case that used to be lost without even
        // an error to show for it.
        const QByteArray hex =
            cdata(fixture(QStringLiteral("qq-lyric-msr-budaoren.xml")), QStringLiteral("content"))
                .toLatin1();
        const QByteArray mislabelled = "<!--<lyric musicid=\"1\" scroll=\"0\" encode=\"1\">"
            "<content><![CDATA[" + hex + "]]></content></lyric>-->";
        error.clear();
        const auto recovered = QqProvider::parseLyricResponse(mislabelled, &error);
        QVERIFY2(recovered.has_value(), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(recovered->hasWords);
        QCOMPARE(recovered->lines.size(), 30);
        QCOMPARE(recovered->lines.first().text,
                 QStringLiteral("布道人 (音律联觉-昔时我见布道人主题曲) - 塞壬唱片-MSR/z1on/ReStudio"));
    }

    void reportsAQrcPayloadThatCannotBeDecrypted()
    {
        const QByteArray response = QByteArrayLiteral(
            "<!--<lyric musicid=\"1\" scroll=\"4\">"
            "<content><![CDATA[00112233445566778899aabbccddeeff]]></content></lyric>-->");
        QString error;
        QVERIFY(!QqProvider::parseLyricResponse(response, &error));
        QVERIFY(error.contains(QStringLiteral("decrypt")));
    }

    // --- provider plumbing ------------------------------------------------

    void exposesItsIdentityAndCacheVersion()
    {
        QqProvider provider;
        QCOMPARE(provider.id(), QStringLiteral("qq"));
        QVERIFY(provider.isConfigured());
        QVERIFY(provider.cacheVersion().startsWith(QStringLiteral("qq:")));
        // Both hosts take part: changing either invalidates cached misses.
        QqProvider other(QUrl(QStringLiteral("https://u.example.test")),
                         QUrl(QStringLiteral("https://c.y.qq.com")));
        QVERIFY(other.cacheVersion() != provider.cacheVersion());
        QqProvider unconfigured{QUrl(), QUrl()};
        QVERIFY(!unconfigured.isConfigured());
    }
};

QTEST_GUILESS_MAIN(QqProviderTest)
#include "tst_qqprovider.moc"
