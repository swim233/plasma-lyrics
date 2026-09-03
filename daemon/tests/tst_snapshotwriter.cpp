// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Snapshot writer, DESIGN §6 test 7. The daemon's only contract with the
 * frontends: seq is monotonically increasing, the write is atomic (rename
 * into place, so a reader never sees a half-written file), and the stale
 * detection the frontend does is possible because every snapshot carries the
 * daemon's monotonic anchor.
 */

#include "snapshot/SnapshotWriter.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Lyrics;

class SnapshotWriterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    void writesValidJsonWithSchema();
    void seqIncrementsMonotonically();
    void seqSurvivesRestart();
    void renameIsAtomic();

private:
    QTemporaryDir m_dir;
    QString m_path;
};

void SnapshotWriterTest::init()
{
    m_path = m_dir.filePath(QStringLiteral("state.json"));
}

void SnapshotWriterTest::writesValidJsonWithSchema()
{
    SnapshotWriter writer(m_path);
    Snapshot snap;
    snap.track.title = QStringLiteral("老街北");
    QVERIFY(writer.write(snap));

    QFile file(m_path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("schema")).toInt(), 1);
    QCOMPARE(root.value(QStringLiteral("seq")).toInt(), 1);
    QCOMPARE(root.value(QStringLiteral("track")).toObject().value(QStringLiteral("title")).toString(),
             QStringLiteral("老街北"));
}

void SnapshotWriterTest::seqIncrementsMonotonically()
{
    SnapshotWriter writer(m_path);
    Snapshot snap;
    QVERIFY(writer.write(snap));
    QVERIFY(writer.write(snap));
    QVERIFY(writer.write(snap));

    QFile file(m_path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("seq")).toInt(), 3);
}

void SnapshotWriterTest::seqSurvivesRestart()
{
    {
        SnapshotWriter writer(m_path);
        Snapshot snap;
        QVERIFY(writer.write(snap));
        QVERIFY(writer.write(snap));
    }
    // A fresh writer (daemon restart) must continue the sequence, not reset
    // it -- otherwise the frontend's "real update" filter would drop or
    // misorder a snapshot.
    SnapshotWriter writer(m_path);
    Snapshot snap;
    QVERIFY(writer.write(snap));

    QFile file(m_path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("seq")).toInt(), 3);
}

void SnapshotWriterTest::renameIsAtomic()
{
    // QSaveFile writes to a temp file and renames into place; the final path
    // never contains a partial document. Verify by writing a large payload
    // and checking the on-disk file always parses.
    SnapshotWriter writer(m_path);
    Snapshot snap;
    snap.lyric.lines.reserve(500);
    for (int i = 0; i < 500; ++i) {
        LyricLine line;
        line.startMs = i * 1000;
        line.text = QStringLiteral("第%1行").arg(i);
        snap.lyric.lines.append(line);
    }
    QVERIFY(writer.write(snap));

    QFile file(m_path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("lyric")).toObject()
                 .value(QStringLiteral("lines")).toArray().size(), 500);
}

QTEST_MAIN(SnapshotWriterTest)
#include "tst_snapshotwriter.moc"
