#include "frontend/qmlmodule/lyricsource.h"
#include "core/lyric/lyricmodel.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <KLocalizedString>

using namespace PlasmaLyrics;

class FakeOffsetControl final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.swim233.PlasmaLyrics.Control")

public:
    struct Call {
        QString provider;
        QString trackId;
        int offsetMs;
    };
    QList<Call> calls;
    QString result;

public Q_SLOTS:
    QString SetOffsetForTrack(const QString &provider, const QString &trackId, int offsetMs)
    {
        calls.append({provider, trackId, offsetMs});
        return result;
    }
};

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
                              bool globalOffsetEnabled = false,
                              const QString &switchingProvider = {},
                              int trackOffsetMs = 0)
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
                                                   {QStringLiteral("trackOffsetMs"), trackOffsetMs},
                                                   {QStringLiteral("preferredProvider"), preferredProvider},
                                                   {QStringLiteral("effectivePreferredProvider"), effectivePreferredProvider},
                                                   {QStringLiteral("actualProvider"), actualProvider},
                                                   {QStringLiteral("temporaryFallback"), temporaryFallback},
                                                   {QStringLiteral("globalOffsetEnabled"), globalOffsetEnabled},
                                                   {QStringLiteral("switchingProvider"), switchingProvider},
                                                   {QStringLiteral("availableProviders"),
                                                    QJsonArray::fromStringList(availableProviders)},
                                                   {QStringLiteral("lines"), QJsonArray{lineToJson({1000, endMs, text, std::nullopt, std::nullopt,
                                                                          std::nullopt})}}}}};
        QSaveFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        QVERIFY(file.commit());
    }

    static void writeWordSnapshot(const QString &path, int seq, const QList<LyricWord> &words)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        // Named assignment rather than aggregate braces: LyricLine has gained
        // two optional members already, and every positional site warned each
        // time. This one cannot warn and cannot silently shift meaning when
        // the next field lands between two identically typed neighbours.
        LyricLine line;
        line.startMs = 1000;
        line.endMs = 2000;
        line.text = QStringLiteral("worded");
        line.words = words;
        const QJsonObject root{
            {QStringLiteral("schema"), 1},
            {QStringLiteral("seq"), seq},
            {QStringLiteral("daemon"),
             QJsonObject{{QStringLiteral("pid"), QCoreApplication::applicationPid()}}},
            {QStringLiteral("track"),
             QJsonObject{{QStringLiteral("fingerprint"), QStringLiteral("mediaSrc:test")},
                         {QStringLiteral("title"), QStringLiteral("song")},
                         {QStringLiteral("artists"), QJsonArray{QStringLiteral("artist")}}}},
            {QStringLiteral("playback"),
             QJsonObject{{QStringLiteral("status"), QStringLiteral("Playing")},
                         {QStringLiteral("positionUs"), 1000000},
                         {QStringLiteral("anchorMonotonicNs"), 1000000000LL},
                         {QStringLiteral("rate"), 1.0}}},
            {QStringLiteral("lyric"),
             QJsonObject{{QStringLiteral("state"), QStringLiteral("ok")},
                         {QStringLiteral("offsetMs"), 0},
                         {QStringLiteral("availableProviders"), QJsonArray{}},
                         {QStringLiteral("lines"), QJsonArray{lineToJson(line)}}}}};
        QSaveFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        QVERIFY(file.commit());
    }

    // Writes an arbitrary multi-line document, positioned so `positionMs`
    // (relative to the anchor below) lands inside whichever line the caller
    // wants current. Used for the document-level (not line-level) synthetic
    // word gate and for the next line, which writeSnapshot()/writeWordSnapshot()
    // above -- both single-line -- cannot exercise.
    static void writeDocumentSnapshot(const QString &path, int seq, const LyricLines &lines,
                                      qint64 positionMs = 1000, int offsetMs = 0,
                                      const QString &state = QStringLiteral("ok"),
                                      const QString &fingerprint = QStringLiteral("mediaSrc:test"))
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QJsonArray lineArray;
        for (const auto &line : lines) {
            lineArray.append(lineToJson(line));
        }
        const QJsonObject root{
            {QStringLiteral("schema"), 1},
            {QStringLiteral("seq"), seq},
            {QStringLiteral("daemon"),
             QJsonObject{{QStringLiteral("pid"), QCoreApplication::applicationPid()}}},
            {QStringLiteral("track"),
             QJsonObject{{QStringLiteral("fingerprint"), fingerprint},
                         {QStringLiteral("title"), QStringLiteral("song")},
                         {QStringLiteral("artists"), QJsonArray{QStringLiteral("artist")}}}},
            {QStringLiteral("playback"),
             QJsonObject{{QStringLiteral("status"), QStringLiteral("Playing")},
                         {QStringLiteral("positionUs"), positionMs * 1000},
                         {QStringLiteral("anchorMonotonicNs"), 1000000000LL},
                         {QStringLiteral("rate"), 1.0}}},
            {QStringLiteral("lyric"),
             QJsonObject{{QStringLiteral("state"), state},
                         {QStringLiteral("offsetMs"), offsetMs},
                         {QStringLiteral("availableProviders"), QJsonArray{}},
                         {QStringLiteral("lines"), lineArray}}}};
        QSaveFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        QVERIFY(file.commit());
    }

    static LyricLine plainLine(qint64 startMs, qint64 endMs, const QString &text)
    {
        LyricLine line;
        line.startMs = startMs;
        line.endMs = endMs;
        line.text = text;
        return line;
    }

    // DESIGN.md decision 81. "two" starts 2150 ms after "one" ends, more
    // than a lead of 2000 ms, so that lead moves it in at 1050 ms, 50 ms past
    // the anchored position of 1000 ms; "three" starts 500 ms after "two"
    // ends and moves in as soon as "two" ends.
    static LyricLines leadDocument()
    {
        return {plainLine(500, 900, QStringLiteral("one")), plainLine(3050, 4000, QStringLiteral("two")),
                plainLine(4500, 5000, QStringLiteral("three"))};
    }

    // One entry per signal, with both lines' indices and texts as the
    // handler sees them.
    static void recordLineSignals(LyricSource &source, QStringList &log)
    {
        const auto record = [&source, &log](const QString &signal) {
            log.append(QStringLiteral("%1 -> current %2 [%3], next %4 [%5]")
                           .arg(signal)
                           .arg(source.currentLineIndex())
                           .arg(source.currentText())
                           .arg(source.nextLineIndex())
                           .arg(source.nextText()));
        };
        connect(&source, &LyricSource::currentLineChanged, &source,
                [record] { record(QStringLiteral("currentLineChanged")); });
        connect(&source, &LyricSource::nextLineChanged, &source,
                [record] { record(QStringLiteral("nextLineChanged")); });
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
        LyricSource source([&now] { return now; });
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
        LyricSource source([&now] { return now; });
        source.setSnapshotPath(path);
        QCOMPARE(source.currentText(), QStringLiteral("first"));
        now += 100000000;
        QTRY_VERIFY(source.currentText().isEmpty());
    }

    void aSongChangeLandingOnTheSameLineIndexStillNotifies()
    {
        // The line *index* is not enough to decide the current line changed.
        // Both snapshots below sit on index 0, so advance() used to leave
        // m_currentLine alone and emit nothing -- while currentText() already
        // returned the new song. Polling a getter therefore never caught it;
        // only a QML binding did, by going on showing the previous song.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(path, 1, 1000000000, QStringLiteral("first"));
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);
        QCOMPARE(source.currentText(), QStringLiteral("first"));
        QSignalSpy spy(&source, &LyricSource::currentLineChanged);
        writeSnapshot(path, 2, 1000000000, QStringLiteral("second"));
        QTRY_COMPARE(source.currentText(), QStringLiteral("second"));
        QCOMPARE(spy.size(), 1);
    }

    void theNextLineAdvancesWithTheCurrentOne()
    {
        // 20 ms apart, so the boundary timer alone carries the position through
        // the whole document; nothing else recomputes the lines.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        qint64 now = 1000000000;
        writeDocumentSnapshot(path, 1, {plainLine(1000, 1020, QStringLiteral("one")),
                                        plainLine(1020, 1040, QStringLiteral("two")),
                                        plainLine(1040, 1060, QStringLiteral("three"))});
        LyricSource source([&now] { return now; });
        source.setSnapshotPath(path);
        QCOMPARE(source.currentLineIndex(), 0);
        QCOMPARE(source.nextLineIndex(), 1);
        QCOMPARE(source.nextText(), QStringLiteral("two"));
        QStringList log;
        recordLineSignals(source, log);

        // Both indices and both texts are new when either signal fires, and
        // the current line's comes first: the animation compares the new
        // current line with the next line it recorded before (DESIGN.md
        // decision 28).
        now += 20000000;
        QTRY_COMPARE(source.currentLineIndex(), 1);
        QCOMPARE(source.currentText(), QStringLiteral("two"));
        QCOMPARE(source.nextText(), QStringLiteral("three"));
        QCOMPARE(log, QStringList({
            QStringLiteral("currentLineChanged -> current 1 [two], next 2 [three]"),
            QStringLiteral("nextLineChanged -> current 1 [two], next 2 [three]")}));

        log.clear();
        now += 20000000;
        QTRY_COMPARE(source.currentLineIndex(), 2);
        QVERIFY(source.nextText().isEmpty()); // the last line has nothing after it
        QCOMPARE(log, QStringList({
            QStringLiteral("currentLineChanged -> current 2 [three], next -1 []"),
            QStringLiteral("nextLineChanged -> current 2 [three], next -1 []")}));

        log.clear();
        now += 20000000;
        QTRY_COMPARE(source.currentLineIndex(), -1);
        QCOMPARE(source.nextLineIndex(), -1);
        QVERIFY(source.nextText().isEmpty());
        QCOMPARE(log, QStringList({
            QStringLiteral("currentLineChanged -> current -1 [], next -1 []")}));
    }

    void theLastOfLinesSharingAStartIsNextAndThenCurrent()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        qint64 now = 1000000000;
        writeDocumentSnapshot(path, 1, {plainLine(1000, 1020, QStringLiteral("one")),
                                        plainLine(1020, 1040, QStringLiteral("two")),
                                        plainLine(1020, 1040, QStringLiteral("two alt"))});
        LyricSource source([&now] { return now; });
        source.setSnapshotPath(path);
        QCOMPARE(source.nextLineIndex(), 2);
        QCOMPARE(source.nextText(), QStringLiteral("two alt"));
        QStringList log;
        recordLineSignals(source, log);

        now += 20000000;
        QTRY_COMPARE(source.currentLineIndex(), 2);
        QCOMPARE(source.currentText(), QStringLiteral("two alt"));
        QCOMPARE(log, QStringList({
            QStringLiteral("currentLineChanged -> current 2 [two alt], next -1 []"),
            QStringLiteral("nextLineChanged -> current 2 [two alt], next -1 []")}));
    }

    void theNextLineMovesWhileNothingIsCurrent()
    {
        // A seek from the intro into the interlude after the first line, then
        // an offset that puts the position back in the intro: no line is
        // current at any point, so only the next line notifies.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        const LyricLines lines{plainLine(2000, 3000, QStringLiteral("verse")),
                               plainLine(20000, 30000, QStringLiteral("chorus"))};
        writeDocumentSnapshot(path, 1, lines, 1000);
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);
        QCOMPARE(source.currentLineIndex(), -1);
        QCOMPARE(source.nextLineIndex(), 0);
        QCOMPARE(source.nextText(), QStringLiteral("verse"));
        QSignalSpy current(&source, &LyricSource::currentLineChanged);
        QSignalSpy next(&source, &LyricSource::nextLineChanged);

        writeDocumentSnapshot(path, 2, lines, 5000);
        QTRY_COMPARE(source.nextLineIndex(), 1);
        QCOMPARE(source.nextText(), QStringLiteral("chorus"));
        QCOMPARE(source.currentLineIndex(), -1);
        QCOMPARE(next.size(), 1);
        QCOMPARE(current.size(), 0);

        writeDocumentSnapshot(path, 3, lines, 5000, 4000);
        QTRY_COMPARE(source.nextLineIndex(), 0);
        QCOMPARE(source.nextText(), QStringLiteral("verse"));
        QCOMPARE(next.size(), 2);
        QCOMPARE(current.size(), 0);
    }

    void aFingerprintChangeAloneFiresNeitherLineSignal()
    {
        // The same document at the same position under another fingerprint,
        // as when a player fills in its metadata in several steps. Neither
        // line signal fires, which is why the animation does not compare
        // fingerprints (DESIGN.md decision 28): a fingerprint it recorded
        // would never be refreshed.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        const LyricLines lines{plainLine(1000, 2000, QStringLiteral("one")),
                               plainLine(2000, 3000, QStringLiteral("two"))};
        writeDocumentSnapshot(path, 1, lines);
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);
        QCOMPARE(source.currentLineIndex(), 0);
        QCOMPARE(source.currentText(), QStringLiteral("one"));
        QCOMPARE(source.nextLineIndex(), 1);
        QCOMPARE(source.nextText(), QStringLiteral("two"));
        QSignalSpy track(&source, &LyricSource::trackChanged);
        QSignalSpy current(&source, &LyricSource::currentLineChanged);
        QSignalSpy next(&source, &LyricSource::nextLineChanged);

        writeDocumentSnapshot(path, 2, lines, 1000, 0, QStringLiteral("ok"),
                              QStringLiteral("mediaSrc:other"));
        QTRY_COMPARE(source.fingerprint(), QStringLiteral("mediaSrc:other"));
        QCOMPARE(track.size(), 1);
        QCOMPARE(current.size(), 0);
        QCOMPARE(next.size(), 0);
        QCOMPARE(source.currentLineIndex(), 0);
        QCOMPARE(source.currentText(), QStringLiteral("one"));
        QCOMPARE(source.nextLineIndex(), 1);
        QCOMPARE(source.nextText(), QStringLiteral("two"));
    }

    void aNewDocumentNotifiesTheNextLineOnlyForOtherText()
    {
        // The next line shows its text and nothing else, so a new document
        // that keeps both its index and its text leaves it alone -- unlike
        // the current line, whose translation and word timings are shown as
        // well.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeDocumentSnapshot(path, 1, {plainLine(1000, 2000, QStringLiteral("one")),
                                        plainLine(2000, 3000, QStringLiteral("two"))});
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);
        QCOMPARE(source.nextText(), QStringLiteral("two"));
        QSignalSpy current(&source, &LyricSource::currentLineChanged);
        QSignalSpy next(&source, &LyricSource::nextLineChanged);

        writeDocumentSnapshot(path, 2, {plainLine(1000, 2000, QStringLiteral("one")),
                                        plainLine(2000, 3000, QStringLiteral("deux"))});
        QTRY_COMPARE(source.nextText(), QStringLiteral("deux"));
        QCOMPARE(source.nextLineIndex(), 1);
        QCOMPARE(next.size(), 1);

        LyricLine translated = plainLine(2000, 3000, QStringLiteral("deux"));
        translated.translation = QStringLiteral("two");
        writeDocumentSnapshot(path, 3, {plainLine(1000, 2000, QStringLiteral("un")), translated});
        QTRY_COMPARE(source.currentText(), QStringLiteral("un"));
        QCOMPARE(current.size(), 2);
        QCOMPARE(next.size(), 1);
    }

    void aSongChangeOrASearchLeavesNoNextLineBehind()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        const LyricLines lines{plainLine(1000, 2000, QStringLiteral("one")),
                               plainLine(2000, 3000, QStringLiteral("two")),
                               plainLine(3000, 4000, QStringLiteral("three"))};
        writeDocumentSnapshot(path, 1, lines);
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);
        QCOMPARE(source.nextLineIndex(), 1);
        QSignalSpy next(&source, &LyricSource::nextLineChanged);

        // Another song, one line long: index 1 no longer exists.
        writeDocumentSnapshot(path, 2, {plainLine(1000, 2000, QStringLiteral("solo"))}, 1000, 0,
                              QStringLiteral("ok"), QStringLiteral("mediaSrc:other"));
        QTRY_COMPARE(source.currentText(), QStringLiteral("solo"));
        QCOMPARE(source.nextLineIndex(), -1);
        QVERIFY(source.nextText().isEmpty());
        QCOMPARE(next.size(), 1);

        writeDocumentSnapshot(path, 3, lines, 1000, 0, QStringLiteral("ok"),
                              QStringLiteral("mediaSrc:other"));
        QTRY_COMPARE(source.nextLineIndex(), 1);
        QCOMPARE(next.size(), 2);

        // A search publishes no lines at all (the daemon's `searching`
        // snapshot carries an empty document).
        writeDocumentSnapshot(path, 4, {}, 1000, 0, QStringLiteral("searching"),
                              QStringLiteral("mediaSrc:third"));
        QTRY_COMPARE(source.lyricState(), QStringLiteral("searching"));
        QCOMPARE(source.currentLineIndex(), -1);
        QCOMPARE(source.nextLineIndex(), -1);
        QVERIFY(source.nextText().isEmpty());
        QCOMPARE(next.size(), 3);
    }

    void aDaemonComingBackWithAnotherSongLeavesNoNextLineBehind()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeDocumentSnapshot(path, 1, {plainLine(1000, 2000, QStringLiteral("one")),
                                        plainLine(2000, 3000, QStringLiteral("two"))});
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);
        QCOMPARE(source.nextLineIndex(), 1);
        QSignalSpy next(&source, &LyricSource::nextLineChanged);

        writeSnapshot(path, 2, 1000000000, QStringLiteral("gone"), 999999999);
        QTRY_VERIFY(source.stale());
        // The same index, other text.
        writeDocumentSnapshot(path, 3, {plainLine(1000, 2000, QStringLiteral("solo")),
                                        plainLine(2000, 3000, QStringLiteral("coda"))},
                              1000, 0, QStringLiteral("ok"), QStringLiteral("mediaSrc:other"));
        QTRY_VERIFY(!source.stale());
        QCOMPARE(source.currentText(), QStringLiteral("solo"));
        QCOMPARE(source.nextLineIndex(), 1);
        QCOMPARE(source.nextText(), QStringLiteral("coda"));
        QCOMPARE(next.size(), 1);
    }

    void theLeadMovesTheNextLineInAtItsEntry()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeDocumentSnapshot(path, 1, leadDocument(), 1049);
        LyricSource source([] { return 1000000000LL; });
        source.setLeadInMs(2000);
        source.setSnapshotPath(path);
        QCOMPARE(source.currentLineIndex(), -1);
        QCOMPARE(source.nextLineIndex(), 1);
        QCOMPARE(source.nextText(), QStringLiteral("two"));
        QStringList log;
        recordLineSignals(source, log);

        // In sequence: the new current line is the next line recorded just
        // before, and its signal comes first (DESIGN.md decision 28).
        writeDocumentSnapshot(path, 2, leadDocument(), 1050);
        QTRY_COMPARE(source.currentLineIndex(), 1);
        QCOMPARE(log, QStringList({
            QStringLiteral("currentLineChanged -> current 1 [two], next 2 [three]"),
            QStringLiteral("nextLineChanged -> current 1 [two], next 2 [three]")}));

        log.clear();
        writeDocumentSnapshot(path, 3, leadDocument(), 4000);
        QTRY_COMPARE(source.currentLineIndex(), 2);
        QCOMPARE(log, QStringList({
            QStringLiteral("currentLineChanged -> current 2 [three], next -1 []"),
            QStringLiteral("nextLineChanged -> current 2 [three], next -1 []")}));
    }

    void wakesUpAtTheEntry()
    {
        // The start of "two" is 2050 ms away, past the timeout below: only a
        // timer armed on its entry, 50 ms away, gets there in time.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        qint64 now = 1000000000;
        writeDocumentSnapshot(path, 1, leadDocument());
        LyricSource source([&now] { return now; });
        source.setLeadInMs(2000);
        source.setSnapshotPath(path);
        QCOMPARE(source.currentLineIndex(), -1);
        now += 100000000;
        QTRY_COMPARE_WITH_TIMEOUT(source.currentText(), QStringLiteral("two"), 1000);
        QCOMPARE(source.nextText(), QStringLiteral("three"));
    }

    void aNewLeadTakesEffectAtOnce()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        qint64 now = 1000000000;
        writeDocumentSnapshot(path, 1, leadDocument());
        LyricSource source([&now] { return now; });
        source.setSnapshotPath(path);
        QCOMPARE(source.leadInMs(), 0);
        QCOMPARE(source.currentLineIndex(), -1);
        QCOMPARE(source.nextLineIndex(), 1);
        QStringList log;
        recordLineSignals(source, log);
        QSignalSpy lead(&source, &LyricSource::leadInMsChanged);

        // 2100 ms puts the entry of "two" at 950 ms, already passed: the
        // lines change within the call, in sequence.
        source.setLeadInMs(2100);
        QCOMPARE(lead.size(), 1);
        QCOMPARE(log, QStringList({
            QStringLiteral("currentLineChanged -> current 1 [two], next 2 [three]"),
            QStringLiteral("nextLineChanged -> current 1 [two], next 2 [three]")}));

        log.clear();
        source.setLeadInMs(2100);
        QCOMPARE(lead.size(), 1);
        QVERIFY(log.isEmpty());

        // A negative lead is none: back to waiting for the start.
        source.setLeadInMs(-300);
        QCOMPARE(source.leadInMs(), 0);
        QCOMPARE(lead.size(), 2);
        QCOMPARE(log, QStringList({
            QStringLiteral("currentLineChanged -> current -1 [], next 1 [two]"),
            QStringLiteral("nextLineChanged -> current -1 [], next 1 [two]")}));

        // An entry still ahead changes no line yet, but the timer is armed
        // on it: 50 ms rather than the 2050 ms to the start.
        log.clear();
        source.setLeadInMs(2000);
        QVERIFY(log.isEmpty());
        now += 100000000;
        QTRY_COMPARE_WITH_TIMEOUT(source.currentText(), QStringLiteral("two"), 1000);
    }

    void noLeadSwitchesAtTheStart()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeDocumentSnapshot(path, 1, leadDocument(), 1050);
        LyricSource source([] { return 1000000000LL; });
        source.setLeadInMs(0);
        source.setSnapshotPath(path);
        QCOMPARE(source.currentLineIndex(), -1);
        QCOMPARE(source.nextLineIndex(), 1);
        writeDocumentSnapshot(path, 2, leadDocument(), 3049);
        QTRY_COMPARE(source.nextLineIndex(), 1);
        QCOMPARE(source.currentLineIndex(), -1);
        writeDocumentSnapshot(path, 3, leadDocument(), 3050);
        QTRY_COMPARE(source.currentLineIndex(), 1);
        QCOMPARE(source.nextLineIndex(), 2);
        writeDocumentSnapshot(path, 4, leadDocument(), 4000);
        QTRY_COMPARE(source.currentLineIndex(), -1);
        QCOMPARE(source.nextLineIndex(), 2);
    }

    void aLeadSetWhileTheServiceIsDownWaitsForItsReturn()
    {
        // The lines stand still while the daemon is gone, as the stopped frame
        // timer leaves them; the snapshot that brings it back applies the lead.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeDocumentSnapshot(path, 1, leadDocument());
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);
        QCOMPARE(source.currentLineIndex(), -1);
        writeSnapshot(path, 2, 1000000000, QStringLiteral("gone"), 999999999);
        QTRY_VERIFY(source.stale());
        QSignalSpy current(&source, &LyricSource::currentLineChanged);
        source.setLeadInMs(2100);
        QCOMPARE(source.leadInMs(), 2100);
        QCOMPARE(source.currentLineIndex(), -1);
        QCOMPARE(current.size(), 0);

        writeDocumentSnapshot(path, 3, leadDocument());
        QTRY_VERIFY(!source.stale());
        QCOMPARE(source.currentLineIndex(), 1);
        QCOMPARE(source.currentText(), QStringLiteral("two"));
    }

    void wordsOfTheCurrentLineReachQml()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        // Named assignment here too. Padding the braces with {} would silence
        // the warning while leaving the positional trap at the site most
        // likely to grow a line carrying both a translation and a
        // romanization -- two adjacent, identically typed optionals. GCC
        // warns on designated initialisers as well (measured), so this is the
        // only form that both builds clean and stays safe to extend.
        LyricWord first;
        first.startMs = 1000;
        first.endMs = 1400;
        first.text = QStringLiteral("li");
        LyricWord second;
        second.startMs = 1400;
        second.endMs = 2000;
        second.text = QStringLiteral("ne");
        writeWordSnapshot(path, 1, {first, second});
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);

        const QVariantList words = source.currentWords();
        QCOMPARE(words.size(), 2);
        QCOMPARE(words.at(0).toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("li"));
        QCOMPARE(words.at(0).toMap().value(QStringLiteral("startMs")).toLongLong(), 1000);
        QCOMPARE(words.at(0).toMap().value(QStringLiteral("endMs")).toLongLong(), 1400);
        QCOMPARE(words.at(1).toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("ne"));
    }

    void aLineWithoutWordDataExposesNoWords()
    {
        // The common case, not an error: most sources carry no word timings,
        // and the widget falls back to showing the line whole.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(path, 1, 1000000000, QStringLiteral("plain"));
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);

        QCOMPARE(source.currentText(), QStringLiteral("plain"));
        QVERIFY(source.currentWords().isEmpty());
        // Same for a line whose words array is present but empty.
        writeWordSnapshot(path, 2, {});
        QTRY_COMPARE(source.currentText(), QStringLiteral("worded"));
        QVERIFY(source.currentWords().isEmpty());
    }

    void documentLevelGateSuppressesSynthesisWhenAnyLineHasRealWords()
    {
        // Synthesis is a document-level decision (DESIGN.md: 204/204 documents
        // observed with any real word timings had them on *every* line, 0
        // mixed documents), so a document that has real words on one line
        // must not synthesize on another line of the same document just
        // because that particular line happens to carry none itself.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        LyricWord verseWord;
        verseWord.startMs = 0;
        verseWord.endMs = 999;
        verseWord.text = QStringLiteral("verse");
        LyricLine worded;
        worded.startMs = 0;
        worded.endMs = 999;
        worded.text = QStringLiteral("verse");
        worded.words = QList<LyricWord>{verseWord};
        LyricLine wordless;
        wordless.startMs = 1000;
        wordless.endMs = 2000;
        wordless.text = QStringLiteral("chorus");
        writeDocumentSnapshot(path, 1, {worded, wordless});

        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);

        // positionUs (1000000) with a zero-elapsed clock lands positionMs at
        // 1000, inside `wordless`, not `worded`.
        QCOMPARE(source.currentText(), QStringLiteral("chorus"));
        QVERIFY(source.currentWords().isEmpty());
        QVERIFY(source.currentSyntheticWords().isEmpty());
    }

    void noRealWordsAnywhereSynthesizesWordsForTheCurrentLine()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        LyricLine first;
        first.startMs = 0;
        first.endMs = 999;
        first.text = QStringLiteral("intro");
        LyricLine second;
        second.startMs = 1000;
        second.endMs = 2000;
        second.text = QStringLiteral("你好");
        writeDocumentSnapshot(path, 1, {first, second});

        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);

        QCOMPARE(source.currentText(), QStringLiteral("你好"));
        QVERIFY(source.currentWords().isEmpty());
        const QVariantList synthetic = source.currentSyntheticWords();
        QCOMPARE(synthetic.size(), 2);
        QCOMPARE(synthetic.at(0).toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("你"));
        QCOMPARE(synthetic.at(0).toMap().value(QStringLiteral("startMs")).toLongLong(), 1000);
        QCOMPARE(synthetic.at(0).toMap().value(QStringLiteral("endMs")).toLongLong(), 1500);
        QCOMPARE(synthetic.at(1).toMap().value(QStringLiteral("text")).toString(),
                 QStringLiteral("好"));
        QCOMPARE(synthetic.at(1).toMap().value(QStringLiteral("endMs")).toLongLong(), 2000);
    }

    void currentSyntheticWordsNotifiesOnCurrentLineChanged()
    {
        // currentSyntheticWords shares currentWords' NOTIFY signal (both are
        // driven by the same current-line/document state), so a track change
        // that flips the document-level gate has to both change the value
        // and fire that signal -- not leave a stale QML binding showing
        // synthetic words for a document that just gained real ones.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        LyricLine wordless;
        wordless.startMs = 1000;
        wordless.endMs = 2000;
        wordless.text = QStringLiteral("你好");
        writeDocumentSnapshot(path, 1, {wordless});

        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);
        QCOMPARE(source.currentSyntheticWords().size(), 2);

        QSignalSpy spy(&source, &LyricSource::currentLineChanged);
        LyricWord word;
        word.startMs = 1000;
        word.endMs = 2000;
        word.text = QStringLiteral("你好");
        LyricLine worded;
        worded.startMs = 1000;
        worded.endMs = 2000;
        worded.text = QStringLiteral("你好");
        worded.words = QList<LyricWord>{word};
        writeDocumentSnapshot(path, 2, {worded});

        QTRY_VERIFY(source.currentSyntheticWords().isEmpty());
        QVERIFY(spy.size() >= 1);
        QCOMPARE(source.currentWords().size(), 1);
    }

    void romanizationRoundTripsFromTheSnapshot()
    {
        // The seam between two branches: the field is written by the QQ
        // provider's side and read by the widget's, and until both were on
        // main neither half could exercise the join. Deliberately a real
        // snapshot round trip rather than a directly-constructed line --
        // reading the two diffs and concluding they fit is what this is here
        // to replace.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        QDir().mkpath(QFileInfo(path).absolutePath());
        LyricLine line;
        line.startMs = 1000;
        line.endMs = 2000;
        line.text = QStringLiteral("惑星ループ");
        line.translation = QStringLiteral("行星循环");
        line.romanization = QStringLiteral("wakusei ruupu");
        const QJsonObject root{
            {QStringLiteral("schema"), 1},
            {QStringLiteral("seq"), 1},
            {QStringLiteral("daemon"),
             QJsonObject{{QStringLiteral("pid"), QCoreApplication::applicationPid()}}},
            {QStringLiteral("track"),
             QJsonObject{{QStringLiteral("fingerprint"), QStringLiteral("mediaSrc:test")},
                         {QStringLiteral("title"), QStringLiteral("song")},
                         {QStringLiteral("artists"), QJsonArray{QStringLiteral("artist")}}}},
            {QStringLiteral("playback"),
             QJsonObject{{QStringLiteral("status"), QStringLiteral("Playing")},
                         {QStringLiteral("positionUs"), 1000000},
                         {QStringLiteral("anchorMonotonicNs"), 1000000000LL},
                         {QStringLiteral("rate"), 1.0}}},
            {QStringLiteral("lyric"),
             QJsonObject{{QStringLiteral("state"), QStringLiteral("ok")},
                         {QStringLiteral("offsetMs"), 0},
                         {QStringLiteral("availableProviders"), QJsonArray{}},
                         {QStringLiteral("lines"), QJsonArray{lineToJson(line)}}}}};
        QSaveFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        QVERIFY(file.commit());

        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);

        QCOMPARE(source.currentText(), QStringLiteral("惑星ループ"));
        QCOMPARE(source.currentTranslation(), QStringLiteral("行星循环"));
        QCOMPARE(source.currentRomanization(), QStringLiteral("wakusei ruupu"));
    }

    void noCurrentLineMeansNoWordsAndNoRomanization()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        // The line ends before the anchored position, so nothing is current.
        writeSnapshot(path, 1, 1000000000, QStringLiteral("gone"),
                      QCoreApplication::applicationPid(), 500);
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);

        QVERIFY(source.currentText().isEmpty());
        QVERIFY(source.currentWords().isEmpty());
        // All three per-line getters are guarded by the same index check, so
        // this is about there being no current line -- not about the data. It
        // was written when romanization could not round-trip on this branch at
        // all; it can now (see romanizationRoundTripsFromTheSnapshot), and this
        // still holds for the reason it always did.
        QVERIFY(source.currentRomanization().isEmpty());
    }

    void theWordClockAppliesTheLyricOffset()
    {
        // Word times share the timeline's own base, so the position handed to
        // the renderer has to have the offset taken out of it -- otherwise the
        // highlight drifts by exactly the amount the user nudged by hand.
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(path, 1, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 300);
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(path);

        QCOMPARE(source.offsetMs(), 300);
        QCOMPARE(source.currentPositionMs(), 1000);
        QCOMPARE(source.lyricPositionMs(), 700);
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
        LyricSource source([] { return 1000000000LL; });

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
        LyricSource source([] { return 1000000000LL; });

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
        LyricSource source([] { return 1000000000LL; });
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
                      {QStringLiteral("local"), QStringLiteral("netease")}, true, {}, 250);

        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(snapshotPath);

        QVERIFY(source.globalOffsetEnabled());
        QCOMPARE(source.offsetMs(), 750);
        QCOMPARE(source.trackOffsetMs(), 250);
        QCOMPARE(source.lyricRefProvider(), QStringLiteral("netease"));
        QCOMPARE(source.lyricRefTrackId(), QStringLiteral("1"));
        // The effective offset is what the timeline uses.
        QCOMPARE(source.lyricPositionMs(), 1000 - 750);
    }

    void aChangeOfTheSongsOwnOffsetAloneNotifies()
    {
        // In global mode a menu adjustment and an equal change of the
        // global offset elsewhere can leave the effective offset where it
        // was; the song's own value still has to reach the menu text.
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 500,
                      QStringLiteral("netease"), QStringLiteral("1"), {},
                      QStringLiteral("netease"), QStringLiteral("netease"), false,
                      {QStringLiteral("netease")}, true, {}, 0);
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(snapshotPath);
        QSignalSpy spy(&source, &LyricSource::offsetChanged);

        writeSnapshot(snapshotPath, 2, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 500,
                      QStringLiteral("netease"), QStringLiteral("1"), {},
                      QStringLiteral("netease"), QStringLiteral("netease"), false,
                      {QStringLiteral("netease")}, true, {}, 500);

        QTRY_COMPARE(source.trackOffsetMs(), 500);
        QCOMPARE(source.offsetMs(), 500);
        QCOMPARE(spy.size(), 1);
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

    void switchingStateDisablesControlsUntilSnapshotClearsIt()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        writeSnapshot(snapshotPath, 1, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 0,
                      QStringLiteral("netease"), QStringLiteral("1"), {},
                      QStringLiteral("amll"), QStringLiteral("netease"), false,
                      {QStringLiteral("netease"), QStringLiteral("amll")}, false,
                      QStringLiteral("amll"));

        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(snapshotPath);
        QCOMPARE(source.switchingProvider(), QStringLiteral("amll"));
        QVERIFY(source.controlInProgress());
        QVERIFY(!source.canControlProvider());
        QVERIFY(!source.canAdjustOffset());

        writeSnapshot(snapshotPath, 2, 1000000000, QStringLiteral("second"));
        QTRY_VERIFY(!source.controlInProgress());
        QVERIFY(source.canControlProvider());
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

    // DESIGN.md decision 79: the menu changes the song's own offset in
    // global mode too, so the gate no longer relaxes there.
    void canAdjustOffsetRequiresASongAndARefInGlobalModeToo()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        QDir().mkpath(QFileInfo(snapshotPath).absolutePath());
        const QJsonObject root{
            {QStringLiteral("schema"), 1},
            {QStringLiteral("seq"), 1},
            {QStringLiteral("daemon"),
             QJsonObject{{QStringLiteral("pid"), QCoreApplication::applicationPid()}}},
            {QStringLiteral("track"), QJsonValue::Null},
            {QStringLiteral("playback"),
             QJsonObject{{QStringLiteral("status"), QStringLiteral("Stopped")},
                         {QStringLiteral("positionUs"), 0},
                         {QStringLiteral("anchorMonotonicNs"), 0},
                         {QStringLiteral("rate"), 1.0}}},
            {QStringLiteral("lyric"),
             QJsonObject{{QStringLiteral("state"), QStringLiteral("filtered")},
                         {QStringLiteral("offsetMs"), 500},
                         {QStringLiteral("trackOffsetMs"), 0},
                         {QStringLiteral("globalOffsetEnabled"), true},
                         {QStringLiteral("availableProviders"), QJsonArray{}},
                         {QStringLiteral("lines"), QJsonArray{}}}}};
        QSaveFile file(snapshotPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        QVERIFY(file.commit());

        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(snapshotPath);
        QVERIFY(source.serviceAvailable());
        QVERIFY(source.globalOffsetEnabled());
        QVERIFY(!source.canAdjustOffset());
        QVERIFY(!source.adjustOffset(500));

        // A song without a lyric ref.
        QSignalSpy spy(&source, &LyricSource::canAdjustOffsetChanged);
        writeSnapshot(snapshotPath, 2, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 500, QString(), QString(), {},
                      QStringLiteral("netease"), QString(), false, {QStringLiteral("netease")},
                      true);
        QTRY_COMPARE(source.fingerprint(), QStringLiteral("mediaSrc:test"));
        QVERIFY(!source.canAdjustOffset());

        writeSnapshot(snapshotPath, 3, 1000000000, QStringLiteral("first"),
                      QCoreApplication::applicationPid(), 2000, 500, QStringLiteral("netease"),
                      QStringLiteral("1"), {}, QStringLiteral("netease"),
                      QStringLiteral("netease"), false, {QStringLiteral("netease")}, true);
        QTRY_VERIFY(source.canAdjustOffset());
        QVERIFY(spy.size() >= 1);
    }

    void setOffsetForTrackReportsTheDaemonsAnswer()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeOffsetControl control;
        QVERIFY(bus.registerService(QStringLiteral("io.github.swim233.PlasmaLyrics")));
        QVERIFY(bus.registerObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"), &control,
                                   QDBusConnection::ExportAllSlots));
        // No snapshot at all: a pinned song can be applied after its player
        // has gone away.
        QTemporaryDir directory;
        LyricSource source([] { return 1000000000LL; });
        source.setSnapshotPath(directory.filePath(QStringLiteral("runtime/state.json")));
        QVERIFY(!source.serviceAvailable());
        QSignalSpy finished(&source, &LyricSource::offsetForTrackFinished);
        QSignalSpy failed(&source, &LyricSource::controlFailed);

        source.setOffsetForTrack(QStringLiteral("netease"), QStringLiteral("7"), -1500);
        QVERIFY(!source.controlInProgress());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(control.calls.size(), 1);
        QCOMPARE(control.calls.first().provider, QStringLiteral("netease"));
        QCOMPARE(control.calls.first().trackId, QStringLiteral("7"));
        QCOMPARE(control.calls.first().offsetMs, -1500);
        QCOMPARE(finished.first(), QVariantList({QStringLiteral("netease"), QStringLiteral("7"),
                                                 -1500, QString(), QString()}));

        control.result = QStringLiteral("provider-unsupported");
        source.setOffsetForTrack(QStringLiteral("other"), QStringLiteral("8"), 100);
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished.last(), QVariantList({QStringLiteral("other"), QStringLiteral("8"), 100,
            QStringLiteral("provider-unsupported"),
            QStringLiteral("The lyrics service does not support this song's lyrics source.")}));
        control.result = QStringLiteral("track-id-empty");
        source.setOffsetForTrack(QStringLiteral("netease"), QString(), 100);
        QTRY_COMPARE(finished.size(), 3);
        QCOMPARE(finished.last().at(3).toString(), QStringLiteral("track-id-empty"));
        QCOMPARE(finished.last().at(4).toString(), QStringLiteral("The song has no lyrics to adjust."));
        QCOMPARE(failed.size(), 0);
        QVERIFY(source.controlError().isEmpty());

        bus.unregisterObject(QStringLiteral("/io/github/swim233/PlasmaLyrics"));
        bus.unregisterService(QStringLiteral("io.github.swim233.PlasmaLyrics"));

        // No daemon on the bus: still one answer, carrying the failure under
        // the D-Bus error's name.
        source.setOffsetForTrack(QStringLiteral("netease"), QStringLiteral("7"), 100);
        QTRY_COMPARE(finished.size(), 4);
        QVERIFY(finished.last().at(3).toString().startsWith(QStringLiteral("org.freedesktop.DBus.Error.")));
        QVERIFY(!finished.last().at(4).toString().isEmpty());
        QCOMPARE(failed.size(), 0);
    }
};

QTEST_GUILESS_MAIN(LyricSourceTest)
#include "tst_lyricsource.moc"
