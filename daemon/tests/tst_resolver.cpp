#include "daemon/src/resolver.h"

#include "core/store/lyricstore.h"

#include <QDateTime>
#include <QEventLoop>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>

#include <algorithm>

using namespace PlasmaLyrics;

namespace {

class TestProvider final : public Provider
{
public:
    TestProvider(QString providerId, QString searchError = {},
                 bool searchTransportFailed = false, QString fetchError = {},
                 bool fetchTransportFailed = false)
        : m_id(std::move(providerId))
        , m_searchError(std::move(searchError))
        , m_searchTransportFailed(searchTransportFailed)
        , m_fetchError(std::move(fetchError))
        , m_fetchTransportFailed(fetchTransportFailed)
    {
    }

    QString id() const override { return m_id; }
    bool isConfigured() const override { return true; }
    void search(const TrackQuery &query, SearchCallback callback) override
    {
        ++m_searchCount;
        if (!m_searchError.isEmpty()) {
            callback({{}, m_searchError, m_searchTransportFailed});
            return;
        }
        callback({{{QStringLiteral("track"), query.title, query.artists, query.album, query.lengthMs}}, {}});
    }
    void fetch(const QString &, FetchCallback callback) override
    {
        if (!m_fetchError.isEmpty()) {
            callback({std::nullopt, m_fetchError, m_fetchTransportFailed});
            return;
        }
        callback({LyricDocument{{{1000, 2000, QStringLiteral("line"), std::nullopt, std::nullopt}}, 0, false}, {}});
    }
    int searchCount() const { return m_searchCount; }

private:
    QString m_id;
    QString m_searchError;
    bool m_searchTransportFailed = false;
    QString m_fetchError;
    bool m_fetchTransportFailed = false;
    int m_searchCount = 0;
};

QStringList *capturedMessages = nullptr;

void captureMessages(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (capturedMessages && (type == QtInfoMsg || type == QtWarningMsg)) {
        capturedMessages->append(message);
    }
}

class MessageCapture
{
public:
    MessageCapture()
        : m_previous(qInstallMessageHandler(captureMessages))
    {
        capturedMessages = &m_messages;
    }

    ~MessageCapture()
    {
        capturedMessages = nullptr;
        qInstallMessageHandler(m_previous);
    }

