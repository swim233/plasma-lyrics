#include "providers/netease/neteaseprovider.h"

#include "core/lyric/timeline.h"

#include <QFile>
#include <QTest>

#include <algorithm>

using namespace PlasmaLyrics;

class NeteaseProviderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesSearchCandidates();
    void parsesTransNamesIntoAlternateTitles();
    void handlesDirtyLyricAndEmptyWordData();
    void creditsFromThePreV1EndpointStillFilter();
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

QTEST_GUILESS_MAIN(NeteaseProviderTest)
#include "tst_neteaseprovider.moc"
