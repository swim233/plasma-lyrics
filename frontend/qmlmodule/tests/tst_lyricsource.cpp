#include "frontend/qmlmodule/lyricsource.h"
#include "core/lyric/lyricmodel.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

using namespace PlasmaLyrics;

class LyricSourceTest : public QObject
{
    Q_OBJECT

private:
    static void writeSnapshot(const QString &path, int seq, qint64 anchor, const QString &text,
                              qint64 pid = QCoreApplication::applicationPid())
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        const QJsonObject root{
            {QStringLiteral("schema"), 1},
            {QStringLiteral("seq"), seq},
            {QStringLiteral("daemon"), QJsonObject{{QStringLiteral("pid"), pid}}},
            {QStringLiteral("track"), QJsonObject{{QStringLiteral("title"), QStringLiteral("song")},
                                                   {QStringLiteral("artists"), QJsonArray{QStringLiteral("artist")}},
                                                   {QStringLiteral("ref"), QJsonObject{{QStringLiteral("provider"), QStringLiteral("netease")},
                                                                                     {QStringLiteral("trackId"), QStringLiteral("1")}}}}},
            {QStringLiteral("playback"), QJsonObject{{QStringLiteral("status"), QStringLiteral("Playing")},
                                                      {QStringLiteral("positionUs"), 1000000},
                                                      {QStringLiteral("anchorMonotonicNs"), anchor},
                                                      {QStringLiteral("rate"), 1.0}}},
            {QStringLiteral("lyric"), QJsonObject{{QStringLiteral("state"), QStringLiteral("ok")},
                                                   {QStringLiteral("offsetMs"), 0},
                                                   {QStringLiteral("lines"), QJsonArray{lineToJson({1000, 2000, text, std::nullopt, std::nullopt})}}}}};
        QSaveFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        QVERIFY(file.commit());
    }

private Q_SLOTS:
    void advancesWithInjectedMonotonicClock()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        qint64 now = 1000000000;
        writeSnapshot(path, 1, now, QStringLiteral("first"));
        LyricSource source([&now] { return now; });
        source.setSnapshotPath(path);
        QCOMPARE(source.currentText(), QStringLiteral("first"));
        now += 500000000;
        QMetaObject::invokeMethod(&source, "reload");
        QTRY_VERIFY(source.currentPositionMs() >= 1000);
    }

    void rearmsAfterAtomicRename()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(path, 1, 1000000000, QStringLiteral("first"));
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);
        QCOMPARE(source.currentText(), QStringLiteral("first"));
        writeSnapshot(path, 2, 1000000000, QStringLiteral("second"));
        QTRY_COMPARE(source.currentText(), QStringLiteral("second"));
    }

    void rejectsSnapshotFromDeadDaemon()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(path, 1, 1000000000, QStringLiteral("stale"), 999999999);
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);
        QVERIFY(!source.serviceAvailable());
        QVERIFY(source.stale());
    }
};

QTEST_GUILESS_MAIN(LyricSourceTest)
#include "tst_lyricsource.moc"
