#include "daemon/src/snapshot.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace PlasmaLyrics;

class SnapshotTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void sequenceSurvivesAtomicRenameAndRestart()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        ResolvedLyric lyric{QStringLiteral("ok"), TrackRef{QStringLiteral("netease"), QStringLiteral("1"), 1},
                            {{{1000, 2000, QStringLiteral("line"), std::nullopt, std::nullopt}}, 0, false}};
        MprisState player;
        player.fingerprint = QStringLiteral("mediaSrc:test");
        player.title = QStringLiteral("song");
        player.playbackStatus = QStringLiteral("Playing");
        {
            SnapshotWriter writer(path);
            QVERIFY(writer.write(player, lyric));
            QVERIFY(writer.write(player, lyric));
            QCOMPARE(writer.sequence(), 2);
        }
        SnapshotWriter restored(path);
        QVERIFY(restored.write(player, lyric));
        QCOMPARE(restored.sequence(), 3);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("schema")).toInt(), 1);
        QCOMPARE(root.value(QStringLiteral("seq")).toInt(), 3);
        QVERIFY(root.value(QStringLiteral("lyric")).toObject().value(QStringLiteral("lines")).toArray().first()
                    .toObject().value(QStringLiteral("words")).isNull());
    }

    void writesProviderStateAndDocumentProvenanceWithoutChangingSchema()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        LyricDocument document;
        document.hasWords = true;
        document.lines = {{1000, 2000, QStringLiteral("word line"),
                           QStringLiteral("translated"),
                           QList<LyricWord>{{1000, 1500, QStringLiteral("word")},
                                            {1500, 2000, QStringLiteral(" line")}}}};
        document.metadata = {{QStringLiteral("source"), QStringLiteral("amll")},
                             {QStringLiteral("contentId"), QStringLiteral("123.ttml")}};
        ResolvedLyric lyric{QStringLiteral("ok"),
                            TrackRef{QStringLiteral("netease"), QStringLiteral("1"), 1},
                            document,
                            QStringLiteral("amll"), QStringLiteral("amll"), true,
                            {QStringLiteral("netease"), QStringLiteral("amll")}};
        lyric.globalOffsetEnabled = true;
        lyric.document.offsetMs = 750;
        lyric.switchingProvider = QStringLiteral("amll");
        MprisState player;
        player.fingerprint = QStringLiteral("mediaSrc:test");

        SnapshotWriter writer(path);
        QVERIFY(writer.write(player, lyric));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("schema")).toInt(), 1);
        const auto stored = root.value(QStringLiteral("lyric")).toObject();
        QCOMPARE(stored.value(QStringLiteral("preferredProvider")).toString(),
                 QStringLiteral("amll"));
        QCOMPARE(stored.value(QStringLiteral("effectivePreferredProvider")).toString(),
                 QStringLiteral("amll"));
        QCOMPARE(stored.value(QStringLiteral("actualProvider")).toString(),
                 QStringLiteral("netease"));
        QVERIFY(stored.value(QStringLiteral("temporaryFallback")).toBool());
        QVERIFY(stored.value(QStringLiteral("globalOffsetEnabled")).toBool());
        QCOMPARE(stored.value(QStringLiteral("offsetMs")).toInt(), 750);
        QCOMPARE(stored.value(QStringLiteral("switchingProvider")).toString(),
                 QStringLiteral("amll"));
        QCOMPARE(stored.value(QStringLiteral("availableProviders")).toArray().size(), 2);
        QCOMPARE(stored.value(QStringLiteral("metadata")).toObject(), document.metadata);
        const auto line = stored.value(QStringLiteral("lines")).toArray().first().toObject();
        QCOMPARE(line.value(QStringLiteral("translation")).toString(),
                 QStringLiteral("translated"));
        QCOMPARE(line.value(QStringLiteral("words")).toArray().size(), 2);
    }
};

QTEST_GUILESS_MAIN(SnapshotTest)
#include "tst_snapshot.moc"
