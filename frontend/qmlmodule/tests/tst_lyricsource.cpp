#include "frontend/qmlmodule/lyricsource.h"
#include "core/lyric/lyricmodel.h"
#include "core/store/lyricstore.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

using namespace PlasmaLyrics;

// Every LyricSource in this file is constructed with an explicit storePath
// (a QTemporaryDir path, via the constructor's storePath parameter) so that
// none of these tests -- old or new -- ever open the real
// ~/.local/share/plasma-lyrics/lyrics.db. The constructor synchronously
// opens a LyricStore at that path to read the global offset cache (DESIGN.md
// decision 41), so leaving storePath at its default in a test would touch
// the real database even for cases that predate the global offset feature
// and never reference it directly.
class LyricSourceTest : public QObject
{
    Q_OBJECT

private:
    static void writeSnapshot(const QString &path, int seq, qint64 anchor, const QString &text,
                              qint64 pid = QCoreApplication::applicationPid(), qint64 endMs = 2000,
                              int offsetMs = 0, const QString &provider = QStringLiteral("netease"),
                              const QString &trackId = QStringLiteral("1"))
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        const QJsonObject root{
            {QStringLiteral("schema"), 1},
            {QStringLiteral("seq"), seq},
            {QStringLiteral("daemon"), QJsonObject{{QStringLiteral("pid"), pid}}},
            {QStringLiteral("track"), QJsonObject{{QStringLiteral("title"), QStringLiteral("song")},
                                                   {QStringLiteral("artists"), QJsonArray{QStringLiteral("artist")}},
                                                   {QStringLiteral("ref"), QJsonObject{{QStringLiteral("provider"), provider},
                                                                                     {QStringLiteral("trackId"), trackId}}}}},
            {QStringLiteral("playback"), QJsonObject{{QStringLiteral("status"), QStringLiteral("Playing")},
                                                      {QStringLiteral("positionUs"), 1000000},
                                                      {QStringLiteral("anchorMonotonicNs"), anchor},
                                                      {QStringLiteral("rate"), 1.0}}},
            {QStringLiteral("lyric"), QJsonObject{{QStringLiteral("state"), QStringLiteral("ok")},
                                                   {QStringLiteral("offsetMs"), offsetMs},
                                                   {QStringLiteral("lines"), QJsonArray{lineToJson({1000, endMs, text, std::nullopt, std::nullopt})}}}}};
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
        LyricSource source([&now] { return now; }, directory.filePath(QStringLiteral("lyrics.db")));
        source.setSnapshotPath(path);
        QCOMPARE(source.currentText(), QStringLiteral("first"));
        now += 500000000;
        QMetaObject::invokeMethod(&source, "reload");
        QTRY_VERIFY(source.currentPositionMs() >= 1000);
    }

    void wakesUpAtTheLineBoundary()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        qint64 now = 1000000000;
        // The line ends 50 ms past the anchored position. Nothing polls any more,
        // so the source only clears it if it armed a timer on that boundary.
        writeSnapshot(path, 1, now, QStringLiteral("first"), QCoreApplication::applicationPid(), 1050);
        LyricSource source([&now] { return now; }, directory.filePath(QStringLiteral("lyrics.db")));
        source.setSnapshotPath(path);
        QCOMPARE(source.currentText(), QStringLiteral("first"));
        now += 100000000;
        QTRY_VERIFY(source.currentText().isEmpty());
    }

    void rearmsAfterAtomicRename()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(path, 1, 1000000000, QStringLiteral("first"));
        LyricSource source([] { return 1000000000LL; }, directory.filePath(QStringLiteral("lyrics.db")));
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
        LyricSource source([] { return 1000000000LL; }, directory.filePath(QStringLiteral("lyrics.db")));
        source.setSnapshotPath(path);
        QVERIFY(!source.serviceAvailable());
        QVERIFY(source.stale());
    }

    // The tests below exercise DESIGN.md decision 41 (the global offset)
    // directly, on top of the storePath injection every test in this file
    // already uses.

    void perTrackOffsetIsUsedWhenGlobalOffsetDisabled()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        const QString storePath = directory.filePath(QStringLiteral("lyrics.db"));
        // Global offset defaults to disabled -- an empty store is enough.
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 300);

        LyricSource source([] { return 1000000000LL; }, storePath);
        source.setSnapshotPath(snapshotPath);

        QVERIFY(!source.globalOffsetEnabled());
        QCOMPARE(source.offsetMs(), 300);
    }

    void globalOffsetOverridesPerTrackValueWhenEnabled()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        const QString storePath = directory.filePath(QStringLiteral("lyrics.db"));
        LyricStore store(storePath);
        QVERIFY(store.open());
        QVERIFY(store.setGlobalOffsetEnabled(true));
        QVERIFY(store.setGlobalOffsetMs(750));
        // The snapshot's own per-track offsetMs (300) must be ignored while
        // the global switch is on.
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 300);

        LyricSource source([] { return 1000000000LL; }, storePath);
        source.setSnapshotPath(snapshotPath);

        QVERIFY(source.globalOffsetEnabled());
        QCOMPARE(source.offsetMs(), 750);
    }

    void adjustOffsetWritesThePerTrackTableWhenGlobalOffsetDisabled()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        const QString storePath = directory.filePath(QStringLiteral("lyrics.db"));
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"));

        LyricSource source([] { return 1000000000LL; }, storePath);
        source.setSnapshotPath(snapshotPath);
        QVERIFY(source.adjustOffset(500));
        QCOMPARE(source.offsetMs(), 500);

        LyricStore store(storePath);
        QVERIFY(store.open());
        QCOMPARE(store.offset({QStringLiteral("netease"), QStringLiteral("1"), 0}), 500);
        QCOMPARE(store.globalOffsetMs(), 0);
    }

    void adjustOffsetWritesTheGlobalTableWhenGlobalOffsetEnabled()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        const QString storePath = directory.filePath(QStringLiteral("lyrics.db"));
        LyricStore store(storePath);
        QVERIFY(store.open());
        QVERIFY(store.setGlobalOffsetEnabled(true));
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"));

        LyricSource source([] { return 1000000000LL; }, storePath);
        source.setSnapshotPath(snapshotPath);
        QVERIFY(source.adjustOffset(500));
        QCOMPARE(source.offsetMs(), 500);

        QCOMPARE(store.globalOffsetMs(), 500);
        // The per-track table must stay untouched -- adjusting in global
        // mode never keys off (provider, trackId).
        QCOMPARE(store.offset({QStringLiteral("netease"), QStringLiteral("1"), 0}), 0);
    }

    void healthPollSyncsPerTrackOffsetFromAnotherInstance()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        const QString storePath = directory.filePath(QStringLiteral("lyrics.db"));
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"));

        LyricSource source([] { return 1000000000LL; }, storePath);
        source.setSnapshotPath(snapshotPath);
        QCOMPARE(source.offsetMs(), 0);

        // Simulate a second widget instance adjusting the shared per-track
        // offset directly through the database, bypassing this LyricSource.
        LyricStore store(storePath);
        QVERIFY(store.open());
        QVERIFY(store.setOffset({QStringLiteral("netease"), QStringLiteral("1"), 0}, 400));

        QTRY_COMPARE(source.offsetMs(), 400);
    }

    void healthPollSyncsGlobalOffsetFromAnotherInstance()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        const QString storePath = directory.filePath(QStringLiteral("lyrics.db"));
        LyricStore store(storePath);
        QVERIFY(store.open());
        QVERIFY(store.setGlobalOffsetEnabled(true));
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"));

        LyricSource source([] { return 1000000000LL; }, storePath);
        source.setSnapshotPath(snapshotPath);
        QCOMPARE(source.offsetMs(), 0);

        // Simulate a second widget instance adjusting the shared global
        // offset directly through the database.
        QVERIFY(store.setGlobalOffsetMs(600));

        QTRY_COMPARE(source.offsetMs(), 600);
    }

    void disablingGlobalOffsetRestoresThePerTrackValueUnchanged()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        const QString storePath = directory.filePath(QStringLiteral("lyrics.db"));
        LyricStore store(storePath);
        QVERIFY(store.open());
        QVERIFY(store.setGlobalOffsetEnabled(true));
        QVERIFY(store.setGlobalOffsetMs(900));
        // The per-track table and the snapshot's own offsetMs must agree --
        // in real operation the daemon writes both from the same value, and
        // the 2 s poll re-reads the per-track table regardless of the global
        // switch (that half is unchanged pre-existing behaviour), so a
        // mismatch here would just be an unrealistic test fixture.
        QVERIFY(store.setOffset({QStringLiteral("netease"), QStringLiteral("1"), 0}, 300));
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 300);

        LyricSource source([] { return 1000000000LL; }, storePath);
        source.setSnapshotPath(snapshotPath);
        QCOMPARE(source.offsetMs(), 900);

        QVERIFY(store.setGlobalOffsetEnabled(false));
        QTRY_COMPARE(source.offsetMs(), 300);
    }

    void canAdjustOffsetIsRelaxedInGlobalModeWithoutATrackRef()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        const QString storePath = directory.filePath(QStringLiteral("lyrics.db"));
        LyricStore store(storePath);
        QVERIFY(store.open());
        QVERIFY(store.setGlobalOffsetEnabled(true));
        // No provider/trackId: an instrumental track, or one lyrics search
        // never resolved.
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 0, QString(), QString());

        LyricSource source([] { return 1000000000LL; }, storePath);
        source.setSnapshotPath(snapshotPath);

        QVERIFY(source.serviceAvailable());
        QVERIFY(source.canAdjustOffset());
        QVERIFY(source.adjustOffset(200));
        QCOMPARE(store.globalOffsetMs(), 200);
    }

    void canAdjustOffsetStillRequiresATrackRefWhenGlobalOffsetDisabled()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        const QString storePath = directory.filePath(QStringLiteral("lyrics.db"));
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 0, QString(), QString());

        LyricSource source([] { return 1000000000LL; }, storePath);
        source.setSnapshotPath(snapshotPath);

        QVERIFY(source.serviceAvailable());
        QVERIFY(!source.canAdjustOffset());
        QVERIFY(!source.adjustOffset(200));
    }
};

QTEST_GUILESS_MAIN(LyricSourceTest)
#include "tst_lyricsource.moc"
