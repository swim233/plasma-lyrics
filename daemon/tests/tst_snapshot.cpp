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
};

QTEST_GUILESS_MAIN(SnapshotTest)
#include "tst_snapshot.moc"
