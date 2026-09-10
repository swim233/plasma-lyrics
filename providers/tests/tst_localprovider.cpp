#include "providers/local/localprovider.h"

#include "core/match/matcher.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace PlasmaLyrics;

namespace {

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(contents), contents.size());
}

ProviderSearchResult search(LocalProvider &provider, const TrackQuery &query)
{
    ProviderSearchResult result;
    provider.search(query, [&](ProviderSearchResult value) { result = std::move(value); });
    return result;
}

} // namespace

class LocalProviderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void scansMetadataAndUsesMatcherNormalization()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        writeFile(directory.filePath(QStringLiteral("unrelated-name.lrc")),
                  "[ti:Ａ Song！]\n[ar:The Artist]\n[al:The Album]\n"
                  "[length:03:30]\n[00:01.000]local line\n");
        LocalProvider provider(directory.path());
        const TrackQuery query{QStringLiteral("A Song"), {QStringLiteral("The Artist")},
                               QStringLiteral("The Album"), 210000};
        const auto result = search(provider, query);
        QCOMPARE(result.candidates.size(), 1);
        const auto chosen = chooseMatch(rankCandidates(query, result.candidates), false);
        QVERIFY(chosen.has_value());
        QCOMPARE(chosen->candidate.lengthMs, 210000);

        ProviderFetchResult fetched;
        provider.fetch(chosen->candidate.contentId,
                       [&](ProviderFetchResult value) { fetched = std::move(value); });
        QVERIFY(fetched.document.has_value());
        QCOMPARE(fetched.document->lines.first().text, QStringLiteral("local line"));
    }

    void rejectsClearlyDifferentTrack()
    {
        QTemporaryDir directory;
        writeFile(directory.filePath(QStringLiteral("Other - Different.lrc")),
                  "[00:01.000]line\n");
        LocalProvider provider(directory.path());
        const TrackQuery query{QStringLiteral("Expected"), {QStringLiteral("Artist")}, {}, 0};
        const auto result = search(provider, query);
        QVERIFY(!chooseMatch(rankCandidates(query, result.candidates), false));
    }

    void sidecarWinsAndRemoteMediaDoesNotUseIt()
    {
        QTemporaryDir directory;
        const QString audio = directory.filePath(QStringLiteral("song.flac"));
        writeFile(audio, "audio");
        writeFile(directory.filePath(QStringLiteral("song.lrc")),
                  "[ti:stale copied title]\n[ar:someone else]\n"
                  "[00:01.000]sidecar line\n");
        const QString lyricsDirectory = directory.filePath(QStringLiteral("library"));
        QVERIFY(QDir().mkpath(lyricsDirectory));
        LocalProvider provider(lyricsDirectory);

        TrackQuery query{QStringLiteral("Song"), {QStringLiteral("Artist")}, {}, 180000};
        query.mediaSrc = QUrl::fromLocalFile(audio).toString();
        const auto localResult = search(provider, query);
        QCOMPARE(localResult.candidates.size(), 1);
        const auto chosen = chooseMatch(rankCandidates(query, localResult.candidates), false);
        QVERIFY(chosen.has_value());
        QVERIFY(!localResult.cacheableMiss);

        query.mediaSrc = QStringLiteral("https://example.test/song.flac");
        const auto remoteResult = search(provider, query);
        QVERIFY(remoteResult.candidates.isEmpty());
        QVERIFY(remoteResult.cacheableMiss);
    }

    void sidecarMissIsNotCacheableAndDirectoryChangesVersion()
    {
        QTemporaryDir directory;
        const QString audio = directory.filePath(QStringLiteral("song.flac"));
        writeFile(audio, "audio");
        LocalProvider provider(directory.filePath(QStringLiteral("lyrics")));
        QVERIFY(QDir().mkpath(provider.lyricsDirectory()));
        TrackQuery query{QStringLiteral("Song"), {}, {}, 0};
        query.mediaSrc = QUrl::fromLocalFile(audio).toString();
        const QString before = provider.cacheVersion();
        const auto miss = search(provider, query);
        QVERIFY(miss.candidates.isEmpty());
        QVERIFY(!miss.cacheableMiss);

        writeFile(QDir(provider.lyricsDirectory()).filePath(QStringLiteral("Song.lrc")),
                  "[00:01.000]line\n");
        QVERIFY(provider.cacheVersion() != before);
    }
};

QTEST_GUILESS_MAIN(LocalProviderTest)
#include "tst_localprovider.moc"
