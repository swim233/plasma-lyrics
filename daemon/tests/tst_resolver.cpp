#include "daemon/src/resolver.h"

#include "core/store/lyricstore.h"
#include "providers/local/localprovider.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <algorithm>
#include <utility>

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
            callback({{}, m_searchError, m_searchTransportFailed, cacheVersion(),
                      m_cacheableMiss});
            return;
        }
        if (m_noCandidates) {
            callback({{}, {}, false, cacheVersion(), m_cacheableMiss});
            return;
        }
        callback({{{QStringLiteral("track"), query.title, query.artists, query.album,
                    query.lengthMs}}, {}, false, cacheVersion(), m_cacheableMiss});
    }
    void fetch(const QString &, FetchCallback callback) override
    {
        ++m_fetchCount;
        if (!m_fetchError.isEmpty()) {
            callback({std::nullopt, m_fetchError, m_fetchTransportFailed});
            return;
        }
        if (m_emptyDocument) {
            callback({LyricDocument{}, {}});
            return;
        }
        callback({LyricDocument{{{1000, 2000,
                                  m_creditOnly ? QStringLiteral("作词：Someone")
                                               : QStringLiteral("line"),
                                  std::nullopt, std::nullopt, m_creditOnly}},
                                0, false}, {}});
    }
    void setNoCandidates(bool noCandidates = true) { m_noCandidates = noCandidates; }
    void setEmptyDocument(bool emptyDocument = true) { m_emptyDocument = emptyDocument; }
    void setCreditOnly(bool creditOnly = true) { m_creditOnly = creditOnly; }
    void setCacheableMiss(bool cacheableMiss) { m_cacheableMiss = cacheableMiss; }
    int searchCount() const { return m_searchCount; }
    int fetchCount() const { return m_fetchCount; }

private:
    QString m_id;
    QString m_searchError;
    bool m_searchTransportFailed = false;
    QString m_fetchError;
    bool m_fetchTransportFailed = false;
    bool m_noCandidates = false;
    bool m_emptyDocument = false;
    bool m_creditOnly = false;
    bool m_cacheableMiss = true;
    int m_searchCount = 0;
    int m_fetchCount = 0;
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
    explicit DeferredProvider(QString providerId = QStringLiteral("deferred"))
        : m_id(std::move(providerId))
    {
    }
    QString id() const override { return m_id; }
    bool isConfigured() const override { return true; }
    void search(const TrackQuery &query, SearchCallback callback) override
    {
        ++m_searchCount;
        m_searches[query.title].append(std::move(callback));
    }
    void fetch(const QString &, FetchCallback callback) override
    {
        callback({LyricDocument{{{0, 1000, QStringLiteral("line"), std::nullopt, std::nullopt}}, 0, false}, {}});
    }
    void complete(const QString &title)
    {
        auto &callbacks = m_searches[title];
        auto callback = callbacks.takeFirst();
        if (callbacks.isEmpty()) m_searches.remove(title);
        callback({{{title, title, {QStringLiteral("artist")}, QString(), 1000}}, {}});
    }
    int searchCount() const { return m_searchCount; }

private:
    QString m_id;
    QHash<QString, QList<SearchCallback>> m_searches;
    int m_searchCount = 0;
};

class RefreshingIndexProvider final : public Provider
{
public:
    QString id() const override { return QStringLiteral("refreshing"); }
    bool isConfigured() const override { return true; }
    QString cacheVersion() const override { return m_cacheVersion; }

    void search(const TrackQuery &query, SearchCallback callback) override
    {
        ++m_searchCount;
        if (m_searchCount == 2 && onRetrySearch) onRetrySearch();
        Candidate candidate{m_searchCount == 1 ? QStringLiteral("old-track")
                                               : QStringLiteral("new-track"),
                            query.title, query.artists, query.album, query.lengthMs};
        candidate.contentId = m_searchCount == 1 ? QStringLiteral("old.ttml")
                                                  : QStringLiteral("new.ttml");
        callback({{candidate}, {}, false, m_cacheVersion});
    }

    void fetch(const QString &contentId, FetchCallback callback) override
    {
        ++m_fetchCount;
        if (contentId == QStringLiteral("old.ttml")) {
            m_oldFetch = std::move(callback);
            return;
        }
        callback({LyricDocument{{{0, 1000, QStringLiteral("fresh line"),
                                  std::nullopt, std::nullopt}}, 0, false}, {}});
    }

    void finishRefreshThenFailOldFetch()
    {
        m_cacheVersion = QStringLiteral("revision-2");
        auto callback = std::exchange(m_oldFetch, {});
        callback({std::nullopt, QStringLiteral("old content disappeared"), false});
    }

