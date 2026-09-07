#include "daemon/src/resolver.h"

#include "core/store/lyricstore.h"

#include <QTemporaryDir>
#include <QTest>

using namespace PlasmaLyrics;

namespace {

class TestProvider final : public Provider
{
public:
    TestProvider(QString providerId, bool fail)
        : m_id(std::move(providerId)), m_fail(fail) {}

    QString id() const override { return m_id; }
    bool isConfigured() const override { return true; }
    QList<Candidate> search(const TrackQuery &query) override
    {
        if (m_fail) {
            m_error = QStringLiteral("simulated network failure");
            return {};
        }
        return {{QStringLiteral("track"), query.title, query.artists, query.album, query.lengthMs}};
    }
    std::optional<LyricDocument> fetch(const QString &) override
    {
        return LyricDocument{{{1000, 2000, QStringLiteral("line"), std::nullopt, std::nullopt}}, 0, false};
    }
    QString lastError() const override { return m_error; }

private:
    QString m_id;
    bool m_fail;
    QString m_error;
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
    QList<Candidate> search(const TrackQuery &) override
    {
        return {
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
            {QStringLiteral("1803908863"), QStringLiteral("怪物"), {QStringLiteral("YOASOBI")}, QString(), 206000}};
    }
    std::optional<LyricDocument> fetch(const QString &) override
    {
        return LyricDocument{{{0, 1000, QStringLiteral("line"), std::nullopt, std::nullopt}}, 0, false};
    }
    QString lastError() const override { return {}; }
};

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
        const auto result = resolver.resolve(state);
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
        TestProvider failed(QStringLiteral("failed"), true);
        TestProvider working(QStringLiteral("working"), false);
        Resolver resolver(store, {&failed, &working});
        MprisState state;
        state.music = true;
        state.fingerprint = QStringLiteral("mediaSrc:provider-fallback");
        state.title = QStringLiteral("song");
        state.artists = {QStringLiteral("artist")};
        state.album = QStringLiteral("album");
        state.lengthUs = 120000000;

        const auto result = resolver.resolve(state);
        QCOMPARE(result.state, QStringLiteral("ok"));
        QCOMPARE(result.ref->provider, QStringLiteral("working"));
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
            const auto result = resolver.resolve(
                makeState(QStringLiteral("mediaSrc:apple-fallback"), QStringLiteral("apple")));
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
            const auto result = resolver.resolve(
                makeState(QStringLiteral("mediaSrc:unknown-platform"), QString()));
            QCOMPARE(result.state, QStringLiteral("not-found"));
        }
    }
};

QTEST_GUILESS_MAIN(ResolverTest)
#include "tst_resolver.moc"
