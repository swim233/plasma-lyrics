#include "providers/local/localprovider.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace PlasmaLyrics;

class LocalProviderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void loadsTrackOverrideWithoutChangingTheCachedSource()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        LocalProvider provider(directory.path());
        QVERIFY(!provider.overrideFor(QStringLiteral("netease"), QStringLiteral("42")));

        QFile file(directory.filePath(QStringLiteral("netease:42.lrc")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("[00:01.000]manual line\n") > 0);
        file.close();

        const auto document = provider.overrideFor(QStringLiteral("netease"), QStringLiteral("42"));
        QVERIFY(document.has_value());
        QCOMPARE(document->lines.size(), 1);
        QCOMPARE(document->lines.first().text, QStringLiteral("manual line"));
    }
};

QTEST_GUILESS_MAIN(LocalProviderTest)
#include "tst_localprovider.moc"
