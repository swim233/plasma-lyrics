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
                              bool globalOffsetEnabled = false,
                              const QString &switchingProvider = {})
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
    // word gate, which writeSnapshot()/writeWordSnapshot() above -- both
    // single-line -- cannot exercise.
    static void writeDocumentSnapshot(const QString &path, int seq, const LyricLines &lines)
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
                         {QStringLiteral("lines"), lineArray}}}};
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

    void globalOffsetCanBeAdjustedWithoutACurrentTrack()
    {
        QTemporaryDir directory;
        const QString snapshotPath = directory.filePath(QStringLiteral("runtime/state.json"));
        QDir().mkpath(QFileInfo(snapshotPath).absolutePath());
        const QJsonObject root{
            {QStringLiteral("schema"), 1},
            {QStringLiteral("seq"), 1},
            {QStringLiteral("daemon"),
             QJsonObject{{QStringLiteral("pid"), QCoreApplication::applicationPid()}}},
            {QStringLiteral("track"),
             QJsonObject{{QStringLiteral("fingerprint"), QString()},
                         {QStringLiteral("title"), QString()},
                         {QStringLiteral("artists"), QJsonArray{}}}},
            {QStringLiteral("playback"),
             QJsonObject{{QStringLiteral("status"), QStringLiteral("Stopped")},
                         {QStringLiteral("positionUs"), 0},
                         {QStringLiteral("anchorMonotonicNs"), 1000000000LL},
                         {QStringLiteral("rate"), 1.0}}},
            {QStringLiteral("lyric"),
             QJsonObject{{QStringLiteral("state"), QStringLiteral("not-found")},
                         {QStringLiteral("offsetMs"), 500},
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
        QVERIFY(source.canAdjustOffset());
    }
};

QTEST_GUILESS_MAIN(LyricSourceTest)
#include "tst_lyricsource.moc"
