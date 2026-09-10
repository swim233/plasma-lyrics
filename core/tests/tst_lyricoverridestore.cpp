#include "core/store/lyricoverridestore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace PlasmaLyrics;

class LyricOverrideStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void loadsOverrideWithoutChangingTheSourceRef()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LyricOverrideStore overrides(directory.path());
        const TrackRef ref{QStringLiteral("netease"), QStringLiteral("42"), 1.0};
        QVERIFY(!overrides.lyric(ref));

        QFile file(directory.filePath(QStringLiteral("netease:42.lrc")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("[00:01.000]manual line\n") > 0);
        file.close();

        const auto document = overrides.lyric(ref);
        QVERIFY(document.has_value());
        QCOMPARE(document->lines.size(), 1);
        QCOMPARE(document->lines.first().text, QStringLiteral("manual line"));
        QCOMPARE(ref.provider, QStringLiteral("netease"));
        QCOMPARE(ref.trackId, QStringLiteral("42"));
    }
};

QTEST_GUILESS_MAIN(LyricOverrideStoreTest)
#include "tst_lyricoverridestore.moc"
