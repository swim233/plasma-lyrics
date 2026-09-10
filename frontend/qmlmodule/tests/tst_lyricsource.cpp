#include "frontend/qmlmodule/lyricsource.h"
#include "core/lyric/lyricmodel.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <KLocalizedString>

using namespace PlasmaLyrics;

class LyricSourceTest : public QObject
{
    Q_OBJECT

private:
    static void writeSnapshot(const QString &path, int seq, qint64 anchor, const QString &text,
                              qint64 pid = QCoreApplication::applicationPid(), qint64 endMs = 2000,
                              int offsetMs = 0, const QString &provider = QStringLiteral("netease"),
                              const QString &trackId = QStringLiteral("1"),
                              const QString &preferredProvider = {},
                              const QString &effectivePreferredProvider = QStringLiteral("netease"),
                              const QString &actualProvider = QStringLiteral("netease"),
                              bool temporaryFallback = false,
                              const QStringList &availableProviders = {
                                  QStringLiteral("netease"), QStringLiteral("amll")},
                              bool globalOffsetEnabled = false)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        const QJsonObject root{
            {QStringLiteral("schema"), 1},
            {QStringLiteral("seq"), seq},
            {QStringLiteral("daemon"), QJsonObject{{QStringLiteral("pid"), pid}}},
            {QStringLiteral("track"), QJsonObject{{QStringLiteral("fingerprint"), QStringLiteral("mediaSrc:test")},
                                                   {QStringLiteral("title"), QStringLiteral("song")},
                                                   {QStringLiteral("artists"), QJsonArray{QStringLiteral("artist")}},
                                                   {QStringLiteral("ref"), QJsonObject{{QStringLiteral("provider"), provider},
                                                                                     {QStringLiteral("trackId"), trackId}}}}},
            {QStringLiteral("playback"), QJsonObject{{QStringLiteral("status"), QStringLiteral("Playing")},
                                                      {QStringLiteral("positionUs"), 1000000},
                                                      {QStringLiteral("anchorMonotonicNs"), anchor},
                                                      {QStringLiteral("rate"), 1.0}}},
            {QStringLiteral("lyric"), QJsonObject{{QStringLiteral("state"), QStringLiteral("ok")},
                                                   {QStringLiteral("offsetMs"), offsetMs},
                                                   {QStringLiteral("preferredProvider"), preferredProvider},
                                                   {QStringLiteral("effectivePreferredProvider"), effectivePreferredProvider},
                                                   {QStringLiteral("actualProvider"), actualProvider},
                                                   {QStringLiteral("temporaryFallback"), temporaryFallback},
                                                   {QStringLiteral("globalOffsetEnabled"), globalOffsetEnabled},
                                                   {QStringLiteral("availableProviders"),
                                                    QJsonArray::fromStringList(availableProviders)},
                                                   {QStringLiteral("lines"), QJsonArray{lineToJson({1000, endMs, text, std::nullopt, std::nullopt})}}}}};
        QSaveFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        QVERIFY(file.commit());
    }

private Q_SLOTS:
    // providerDisplayName() goes through i18nd(), so the expected strings below
    // are only stable when the catalogue lookup is pinned to the source
    // language. Without this the suite fails on any machine whose session
    // locale has an installed translation -- including this project's own
    // plasma-lyrics-git install, which puts zh_CN into /usr/share/locale.
    void initTestCase()
    {
        KLocalizedString::setLanguages({QStringLiteral("en_US")});
    }

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

    void exposesPreferredActualAndFallbackProviderState()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(path, 1, 1000000000, QStringLiteral("fallback line"),
                      QCoreApplication::applicationPid(), 2000, 0,
                      QStringLiteral("netease"), QStringLiteral("1"),
                      QStringLiteral("amll"), QStringLiteral("amll"),
                      QStringLiteral("netease"), true,
                      {QStringLiteral("netease"), QStringLiteral("amll")});
        LyricSource source([] { return 1000000000LL; },
                           directory.filePath(QStringLiteral("lyrics.db")));

        source.setSnapshotPath(path);

        QCOMPARE(source.fingerprint(), QStringLiteral("mediaSrc:test"));
        QCOMPARE(source.preferredProvider(), QStringLiteral("amll"));
        QCOMPARE(source.effectivePreferredProvider(), QStringLiteral("amll"));
        QCOMPARE(source.actualProvider(), QStringLiteral("netease"));
        QVERIFY(source.temporaryFallback());
        QCOMPARE(source.availableProviders(),
                 QStringList({QStringLiteral("netease"), QStringLiteral("amll")}));
        QVERIFY(source.canControlProvider());
        QVERIFY(!source.setPreferredProvider(QStringLiteral("missing")));
    }

    void providerDisplayNamesCoverImportedAndUnknownSources()
    {
        QTemporaryDir directory;
        LyricSource source([] { return 1000000000LL; },
                           directory.filePath(QStringLiteral("lyrics.db")));

        QCOMPARE(source.providerDisplayName(QStringLiteral("waylyrics")),
                 QStringLiteral("Waylyrics import"));
        QCOMPARE(source.providerDisplayName(QStringLiteral("amll")),
                 QStringLiteral("AMLL"));
        const QString unknown = source.providerDisplayName(QStringLiteral("future-provider"));
        QVERIFY(unknown.contains(QStringLiteral("future-provider")));
        QVERIFY(unknown != source.providerDisplayName(QString()));
    }

    void unavailableControlServiceReportsAnAsynchronousFailure()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(path, 1, 1000000000, QStringLiteral("line"));
        LyricSource source([] { return 1000000000LL; },
                           directory.filePath(QStringLiteral("lyrics.db")));
        source.setSnapshotPath(path);
        QSignalSpy failed(&source, &LyricSource::controlFailed);

        QVERIFY(source.research());
        QVERIFY(source.controlInProgress());
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!source.controlInProgress());
        QVERIFY(!source.controlError().isEmpty());
        QVERIFY(source.canControlProvider());
    }

    void perTrackOffsetIsUsedWhenGlobalOffsetDisabled()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 300);

        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(snapshotPath);

        QVERIFY(!source.globalOffsetEnabled());
        QCOMPARE(source.offsetMs(), 300);
    }

    void effectiveGlobalOffsetComesFromSnapshot()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 750,
                      QStringLiteral("netease"), QStringLiteral("1"), {},
                      QStringLiteral("netease"), QStringLiteral("netease"), false,
                      {QStringLiteral("local"), QStringLiteral("netease")}, true);

        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(snapshotPath);

        QVERIFY(source.globalOffsetEnabled());
        QCOMPARE(source.offsetMs(), 750);
    }

    void allInstancesConsumeTheSamePublishedOffset()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"));
        LyricSource panel([] { return 1000000000LL; });
        LyricSource desktop([] { return 1000000000LL; });
        panel.setSnapshotPath(snapshotPath);
        desktop.setSnapshotPath(snapshotPath);
        QCOMPARE(panel.offsetMs(), 0);
        QCOMPARE(desktop.offsetMs(), 0);

        writeSnapshot(snapshotPath, 2, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 500);

        QTRY_COMPARE(panel.offsetMs(), 500);
        QTRY_COMPARE(desktop.offsetMs(), 500);
    }

    void canAdjustOffsetStillRequiresATrackRefWhenGlobalOffsetDisabled()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 0, QString(), QString());

        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(snapshotPath);

        QVERIFY(source.serviceAvailable());
        QVERIFY(!source.canAdjustOffset());
        QVERIFY(!source.adjustOffset(200));
    }
};

QTEST_GUILESS_MAIN(LyricSourceTest)
#include "tst_lyricsource.moc"
