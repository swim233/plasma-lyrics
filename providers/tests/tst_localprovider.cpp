#include "providers/local/localprovider.h"

#include "core/match/matcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <algorithm>

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

QStringList *capturedMessages = nullptr;

void captureMessages(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (capturedMessages && type == QtDebugMsg) {
        capturedMessages->append(message);
    }
}

// Mirrors daemon/tests/tst_resolver.cpp's MessageCapture/DebugLoggingScope so
// the lcLocal side of the bilingual-pairing log line gets the same kind of
// exact-text assertion the lcResolver side already has.
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

class DebugLoggingScope
{
public:
    DebugLoggingScope() { QLoggingCategory::setFilterRules(QStringLiteral("plasmalyrics.*.debug=true")); }
    ~DebugLoggingScope() { QLoggingCategory::setFilterRules(QString()); }
};

bool logged(const QStringList &messages, const QString &needle)
{
    return std::any_of(messages.cbegin(), messages.cend(),
                       [&needle](const QString &message) { return message.contains(needle); });
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

    void recursivelyFindsLyricsInOrganizedDirectories()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalProvider provider(directory.filePath(QStringLiteral("lyrics")));
        const QString before = provider.cacheVersion();
        const QString albumDirectory = directory.filePath(
            QStringLiteral("lyrics/Artist/Album"));
        QVERIFY(QDir().mkpath(albumDirectory));
        writeFile(QDir(albumDirectory).filePath(QStringLiteral("Song.lrc")),
                  "[ti:Song]\n[ar:Artist]\n[al:Album]\n[00:01.000]nested line\n");

        QVERIFY(provider.cacheVersion() != before);
        const TrackQuery query{QStringLiteral("Song"), {QStringLiteral("Artist")},
                               QStringLiteral("Album"), 0};
        const auto result = search(provider, query);
        const auto chosen = chooseMatch(rankCandidates(query, result.candidates), false);

        QVERIFY(chosen.has_value());
        QVERIFY(chosen->candidate.contentId.endsWith(QStringLiteral("Artist/Album/Song.lrc")));
    }

    void unreadableFilesNeitherBecomeCandidatesNorChangeTheVersion()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString readable = directory.filePath(QStringLiteral("Readable.lrc"));
        writeFile(readable, "[00:01.000]line\n");
        LocalProvider provider(directory.path());
        const QString before = provider.cacheVersion();

        const QString unreadable = directory.filePath(QStringLiteral("Unreadable.lrc"));
        writeFile(unreadable, "[00:01.000]hidden\n");
        QVERIFY(QFile::setPermissions(unreadable, QFileDevice::WriteOwner));
        // uid 0 -- what the containerised CI jobs run as -- and anything else
        // holding CAP_DAC_OVERRIDE reads the file regardless of its mode bits,
        // leaving nothing to assert. Probe the file, not the uid.
        QFile probe(unreadable);
        if (probe.open(QIODevice::ReadOnly)) {
            probe.close();
            QVERIFY(QFile::setPermissions(unreadable,
                                          QFileDevice::ReadOwner | QFileDevice::WriteOwner));
            QSKIP("this process reads files whose owner read bit is cleared");
        }
        const QString after = provider.cacheVersion();
        const auto result = search(provider, TrackQuery{});

        QCOMPARE(after, before);
        QCOMPARE(result.candidates.size(), 1);
        QVERIFY(QFile::setPermissions(unreadable,
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    }

    void brokenSidecarFallsBackToTheLyricsDirectory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString audio = directory.filePath(QStringLiteral("song.flac"));
        writeFile(audio, "audio");
        writeFile(directory.filePath(QStringLiteral("song.lrc")),
                  "[ti:Song]\n[ar:Artist]\nthis sidecar has no timed lines\n");
        const QString lyricsDirectory = directory.filePath(QStringLiteral("lyrics/Artist"));
        QVERIFY(QDir().mkpath(lyricsDirectory));
        const QString goodLyrics = QDir(lyricsDirectory).filePath(QStringLiteral("Song.lrc"));
        writeFile(goodLyrics,
                  "[ti:Song]\n[ar:Artist]\n[00:01.000]directory line\n");
        LocalProvider provider(directory.filePath(QStringLiteral("lyrics")));
        TrackQuery query{QStringLiteral("Song"), {QStringLiteral("Artist")}, {}, 0};
        query.mediaSrc = QUrl::fromLocalFile(audio).toString();

        const auto result = search(provider, query);
        const auto chosen = chooseMatch(rankCandidates(query, result.candidates), false);
        QVERIFY(chosen.has_value());
        QCOMPARE(chosen->candidate.contentId, goodLyrics);
        QVERIFY(!result.cacheableMiss);

        ProviderFetchResult fetched;
        provider.fetch(chosen->candidate.contentId,
                       [&](ProviderFetchResult value) { fetched = std::move(value); });
        QVERIFY(fetched.document.has_value());
        QCOMPARE(fetched.document->lines.first().text, QStringLiteral("directory line"));
    }

    void reusesParsedIndexWhileVersionIsUnchanged()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("song.lrc"));
        const QByteArray original("[ti:Original]\n[ar:Artist]\n[00:01.000]line\n");
        const QByteArray changed("[ti:Changed!]\n[ar:Artist]\n[00:01.000]line\n");
        QCOMPARE(changed.size(), original.size());
        writeFile(path, original);
        const QDateTime originalModified = QFileInfo(path).lastModified();
        LocalProvider provider(directory.path());
        const TrackQuery query{QStringLiteral("Original"), {QStringLiteral("Artist")}, {}, 0};

        const auto first = search(provider, query);
        QCOMPARE(first.candidates.size(), 1);
        QCOMPARE(first.candidates.first().title, QStringLiteral("Original"));

        writeFile(path, changed);
        QFile rewritten(path);
        QVERIFY(rewritten.open(QIODevice::ReadWrite));
        QVERIFY(rewritten.setFileTime(originalModified, QFileDevice::FileModificationTime));
        rewritten.close();
        QCOMPARE(provider.cacheVersion(), first.cacheVersion);

        const auto second = search(provider, query);
        QCOMPARE(second.candidates.size(), 1);
        QCOMPARE(second.candidates.first().title, QStringLiteral("Original"));
    }

    void fetchPairsBilingualLocalFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("song.lrc"));
        writeFile(path, "[00:01.000]你好\n[00:01.000]Hello\n");
        LocalProvider provider(directory.path());

        DebugLoggingScope debugScope;
        MessageCapture capture;
        ProviderFetchResult fetched;
        provider.fetch(path, [&](ProviderFetchResult value) { fetched = std::move(value); });
        QVERIFY(fetched.document.has_value());
        QCOMPARE(fetched.document->lines.size(), 1);
        QCOMPARE(fetched.document->lines.first().text, QStringLiteral("你好"));
        QVERIFY(fetched.document->lines.first().translation.has_value());
        QCOMPARE(*fetched.document->lines.first().translation, QStringLiteral("Hello"));
        QVERIFY(logged(capture.messages(),
                       QStringLiteral("bilingual pairing: path=\"%1\" collapsed=1").arg(path)));
    }

    void limitsCandidatesPassedToResolver()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        for (int index = 0; index < 64; ++index) {
            writeFile(directory.filePath(QStringLiteral("Artist - Song %1.lrc").arg(index)),
                      QByteArray("[ti:Song ") + QByteArray::number(index)
                          + "]\n[ar:Artist]\n[00:01.000]line\n");
        }
        LocalProvider provider(directory.path());
        const TrackQuery query{QStringLiteral("Song 63"), {QStringLiteral("Artist")}, {}, 0};

        const auto result = search(provider, query);

        QCOMPARE(result.candidates.size(), 50);
        QVERIFY2(result.candidates.size() < 64,
                 "Local search must not hand the whole directory to Resolver ranking");
    }
};

QTEST_GUILESS_MAIN(LocalProviderTest)
#include "tst_localprovider.moc"
