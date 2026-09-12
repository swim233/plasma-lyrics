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
                           QStringLiteral("translated"), std::nullopt,
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

    void carriesRomanizationThroughTheSnapshotAtBothLevels()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        LyricDocument document;
        document.hasWords = true;
        document.lines = {{0, 300, QStringLiteral("惑星!"), std::nullopt,
                           QStringLiteral("waku sei"),
                           QList<LyricWord>{{0, 116, QStringLiteral("惑"), QStringLiteral("waku")},
                                            {116, 232, QStringLiteral("星"), QStringLiteral("sei")},
                                            {232, 300, QStringLiteral("!")}}}};
        ResolvedLyric lyric{QStringLiteral("ok"),
                            TrackRef{QStringLiteral("qq"), QStringLiteral("204530409"), 1},
                            document, QStringLiteral("qq"), QStringLiteral("qq"), false,
                            {QStringLiteral("qq")}};
        MprisState player;
        player.fingerprint = QStringLiteral("mediaSrc:test");

        SnapshotWriter writer(path);
        QVERIFY(writer.write(player, lyric));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        const auto line = root.value(QStringLiteral("lyric")).toObject()
                              .value(QStringLiteral("lines")).toArray().first().toObject();
        QCOMPARE(line.value(QStringLiteral("romanization")).toString(),
                 QStringLiteral("waku sei"));
        const auto words = line.value(QStringLiteral("words")).toArray();
        QCOMPARE(words.at(0).toObject().value(QStringLiteral("romanization")).toString(),
                 QStringLiteral("waku"));
        QCOMPARE(words.at(1).toObject().value(QStringLiteral("romanization")).toString(),
                 QStringLiteral("sei"));
        // A word without romanization carries no key at all rather than an
        // explicit null: word arrays run to hundreds of entries and almost no
        // source fills this in.
        QVERIFY(!words.at(2).toObject().contains(QStringLiteral("romanization")));
        // And the whole line survives the trip back through lineFromJson.
        const auto restored = lineFromJson(line);
        QVERIFY(restored.has_value());
        QCOMPARE(*restored, document.lines.first());
    }
};

QTEST_GUILESS_MAIN(SnapshotTest)
#include "tst_snapshot.moc"