    const QStringList &messages() const { return m_messages; }

private:
    QStringList m_messages;
    QtMessageHandler m_previous;
};

// qa-1: every fixture above echoes the query back as a single exact-match
// candidate, so isAcceptableMatch(ranked.first()) always wins on the primary
// path -- none of them can tell whether Resolver::resolve's
// state.platform == "apple" wiring into chooseMatch's fallback is even
// reachable, let alone reversed (e.g. "==" typo'd as "!="). This provider
// returns a fixed pool (the measured data from
// core/tests/tst_matcher.cpp's romanizedTitleRescuedByLocalizedFallback)
// whose titles don't match a "Gunjou" query well enough for the primary
// path, so only the platform-gated fallback can resolve it.
class LocalizedFallbackProvider final : public Provider
{
public:
    QString id() const override { return QStringLiteral("localized-fallback"); }
    bool isConfigured() const override { return true; }
    void search(const TrackQuery &, SearchCallback callback) override
    {
        callback({{
            {QStringLiteral("1875383422"), QStringLiteral("Gunjou (Yoasobi)"), {QStringLiteral("Vangakuz")}, QString(), 243941},
            {QStringLiteral("2042876595"), QStringLiteral("Gunjou (8-Bit YOASOBI Emulation)"), {QStringLiteral("8-Bit Arcade")}, QString(), 246061},
            {QStringLiteral("1472480890"), QStringLiteral("群青"), {QStringLiteral("YOASOBI")}, QString(), 248444},
            {QStringLiteral("3389005906"), QStringLiteral("オリオン"), {QStringLiteral("YOASOBI")}, QString(), 206586},
            {QStringLiteral("1459343252"), QStringLiteral("たぶん"), {QStringLiteral("YOASOBI")}, QString(), 258666,
             {QStringLiteral("大概")}},
            {QStringLiteral("2042879549"), QStringLiteral("Gunjou (Originally Performed by YOASOBI)"),
             {QStringLiteral("Backing Business")}, QString(), 249324},
            {QStringLiteral("3323596738"), QStringLiteral("GUNJOU (Cover)"), {QStringLiteral("Omnixor")}, QString(), 262153},
            {QStringLiteral("2083182016"), QStringLiteral("勇者"), {QStringLiteral("YOASOBI")}, QString(), 194164},
            {QStringLiteral("2034742057"), QStringLiteral("アイドル"), {QStringLiteral("YOASOBI")}, QString(), 213233,
             {QStringLiteral("偶像")}},
            {QStringLiteral("1803908863"), QStringLiteral("怪物"), {QStringLiteral("YOASOBI")}, QString(), 206000}}, {}});
    }
    void fetch(const QString &, FetchCallback callback) override
    {
        callback({LyricDocument{{{0, 1000, QStringLiteral("line"), std::nullopt, std::nullopt}}, 0, false}, {}});
    }
};

class DeferredProvider final : public Provider
{
public:
    QString id() const override { return QStringLiteral("deferred"); }
    bool isConfigured() const override { return true; }
    void search(const TrackQuery &query, SearchCallback callback) override
    {
        m_searches.insert(query.title, std::move(callback));
    }
    void fetch(const QString &, FetchCallback callback) override
    {
        callback({LyricDocument{{{0, 1000, QStringLiteral("line"), std::nullopt, std::nullopt}}, 0, false}, {}});
    }
    void complete(const QString &title)
    {
        auto callback = m_searches.take(title);
        callback({{{title, title, {QStringLiteral("artist")}, QString(), 1000}}, {}});
    }

private:
    QHash<QString, SearchCallback> m_searches;
};

ResolvedLyric resolveSynchronously(Resolver &resolver, const MprisState &state)
{
    std::optional<ResolvedLyric> result;
    QEventLoop loop;
    const auto connection = QObject::connect(
        &resolver, &Resolver::resolved, &loop,
        [&](const QString &fingerprint, const ResolvedLyric &lyric) {
            if (fingerprint == state.fingerprint) {
                result = lyric;
                loop.quit();
            }
        });
    resolver.resolve(state);
    if (!result) {
        QTimer::singleShot(1000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    QObject::disconnect(connection);
    return result.value_or(ResolvedLyric{QStringLiteral("test-timeout"), std::nullopt, {}});
}

} // namespace

class ResolverTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void reproducesWaylyricsCacheKey()
    {
        MprisState state;
        state.title = QStringLiteral("老街北");
        state.artists = {QStringLiteral("闹闹丶/FFF君/欧Ωhm/洛天依Official")};
        state.album = QStringLiteral("老街北");
        state.lengthUs = 227708345;
        QCOMPARE(Resolver::legacyWaylyricsIds(state).first(),
                 QStringLiteral("29666bc02ed4053200633fe4eae4090b"));
    }

    void importedCacheWinsOverAnEarlierMissAndPreservesRawCredits()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());

        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:test");
        state.title = QStringLiteral("老街北");
        state.artists = {QStringLiteral("闹闹丶/FFF君/欧Ωhm/洛天依Official")};
        state.album = QStringLiteral("老街北");
        state.lengthUs = 227708345;
        const TrackRef imported{QStringLiteral("waylyrics"),
                                Resolver::legacyWaylyricsIds(state).first(), 1.0};
        const LyricDocument raw{{{0, 1000, QStringLiteral("作词：Example"), std::nullopt, std::nullopt},
                                 {28630, 31620, QStringLiteral("若能再相见"), std::nullopt, std::nullopt}},
                                0, false};
        QVERIFY(store.putLyric(imported, raw));
        QVERIFY(store.recordMiss(state.fingerprint, QStringLiteral("no-candidate")));