    int searchCount() const { return m_searchCount; }
    int fetchCount() const { return m_fetchCount; }
    std::function<void()> onRetrySearch;

private:
    QString m_cacheVersion = QStringLiteral("revision-1");
    FetchCallback m_oldFetch;
    int m_searchCount = 0;
    int m_fetchCount = 0;
};

ResolvedLyric resolveSynchronously(Resolver &resolver, const MprisState &state,
                                   Resolver::ResolveOptions options = {})
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
    resolver.resolve(state, std::move(options));
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
        TestProvider failed(QStringLiteral("netease"), QStringLiteral("simulated network failure"), true);
        TestProvider working(QStringLiteral("amll"));
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
        QCOMPARE(result.ref->provider, QStringLiteral("amll"));
    }

    void localHitDoesNotCallTheFollowingNetworkProvider()
    {
        QTemporaryDir directory;
        const QString lyricsDirectory = directory.filePath(QStringLiteral("lyrics"));
        QVERIFY(QDir().mkpath(lyricsDirectory));
        QFile lyric(QDir(lyricsDirectory).filePath(QStringLiteral("library-entry.lrc")));
        QVERIFY(lyric.open(QIODevice::WriteOnly));
        QVERIFY(lyric.write("[ti:Song]\n[ar:Artist]\n[length:02:00]\n"
                            "[00:01.000]local line\n") > 0);
        lyric.close();

        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        LocalProvider local(lyricsDirectory);
        TestProvider network(QStringLiteral("network"));
        Resolver resolver(store, {&local, &network});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:local-first");
        state.mediaSrc = QStringLiteral("https://example.test/song.flac");
        state.title = QStringLiteral("Song");
        state.artists = {QStringLiteral("Artist")};
        state.lengthUs = 120000000;

        const auto result = resolveSynchronously(resolver, state);

        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.ref->provider, QStringLiteral("local"));
        QCOMPARE(result.document.lines.first().text, QStringLiteral("local line"));
        QCOMPARE(network.searchCount(), 0);
        QCOMPARE(network.fetchCount(), 0);
    }

    void standardMprisUrlFindsSidecarWithoutKdeMediaSrc()
    {
        QTemporaryDir directory;
        const QString lyricsDirectory = directory.filePath(QStringLiteral("lyrics"));
        QVERIFY(QDir().mkpath(lyricsDirectory));
        const QString audioPath = directory.filePath(QStringLiteral("song.flac"));
        QFile audio(audioPath);
        QVERIFY(audio.open(QIODevice::WriteOnly));
        QVERIFY(audio.write("audio") > 0);
        audio.close();
        QFile sidecar(directory.filePath(QStringLiteral("song.lrc")));
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        QVERIFY(sidecar.write("[00:01.000]standard url sidecar\n") > 0);
        sidecar.close();

        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        LocalProvider local(lyricsDirectory);
        Resolver resolver(store, {&local});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("meta:standard-url-sidecar");
        state.url = QUrl::fromLocalFile(audioPath).toString();
        state.title = QStringLiteral("Song");
        QVERIFY(state.mediaSrc.isEmpty());

        const auto result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.ref->provider, QStringLiteral("local"));
        QCOMPARE(result.document.lines.first().text,
                 QStringLiteral("standard url sidecar"));
        QVERIFY(store.refForFingerprint(QStringLiteral("meta:standard-url-sidecar"))
                    .has_value());
        QCOMPARE(state.fingerprint, QStringLiteral("meta:standard-url-sidecar"));
    }

    void validKdeMediaSrcWinsButInvalidOneFallsBackToStandardUrl()
    {
        QTemporaryDir directory;
        const QString lyricsDirectory = directory.filePath(QStringLiteral("lyrics"));
        QVERIFY(QDir().mkpath(lyricsDirectory));
        const QString standardAudioPath = directory.filePath(QStringLiteral("standard.flac"));
        const QString kdeAudioPath = directory.filePath(QStringLiteral("kde.flac"));
        for (const auto &audioPath : {standardAudioPath, kdeAudioPath}) {
            QFile audio(audioPath);
            QVERIFY(audio.open(QIODevice::WriteOnly));
            QVERIFY(audio.write("audio") > 0);
        }
        QFile standardSidecar(directory.filePath(QStringLiteral("standard.lrc")));
        QVERIFY(standardSidecar.open(QIODevice::WriteOnly));
        QVERIFY(standardSidecar.write("[00:01.000]standard line\n") > 0);
        standardSidecar.close();
        QFile kdeSidecar(directory.filePath(QStringLiteral("kde.lrc")));
        QVERIFY(kdeSidecar.open(QIODevice::WriteOnly));
        QVERIFY(kdeSidecar.write("[00:01.000]kde line\n") > 0);
        kdeSidecar.close();

        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        LocalProvider local(lyricsDirectory);
        Resolver resolver(store, {&local});
        MprisState state;
        state.music = true;
        state.url = QUrl::fromLocalFile(standardAudioPath).toString();
        state.mediaSrc = QUrl::fromLocalFile(kdeAudioPath).toString();
        state.fingerprint = QStringLiteral("mediaSrc:kde-wins");
        state.title = QStringLiteral("Song");

        auto result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.document.lines.first().text, QStringLiteral("kde line"));

        state.mediaSrc = QUrl::fromLocalFile(
            directory.filePath(QStringLiteral("missing.flac"))).toString();
        state.fingerprint = QStringLiteral("mediaSrc:invalid-kde-falls-back");
        result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.document.lines.first().text, QStringLiteral("standard line"));
    }

    void localDirectoryChangeInvalidatesItsNegativeCache()
    {
        QTemporaryDir directory;
        const QString lyricsDirectory = directory.filePath(QStringLiteral("lyrics"));
        QVERIFY(QDir().mkpath(lyricsDirectory));
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        LocalProvider local(lyricsDirectory);
        Resolver resolver(store, {&local});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:local-version");
        state.mediaSrc = QStringLiteral("https://example.test/song.flac");
        state.title = QStringLiteral("Song");
        state.artists = {QStringLiteral("Artist")};

        QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("not-found"));
        QVERIFY(store.freshProviderMiss(state.fingerprint, local.id(), local.cacheVersion(),
                                        QDateTime::currentSecsSinceEpoch(), 86400));

        QFile lyric(QDir(lyricsDirectory).filePath(QStringLiteral("Song.lrc")));
        QVERIFY(lyric.open(QIODevice::WriteOnly));
        QVERIFY(lyric.write("[ar:Artist]\n[00:01.000]new local line\n") > 0);
        lyric.close();

        const auto result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.ref->provider, QStringLiteral("local"));
        QCOMPARE(result.document.lines.first().text, QStringLiteral("new local line"));
    }

    void standardMprisUrlMissCannotHideALaterSidecar()
    {
        QTemporaryDir directory;
        const QString lyricsDirectory = directory.filePath(QStringLiteral("lyrics"));
        QVERIFY(QDir().mkpath(lyricsDirectory));
        const QString audioPath = directory.filePath(QStringLiteral("song.flac"));
        QFile audio(audioPath);
        QVERIFY(audio.open(QIODevice::WriteOnly));
        QVERIFY(audio.write("audio") > 0);
        audio.close();

        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        LocalProvider local(lyricsDirectory);
        Resolver resolver(store, {&local});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("meta:standard-url-sidecar-version");
        state.url = QUrl::fromLocalFile(audioPath).toString();
        state.title = QStringLiteral("Song");
        QVERIFY(state.mediaSrc.isEmpty());

        QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("not-found"));
        QVERIFY(!store.freshProviderMiss(state.fingerprint, local.id(), local.cacheVersion(),
                                         QDateTime::currentSecsSinceEpoch(), 86400));

        QFile sidecar(directory.filePath(QStringLiteral("song.lrc")));
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        QVERIFY(sidecar.write("[00:01.000]new sidecar line\n") > 0);
        sidecar.close();

        const auto result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.ref->provider, QStringLiteral("local"));
        QCOMPARE(result.document.lines.first().text, QStringLiteral("new sidecar line"));
    }

    void invalidDirectoryCandidateCannotHideALaterSidecar()
    {
        QTemporaryDir directory;
        const QString lyricsDirectory = directory.filePath(QStringLiteral("lyrics"));
        QVERIFY(QDir().mkpath(lyricsDirectory));
        const QString audioPath = directory.filePath(QStringLiteral("song.flac"));
        QFile audio(audioPath);
        QVERIFY(audio.open(QIODevice::WriteOnly));
        QVERIFY(audio.write("audio") > 0);
        audio.close();
        QFile invalid(QDir(lyricsDirectory).filePath(QStringLiteral("library-entry.lrc")));
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        QVERIFY(invalid.write("[ti:Song]\n[ar:Artist]\nnot a timed lyric\n") > 0);
        invalid.close();

        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        LocalProvider local(lyricsDirectory);
        Resolver resolver(store, {&local});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:invalid-directory-sidecar");
        state.mediaSrc = QUrl::fromLocalFile(audioPath).toString();
        state.title = QStringLiteral("Song");
        state.artists = {QStringLiteral("Artist")};

        QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("no-lyric"));
        QVERIFY(!store.freshProviderMiss(state.fingerprint, local.id(), local.cacheVersion())
                     .has_value());

        QFile sidecar(directory.filePath(QStringLiteral("song.lrc")));
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        QVERIFY(sidecar.write("[00:01.000]recovered from sidecar\n") > 0);
        sidecar.close();

        const auto result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.document.lines.first().text,
                 QStringLiteral("recovered from sidecar"));
    }

    void repairedSidecarIsNotSuppressedByItsEarlierInvalidContents()
    {
        QTemporaryDir directory;
        const QString lyricsDirectory = directory.filePath(QStringLiteral("lyrics"));
        QVERIFY(QDir().mkpath(lyricsDirectory));
        const QString audioPath = directory.filePath(QStringLiteral("song.flac"));
        QFile audio(audioPath);
        QVERIFY(audio.open(QIODevice::WriteOnly));
        QVERIFY(audio.write("audio") > 0);
        audio.close();
        const QString sidecarPath = directory.filePath(QStringLiteral("song.lrc"));
        QFile sidecar(sidecarPath);
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        QVERIFY(sidecar.write("[ti:Wrong metadata]\ninvalid contents\n") > 0);
        sidecar.close();

        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        LocalProvider local(lyricsDirectory);
        Resolver resolver(store, {&local});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:repaired-sidecar");
        state.mediaSrc = QUrl::fromLocalFile(audioPath).toString();
        state.title = QStringLiteral("Song");

        QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("no-lyric"));
        QVERIFY(!store.freshProviderMiss(state.fingerprint, local.id(), local.cacheVersion())
                     .has_value());

        QVERIFY(sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(sidecar.write("[00:02.000]fixed sidecar\n") > 0);
        sidecar.close();

        const auto result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.document.lines.first().text, QStringLiteral("fixed sidecar"));
    }

    void continuesToTheNextProviderAfterFetchFailure()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider failed(QStringLiteral("failed"), {}, false,
                            QStringLiteral("invalid lyric payload"), false);
        TestProvider working(QStringLiteral("working"));
        Resolver resolver(store, {&failed, &working});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:fetch-fallback");
        state.title = QStringLiteral("song");
        state.artists = {QStringLiteral("artist")};

        const auto result = resolveSynchronously(resolver, state);

        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.ref->provider, QStringLiteral("working"));
        QCOMPARE(failed.fetchCount(), 1);
        QCOMPARE(working.searchCount(), 1);
    }

    void nonCacheableSearchSuppressesEveryFailureMiss()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider searchFailure(QStringLiteral("search-failure"),
                                   QStringLiteral("bad search payload"));
        TestProvider fetchFailure(QStringLiteral("fetch-failure"), {}, false,
                                  QStringLiteral("bad lyric payload"));
        TestProvider empty(QStringLiteral("empty"));
        empty.setEmptyDocument();
        TestProvider filtered(QStringLiteral("filtered"));
        filtered.setCreditOnly();
        TestProvider working(QStringLiteral("working"));
        const QList<TestProvider *> nonCacheable{
            &searchFailure, &fetchFailure, &empty, &filtered};
        for (auto *provider : nonCacheable) provider->setCacheableMiss(false);
        Resolver resolver(store, {&searchFailure, &fetchFailure, &empty, &filtered, &working},
                          true);
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:non-cacheable-failures");
        state.title = QStringLiteral("song");

        const auto result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.ref->provider, QStringLiteral("working"));
        for (const auto *provider : nonCacheable) {
            QVERIFY(!store.freshProviderMiss(state.fingerprint, provider->id(),
                                             provider->cacheVersion()).has_value());
        }
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
        const auto miss = store.freshProviderMiss(state.fingerprint, failed.id(),
                                                  failed.cacheVersion());
        QVERIFY(miss.has_value());
        QCOMPARE(miss->reason, QStringLiteral("network"));
        QCOMPARE(failed.searchCount(), 1);
        QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("network-error"));
        QCOMPARE(failed.searchCount(), 1);

        QVERIFY(store.recordProviderMiss(state.fingerprint, failed.id(),
                                         QStringLiteral("network"), failed.cacheVersion(),
                                         QDateTime::currentSecsSinceEpoch() - 301));
        QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("network-error"));
        QCOMPARE(failed.searchCount(), 2);
    }

    void providerNoCandidateMissKeepsSevenDayTtl()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider provider(QStringLiteral("unused"));
        provider.setNoCandidates();
        Resolver resolver(store, {&provider});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:no-candidate-cache");
        state.title = QStringLiteral("song");

        QVERIFY(store.recordProviderMiss(state.fingerprint, provider.id(),
                                         QStringLiteral("no-candidate"), provider.cacheVersion(),
                                         QDateTime::currentSecsSinceEpoch() - 301));
        QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("not-found"));
        QCOMPARE(provider.searchCount(), 0);
    }

    void legacyGlobalMissDoesNotSuppressANewProvider()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider provider(QStringLiteral("new-provider"));
        Resolver resolver(store, {&provider});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:legacy-global-miss");
        state.title = QStringLiteral("song");

        QVERIFY(store.recordMiss(state.fingerprint, QStringLiteral("no-candidate")));
        QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("ok"));
        QCOMPARE(provider.searchCount(), 1);
    }

    void successfulPrimaryProviderDoesNotQueryFallback()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider primary(QStringLiteral("netease"));
        TestProvider fallback(QStringLiteral("amll"));
        Resolver resolver(store, {&primary, &fallback});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:primary-wins");
        state.title = QStringLiteral("song");

        const auto result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.ref->provider, QStringLiteral("netease"));
        QCOMPARE(primary.searchCount(), 1);
        QCOMPARE(fallback.searchCount(), 0);
    }

    void manualPreferenceFallsBackWithoutBeingOverwritten()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider netease(QStringLiteral("netease"));
        TestProvider amll(QStringLiteral("amll"));
        amll.setNoCandidates();
        Resolver resolver(store, {&netease, &amll});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:preferred-fallback");
        state.title = QStringLiteral("song");
        QVERIFY(store.setPreferredProvider(state.fingerprint, QStringLiteral("amll")));

        const auto result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.preferredProvider, QStringLiteral("amll"));
        QCOMPARE(result.effectivePreferredProvider, QStringLiteral("amll"));
        QVERIFY(result.temporaryFallback);
        QCOMPARE(result.ref->provider, QStringLiteral("netease"));
        QCOMPARE(amll.searchCount(), 1);
        QCOMPARE(netease.searchCount(), 1);
        QCOMPARE(store.preferredProvider(state.fingerprint).value_or(QString()),
                 QStringLiteral("amll"));
    }

    void emptyLyricFallsBackToTheNextProvider()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider empty(QStringLiteral("empty"));
        empty.setEmptyDocument();
        TestProvider working(QStringLiteral("working"));
        Resolver resolver(store, {&empty, &working});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:empty-fallback");
        state.title = QStringLiteral("song");

        const auto result = resolveSynchronously(resolver, state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.ref->provider, QStringLiteral("working"));
        QCOMPARE(empty.fetchCount(), 1);
        QCOMPARE(working.searchCount(), 1);
        const auto miss = store.freshProviderMiss(state.fingerprint, empty.id(),
                                                  empty.cacheVersion());
        QVERIFY(miss.has_value());
        QCOMPARE(miss->reason, QStringLiteral("empty"));
    }

    void lyricRemovedByCreditFilteringFallsBackToTheNextProvider()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider credits(QStringLiteral("credits"));
        credits.setCreditOnly();
        TestProvider working(QStringLiteral("working"));
        Resolver resolver(store, {&credits, &working}, true);
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:credits-fallback");
        state.title = QStringLiteral("song");

        const auto result = resolveSynchronously(resolver, state);

        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.ref->provider, QStringLiteral("working"));
        QCOMPARE(credits.fetchCount(), 1);
        QCOMPARE(working.searchCount(), 1);
    }

    void forcedResearchBypassesCachedMappingAndProviderMiss()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider provider(QStringLiteral("netease"));
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:forced-research");
        state.title = QStringLiteral("song");
        const TrackRef oldRef{provider.id(), QStringLiteral("old"), 1.0};
        const LyricDocument oldDocument{{{0, 1000, QStringLiteral("old line"),
                                          std::nullopt, std::nullopt}}, 0, false};
        QVERIFY(store.putLyric(oldRef, oldDocument));
        QVERIFY(store.mapFingerprint(state.fingerprint, oldRef));
        QVERIFY(store.recordProviderMiss(state.fingerprint, provider.id(),
                                         QStringLiteral("no-candidate"),
                                         provider.cacheVersion()));
        Resolver resolver(store, {&provider});

        std::optional<ResolvedLyric> result;
        connect(&resolver, &Resolver::resolved, this,
                [&](const QString &, const ResolvedLyric &resolved) { result = resolved; });
        ResolvedLyric existing{QStringLiteral("ok"), oldRef, oldDocument};
        resolver.resolve(state, {.force = true, .existing = existing});

        QVERIFY(result.has_value());
        QCOMPARE(result->state, QStringLiteral("ok"));
        QCOMPARE(provider.searchCount(), 1);
        QCOMPARE(provider.fetchCount(), 1);
        QCOMPARE(result->document.lines.first().text, QStringLiteral("line"));
        QCOMPARE(store.refForFingerprint(state.fingerprint)->trackId,
                 QStringLiteral("track"));
    }

    void forcedResearchFromSearchingPublishesNotFound()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider provider(QStringLiteral("provider"));
        provider.setNoCandidates();
        Resolver resolver(store, {&provider});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:force-searching");
        state.title = QStringLiteral("song");
        const ResolvedLyric searching{QStringLiteral("searching"), std::nullopt, {}};

        const auto result = resolveSynchronously(
            resolver, state, {.force = true, .existing = searching});

        QCOMPARE(result.state, QStringLiteral("not-found"));
        QCOMPARE(provider.searchCount(), 1);
    }

    void forcedResearchFromErrorPublishesSpecificTerminalError()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider provider(QStringLiteral("provider"),
                              QStringLiteral("connection reset"), true);
        Resolver resolver(store, {&provider});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:force-error");
        state.title = QStringLiteral("song");
        const ResolvedLyric previousError{QStringLiteral("network-error"),
                                          std::nullopt, {}};

        const auto result = resolveSynchronously(
            resolver, state, {.force = true, .existing = previousError});

        QCOMPARE(result.state, QStringLiteral("network-error"));
        QCOMPARE(provider.searchCount(), 1);
    }

    void forcedResearchKeepsOnlyAValidNonEmptyLyric()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider provider(QStringLiteral("provider"));
        provider.setNoCandidates();
        Resolver resolver(store, {&provider});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:force-valid-existing");
        state.title = QStringLiteral("song");
        const TrackRef ref{QStringLiteral("provider"), QStringLiteral("old"), 1.0};
        const LyricDocument document{{{0, 1000, QStringLiteral("visible"),
                                       std::nullopt, std::nullopt}}, 0, false};
        const ResolvedLyric existing{QStringLiteral("ok"), ref, document};
        QSignalSpy resolvedSpy(&resolver, &Resolver::resolved);

        resolver.resolve(state, {.force = true, .existing = existing});

        QCOMPARE(provider.searchCount(), 1);
        QCOMPARE(resolvedSpy.size(), 1);
        const auto retained = resolvedSpy.first().at(1).value<ResolvedLyric>();
        QCOMPARE(retained.state, QStringLiteral("ok"));
        QCOMPARE(retained.document.lines.first().text, QStringLiteral("visible"));
        QVERIFY(retained.switchingProvider.isEmpty());
    }

    void cachedFallbackStaysVisibleWhilePreferredProviderRetries()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider netease(QStringLiteral("netease"));
        netease.setNoCandidates();
        TestProvider amll(QStringLiteral("amll"));
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:cached-fallback");
        state.title = QStringLiteral("song");
        const TrackRef fallbackRef{amll.id(), QStringLiteral("cached"), 0.9};
        const LyricDocument fallbackDocument{{{0, 1000, QStringLiteral("cached line"),
                                               std::nullopt, std::nullopt}}, 0, false};
        QVERIFY(store.setPreferredProvider(state.fingerprint, netease.id()));
        QVERIFY(store.putLyric(fallbackRef, fallbackDocument));
        QVERIFY(store.mapFingerprint(state.fingerprint, fallbackRef));
        Resolver resolver(store, {&netease, &amll});
        QList<ResolvedLyric> emissions;
        connect(&resolver, &Resolver::resolved, this,
                [&](const QString &, const ResolvedLyric &resolved) {
                    emissions.append(resolved);
                });

        resolver.resolve(state);

        QCOMPARE(emissions.size(), 1);
        QCOMPARE(emissions.first().state, QStringLiteral("ok"));
        QCOMPARE(emissions.first().ref->provider, QStringLiteral("amll"));
        QCOMPARE(emissions.first().document.lines.first().text,
                 QStringLiteral("cached line"));
        QVERIFY(emissions.first().temporaryFallback);
        QCOMPARE(netease.searchCount(), 1);
        QCOMPARE(amll.searchCount(), 0);

        resolver.resolve(state);
        QCOMPARE(netease.searchCount(), 1);
        QCOMPARE(emissions.size(), 2);
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

    void preferredCacheRemapFailureIsLogged()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const QString fingerprint = QStringLiteral("mediaSrc:preferred-remap-failure");
        const TrackRef ref{QStringLiteral("preferred"), QStringLiteral("cached"), 1.0};
        const LyricDocument document{{{0, 1000, QStringLiteral("cached line"),
                                       std::nullopt, std::nullopt}}, 0, false};
        QVERIFY(store.putLyric(ref, document));
        QVERIFY(store.mapProviderFingerprint(fingerprint, ref));
        QVERIFY(store.setPreferredProvider(fingerprint, ref.provider));

        const QString connectionName = QStringLiteral("resolver-preferred-remap-fault-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        {
            auto faultDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            faultDatabase.setDatabaseName(store.path());
            QVERIFY(faultDatabase.open());
            QSqlQuery query(faultDatabase);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TRIGGER fail_preferred_remap BEFORE INSERT ON fingerprint "
                "BEGIN SELECT RAISE(FAIL, 'preferred remap rejected'); END")));
            TestProvider provider(ref.provider);
            Resolver resolver(store, {&provider});
            MprisState state;
            state.music = true;
            state.fingerprint = fingerprint;
            state.title = QStringLiteral("song");

            MessageCapture capture;
            const auto result = resolveSynchronously(resolver, state);
            QCOMPARE(result.state, QStringLiteral("ok"));
            QCOMPARE(provider.searchCount(), 0);
            QVERIFY(capture.messages().contains(
                QStringLiteral("cache map failed: fingerprint=%1 ref=%2/%3")
                    .arg(fingerprint, ref.provider, ref.trackId)));
            faultDatabase.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    void providerCacheRemapFailureIsLogged()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const QString fingerprint = QStringLiteral("mediaSrc:provider-remap-failure");
        const TrackRef ref{QStringLiteral("cached-provider"), QStringLiteral("cached"), 1.0};
        const LyricDocument document{{{0, 1000, QStringLiteral("cached line"),
                                       std::nullopt, std::nullopt}}, 0, false};
        QVERIFY(store.putLyric(ref, document));
        QVERIFY(store.mapProviderFingerprint(fingerprint, ref));

        const QString connectionName = QStringLiteral("resolver-provider-remap-fault-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        {
            auto faultDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            faultDatabase.setDatabaseName(store.path());
            QVERIFY(faultDatabase.open());
            QSqlQuery query(faultDatabase);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TRIGGER fail_provider_remap BEFORE INSERT ON fingerprint "
                "BEGIN SELECT RAISE(FAIL, 'provider remap rejected'); END")));
            TestProvider first(QStringLiteral("first"));
            first.setNoCandidates();
            TestProvider cached(ref.provider);
            Resolver resolver(store, {&first, &cached});
            MprisState state;
            state.music = true;
            state.fingerprint = fingerprint;
            state.title = QStringLiteral("song");

            MessageCapture capture;
            const auto result = resolveSynchronously(resolver, state);
            QCOMPARE(result.state, QStringLiteral("ok"));
            QCOMPARE(result.ref->provider, ref.provider);
            QCOMPARE(cached.searchCount(), 0);
            QVERIFY(capture.messages().contains(
                QStringLiteral("cache map failed: fingerprint=%1 ref=%2/%3")
                    .arg(fingerprint, ref.provider, ref.trackId)));
            faultDatabase.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    void providerMissClearFailureIsLogged()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        TestProvider provider(QStringLiteral("clear-failure"));
        const QString fingerprint = QStringLiteral("mediaSrc:clear-failure");
        QVERIFY(store.recordProviderMiss(fingerprint, provider.id(),
                                         QStringLiteral("fetch-error"),
                                         provider.cacheVersion()));
        const QString connectionName = QStringLiteral("resolver-clear-fault-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        {
            auto faultDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            faultDatabase.setDatabaseName(store.path());
            QVERIFY(faultDatabase.open());
            QSqlQuery query(faultDatabase);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TRIGGER fail_miss_clear BEFORE DELETE ON provider_miss "
                "BEGIN SELECT RAISE(FAIL, 'miss clear rejected'); END")));
            Resolver resolver(store, {&provider});
            MprisState state;
            state.music = true;
            state.fingerprint = fingerprint;
            state.title = QStringLiteral("song");

            MessageCapture capture;
            QCOMPARE(resolveSynchronously(resolver, state,
                                           {.force = true}).state,
                     QStringLiteral("ok"));
            QVERIFY(capture.messages().contains(
                QStringLiteral("provider miss clear failed: fingerprint=%1 provider=%2")
                    .arg(fingerprint, provider.id())));
            faultDatabase.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
    }

    void staleCandidateFailureUsesItsRevisionAndRetriesRefreshedIndex()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        RefreshingIndexProvider provider;
        Resolver resolver(store, {&provider});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:refresh-race");
        state.title = QStringLiteral("song");
        state.artists = {QStringLiteral("artist")};
        bool oldMissObservedBeforeRetry = false;
        bool newRevisionWasNotSuppressed = false;
        provider.onRetrySearch = [&] {
            oldMissObservedBeforeRetry = store.freshProviderMiss(
                state.fingerprint, provider.id(), QStringLiteral("revision-1")).has_value();
            newRevisionWasNotSuppressed = !store.freshProviderMiss(
                state.fingerprint, provider.id(), QStringLiteral("revision-2")).has_value();
        };
        std::optional<ResolvedLyric> resolved;
        connect(&resolver, &Resolver::resolved, this,
                [&](const QString &, const ResolvedLyric &value) { resolved = value; });

        resolver.resolve(state);
        QCOMPARE(provider.searchCount(), 1);
        QCOMPARE(provider.fetchCount(), 1);
        QVERIFY(!resolved.has_value());

        provider.finishRefreshThenFailOldFetch();

        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->state, QStringLiteral("ok"));
        QCOMPARE(resolved->ref->trackId, QStringLiteral("new-track"));
        QCOMPARE(resolved->document.lines.first().text, QStringLiteral("fresh line"));
        QCOMPARE(provider.searchCount(), 2);
        QCOMPARE(provider.fetchCount(), 2);
        QVERIFY(oldMissObservedBeforeRetry);
        QVERIFY(newRevisionWasNotSuppressed);
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
                "CREATE TRIGGER fail_miss BEFORE INSERT ON provider_miss "
                "BEGIN SELECT RAISE(FAIL, 'miss write rejected'); END")));

            TestProvider provider(QStringLiteral("failed"));
            provider.setNoCandidates();
            Resolver resolver(store, {&provider});
            MprisState state;
            state.music = true;
            state.fingerprint = QStringLiteral("mediaSrc:miss-write-failure");
            state.title = QStringLiteral("song");

            MessageCapture capture;
            QCOMPARE(resolveSynchronously(resolver, state).state, QStringLiteral("not-found"));
            QVERIFY(capture.messages().contains(
                QStringLiteral("provider miss record failed: fingerprint=mediaSrc:miss-write-failure provider=failed reason=no-candidate")));
            QVERIFY(!store.freshProviderMiss(state.fingerprint, provider.id(),
                                             provider.cacheVersion()).has_value());
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

    void preferenceChangeInvalidatesThePreviousProviderRequest()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        DeferredProvider amll(QStringLiteral("amll"));
        DeferredProvider netease(QStringLiteral("netease"));
        Resolver resolver(store, {&amll, &netease});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:preference-generation");
        state.title = QStringLiteral("song");
        state.artists = {QStringLiteral("artist")};
        state.lengthUs = 1000000;
        QList<ResolvedLyric> emissions;
        connect(&resolver, &Resolver::resolved, this,
                [&](const QString &, const ResolvedLyric &lyric) {
                    emissions.append(lyric);
                });

        resolver.resolve(state);
        QCOMPARE(amll.searchCount(), 1);
        QVERIFY(store.setPreferredProvider(state.fingerprint, QStringLiteral("netease")));
        resolver.resolve(state, {.force = true});
        QCOMPARE(netease.searchCount(), 1);

        amll.complete(state.title);
        QVERIFY(emissions.isEmpty());
        QVERIFY(!store.refForProvider(state.fingerprint, QStringLiteral("amll")).has_value());
        netease.complete(state.title);
        QCOMPARE(emissions.size(), 1);
        QCOMPARE(emissions.first().ref->provider, QStringLiteral("netease"));
    }

    void repeatedResearchOnlyAllowsTheLatestRequestToCommit()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        DeferredProvider provider;
        Resolver resolver(store, {&provider});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:repeat-research");
        state.title = QStringLiteral("song");
        state.artists = {QStringLiteral("artist")};
        state.lengthUs = 1000000;
        int emissions = 0;
        connect(&resolver, &Resolver::resolved, this,
                [&](const QString &, const ResolvedLyric &) { ++emissions; });

        resolver.resolve(state, {.force = true});
        resolver.resolve(state, {.force = true});
        QCOMPARE(provider.searchCount(), 2);

        provider.complete(state.title);
        QCOMPARE(emissions, 0);
        QVERIFY(!store.refForFingerprint(state.fingerprint).has_value());
        provider.complete(state.title);
        QCOMPARE(emissions, 1);
        QVERIFY(store.refForFingerprint(state.fingerprint).has_value());
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
