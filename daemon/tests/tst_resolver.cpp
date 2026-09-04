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
};

QTEST_GUILESS_MAIN(ResolverTest)
#include "tst_resolver.moc"