        Resolver resolver(store, {}, true);
        const auto result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.ref->provider, QStringLiteral("waylyrics"));
        QCOMPARE(result.document.lines.size(), 1);
        QCOMPARE(result.document.lines.first().text, QStringLiteral("若能再相见"));

        const auto cached = store.lyric(imported);
        QVERIFY(cached.has_value());
        QCOMPARE(cached->lines.size(), 2);
        QCOMPARE(cached->lines.first().text, QStringLiteral("作词：Example"));
    }

    void continuesToTheNextProviderAfterNetworkFailure()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider failed(QStringLiteral("failed"), QStringLiteral("simulated network failure"), true);
        TestProvider working(QStringLiteral("working"));
        Resolver resolver(store, {&failed, &working});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:provider-fallback");
        state.title = QStringLiteral("song");
        state.artists = {QStringLiteral("artist")};
        state.album = QStringLiteral("album");
        state.lengthUs = 120000000;

        const auto result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.ref->provider, QStringLiteral("working"));
    }

    void networkFailureHasItsOwnStateAndShortNegativeCache()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider failed(QStringLiteral("failed"), QStringLiteral("simulated network failure"), true);
        Resolver resolver(store, {&failed});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:network-error");
        state.title = QStringLiteral("song");
        state.artists = {QStringLiteral("artist")};
        state.lengthUs = 120000000;

        QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("network-error"));
        const auto miss = store.freshMiss(state.fingerprint);
        QVERIFY(miss.has_value());
        QCOMPARE(miss->reason, QStringLiteral("network"));
        QCOMPARE(failed.searchCount(), 1);
        QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("network-error"));
        QCOMPARE(failed.searchCount(), 1);

        QVERIFY(store.recordMiss(state.fingerprint, QStringLiteral("network"),
                                 QDateTime::currentSecsSinceEpoch() - 301));
        QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("network-error"));
        QCOMPARE(failed.searchCount(), 2);
    }

    void noCandidateMissKeepsSevenDayTtl()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider provider(QStringLiteral("unused"));
        Resolver resolver(store, {&provider});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:no-candidate-cache");
        state.title = QStringLiteral("song");

        QVERIFY(store.recordMiss(state.fingerprint, QStringLiteral("no-candidate"),
                                 QDateTime::currentSecsSinceEpoch() - 301));
        QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("not-found"));
        QCOMPARE(provider.searchCount(), 0);
    }

    void localizedFallbackOnlyAppliesWhenPlatformIsApple()
    {
        // Same candidate pool, same query, only state.platform differs --
        // proves the wiring actually reaches chooseMatch's fallback branch
        // and actually gates on "apple", rather than always taking (or
        // always skipping) the primary-path-only behavior.
        auto makeState = [](const QString &fingerprint, const QString &platform) {
            MprisState state;
            state.music = true;
            state.fingerprint = fingerprint;
            state.title = QStringLiteral("Gunjou");
            state.artists = {QStringLiteral("YOASOBI")};
            state.lengthUs = 248444000;
            state.platform = platform;
            return state;
        };

        {
            QTemporaryDir directory;
            QVERIFY(directory.isValid());
            LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
            QVERIFY(store.open());
            LocalizedFallbackProvider provider;
            Resolver resolver(store, {&provider});
            const auto state = makeState(QStringLiteral("mediaSrc:apple-fallback"), QStringLiteral("apple"));
            const auto result = resolveSynchronously(resolver, state);
            QCOMPARE(result.state, QStringLiteral("ok"));
            QCOMPARE(result.ref->trackId, QStringLiteral("1472480890"));
        }
        {
            QTemporaryDir directory;
            QVERIFY(directory.isValid());
            LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
            QVERIFY(store.open());
            LocalizedFallbackProvider provider;
            Resolver resolver(store, {&provider});
            const auto state = makeState(QStringLiteral("mediaSrc:unknown-platform"), QString());
            const auto result = resolveSynchronously(resolver, state);
            QCOMPARE(result.state, QStringLiteral("not-found"));
        }
    }

    void searchErrorsAreLogged_data()
    {
        QTest::addColumn<QString>("providerId");
        QTest::addColumn<QString>("error");
        QTest::addColumn<bool>("transportFailed");
        QTest::addColumn<QString>("expectedState");
        QTest::newRow("final transport failure")
            << QStringLiteral("transport")
            << QStringLiteral("The remote host closed the connection")
            << true << QStringLiteral("network-error");
        QTest::newRow("HTTP error")
            << QStringLiteral("http")
            << QStringLiteral("server replied: Service Unavailable")
            << false << QStringLiteral("not-found");
        QTest::newRow("JSON error")
            << QStringLiteral("json")
            << QStringLiteral("illegal value")
            << false << QStringLiteral("not-found");
    }

    void searchErrorsAreLogged()
    {
        QFETCH(QString, providerId);
        QFETCH(QString, error);
        QFETCH(bool, transportFailed);
        QFETCH(QString, expectedState);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider provider(providerId, error, transportFailed);
        Resolver resolver(store, {&provider});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:search-error-") + providerId;
        state.title = QStringLiteral("song");

        MessageCapture capture;
        QCOMPARE(resolveSynchronously(resolver, state).state, expectedState);
        QVERIFY(capture.messages().contains(
            QStringLiteral("search failed: %1: %2").arg(providerId, error)));
        QVERIFY(std::none_of(capture.messages().cbegin(), capture.messages().cend(),
                             [](const QString &message) {
            return message.contains(QStringLiteral("selected:"));
        }));
    }

    void fetchErrorsUseTransportClassification_data()
    {
        QTest::addColumn<QString>("error");
        QTest::addColumn<bool>("transportFailed");
        QTest::addColumn<QString>("expectedState");
        QTest::newRow("transport") << QStringLiteral("connection reset")
                                    << true << QStringLiteral("network-error");
        QTest::newRow("HTTP") << QStringLiteral("server replied: Not Found")
                               << false << QStringLiteral("not-found");
        QTest::newRow("lyric response") << QStringLiteral("response has no lrc field")
                                         << false << QStringLiteral("not-found");
    }

    void fetchErrorsUseTransportClassification()
    {
        QFETCH(QString, error);
        QFETCH(bool, transportFailed);
        QFETCH(QString, expectedState);
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider provider(QStringLiteral("fetch-error"), {}, false, error,
                              transportFailed);
        Resolver resolver(store, {&provider});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:fetch-") + expectedState;
        state.title = QStringLiteral("song");

        QCOMPARE(resolveSynchronously(resolver, state).state, expectedState);
    }

    void cacheMissKindsAreLoggedSeparately()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const TrackRef dangling{QStringLiteral("test"), QStringLiteral("missing-body"), 1.0};
        QVERIFY(store.mapFingerprint(QStringLiteral("mediaSrc:dangling"), dangling));
        Resolver resolver(store, {});

        MprisState missing;
        missing.music = true;
        missing.fingerprint = QStringLiteral("mediaSrc:no-mapping");
        missing.title = QStringLiteral("song");
        MprisState danglingState = missing;
        danglingState.fingerprint = QStringLiteral("mediaSrc:dangling");

        MessageCapture capture;
        QCOMPARE(resolveSynchronously(resolver, missing).state, QStringLiteral("not-found"));
        QCOMPARE(resolveSynchronously(resolver, danglingState).state, QStringLiteral("not-found"));
        QVERIFY(capture.messages().contains(
            QStringLiteral("cache mapping missing: mediaSrc:no-mapping")));
        QVERIFY(capture.messages().contains(
            QStringLiteral("cache lyric missing: test/missing-body")));
    }

    void cacheWriteFailuresAreLogged()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const QString connectionName = QStringLiteral("resolver-fault-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        {
            auto faultDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            faultDatabase.setDatabaseName(store.path());
            QVERIFY(faultDatabase.open());
            QSqlQuery query(faultDatabase);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TRIGGER fail_lyric BEFORE INSERT ON lyric "
                "BEGIN SELECT RAISE(FAIL, 'lyric write rejected'); END")));
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TRIGGER fail_fingerprint BEFORE INSERT ON fingerprint "
                "BEGIN SELECT RAISE(FAIL, 'mapping write rejected'); END")));

            TestProvider provider(QStringLiteral("cache-test"));
            Resolver resolver(store, {&provider});
            MprisState state;
            state.music = true;
            state.fingerprint = QStringLiteral("mediaSrc:write-failure");
            state.title = QStringLiteral("song");

            MessageCapture capture;
            QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("ok"));
            QVERIFY(capture.messages().contains(
                QStringLiteral("cache put failed: cache-test/track")));
            QVERIFY(capture.messages().contains(
                QStringLiteral("cache map failed: fingerprint=mediaSrc:write-failure ref=cache-test/track")));
            faultDatabase.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    void missWriteFailureIsLoggedWithoutSuccess()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const QString connectionName = QStringLiteral("resolver-miss-fault-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        {
            auto faultDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            faultDatabase.setDatabaseName(store.path());
            QVERIFY(faultDatabase.open());
            QSqlQuery query(faultDatabase);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TRIGGER fail_miss BEFORE INSERT ON miss "
                "BEGIN SELECT RAISE(FAIL, 'miss write rejected'); END")));

            Resolver resolver(store, {});
            MprisState state;
            state.music = true;
            state.fingerprint = QStringLiteral("mediaSrc:miss-write-failure");
            state.title = QStringLiteral("song");

            MessageCapture capture;
            QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("not-found"));
            QVERIFY(capture.messages().contains(
                QStringLiteral("cache miss record failed: fingerprint=mediaSrc:miss-write-failure reason=no-candidate")));
            QVERIFY(!capture.messages().contains(
                QStringLiteral("record miss: reason=no-candidate")));
            QVERIFY(!store.freshMiss(state.fingerprint).has_value());
            faultDatabase.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    void staleAsyncResultCannotReplaceTheCurrentTrack()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        DeferredProvider provider;
        Resolver resolver(store, {&provider});
        QStringList emittedFingerprints;
        connect(&resolver, &Resolver::resolved, this,
                [&](const QString &fingerprint, const ResolvedLyric &) {
                    emittedFingerprints.append(fingerprint);
                });
        auto state = [](const QString &name) {
            MprisState value;
            value.music = true;
            value.fingerprint = QStringLiteral("mediaSrc:") + name;
            value.title = name;
            value.artists = {QStringLiteral("artist")};
            value.lengthUs = 1000000;
            return value;
        };

        resolver.resolve(state(QStringLiteral("old")));
        resolver.resolve(state(QStringLiteral("new")));
        provider.complete(QStringLiteral("old"));
        QVERIFY(emittedFingerprints.isEmpty());
        QVERIFY(!store.refForFingerprint(QStringLiteral("mediaSrc:old")).has_value());

        provider.complete(QStringLiteral("new"));
        QCOMPARE(emittedFingerprints, QStringList{QStringLiteral("mediaSrc:new")});
        QVERIFY(store.refForFingerprint(QStringLiteral("mediaSrc:new")).has_value());
    }

    void destroyingResolverInvalidatesPendingCallback()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        DeferredProvider provider;
        {
            Resolver resolver(store, {&provider});
            MprisState state;
            state.music = true;
            state.fingerprint = QStringLiteral("mediaSrc:gone");
            state.title = QStringLiteral("gone");
            state.artists = {QStringLiteral("artist")};
            state.lengthUs = 1000000;
            resolver.resolve(state);
        }
        provider.complete(QStringLiteral("gone"));
        QVERIFY(!store.refForFingerprint(QStringLiteral("mediaSrc:gone")).has_value());
    }
};

QTEST_GUILESS_MAIN(ResolverTest)
#include "tst_resolver.moc"
