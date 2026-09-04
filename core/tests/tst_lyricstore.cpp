#include "core/store/lyricstore.h"

#include <QTemporaryDir>
#include <QTest>

using namespace PlasmaLyrics;

class LyricStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void mappingsAndMissTtl()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const TrackRef ref{QStringLiteral("netease"), QStringLiteral("1299289240"), 0.97};
        QVERIFY(store.mapFingerprint(QStringLiteral("mediaSrc:abc"), ref, 100));
        const auto restored = store.refForFingerprint(QStringLiteral("mediaSrc:abc"));
        QVERIFY(restored.has_value());
        QCOMPARE(restored->trackId, ref.trackId);
        QVERIFY(store.recordMiss(QStringLiteral("meta:video"), QStringLiteral("not-music"), 100));
        QVERIFY(store.hasFreshMiss(QStringLiteral("meta:video"), 100 + 60));
        QVERIFY(!store.hasFreshMiss(QStringLiteral("meta:video"), 100 + 8 * 24 * 60 * 60));
    }

    void lyricAndOffsetRoundTrip()
    {
        QTemporaryDir directory;
        LyricStore store(directory.filePath(QStringLiteral("lyrics.db")));
        QVERIFY(store.open());
        const TrackRef ref{QStringLiteral("netease"), QStringLiteral("1"), 1};
        LyricDocument input{{{1000, 2000, QStringLiteral("line"), QStringLiteral("translation"), std::nullopt}}, 0, false};
        QVERIFY(store.putLyric(ref, input, 100));
        QVERIFY(store.setOffset(ref, 250));
        QCOMPARE(store.adjustOffset(ref, -50), std::optional<int>(200));
        const auto restored = store.lyric(ref);
        QVERIFY(restored.has_value());
        QCOMPARE(restored->lines, input.lines);
        QCOMPARE(restored->offsetMs, 200);
    }
};

QTEST_GUILESS_MAIN(LyricStoreTest)
#include "tst_lyricstore.moc"
