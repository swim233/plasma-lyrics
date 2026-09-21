#include "frontend/qmlmodule/settingslog.h"

#include <QPoint>
#include <QTest>

using namespace PlasmaLyrics;

class SettingsLogTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void rendersScalarsAndLists()
    {
        QCOMPARE(renderConfigValue(true), QStringLiteral("true"));
        QCOMPARE(renderConfigValue(false), QStringLiteral("false"));
        QCOMPARE(renderConfigValue(34), QStringLiteral("34"));
        QCOMPARE(renderConfigValue(-300), QStringLiteral("-300"));
        QCOMPARE(renderConfigValue(QStringLiteral("#99000000")), QStringLiteral("#99000000"));
        QCOMPARE(renderConfigValue(QString()), QString());
        QCOMPARE(renderConfigValue(QStringList{QStringLiteral("local"), QStringLiteral("netease")}),
                 QStringLiteral("local,netease"));
        QCOMPARE(renderConfigValue(QStringList{}), QString());
    }

    // QSettings hands back numbers and booleans it read from an INI file as
    // strings; those must render exactly like the typed member they are
    // compared against, or every save would report a spurious change.
    void iniStringsRenderLikeTheirTypedTwins()
    {
        QCOMPARE(renderConfigValue(QStringLiteral("4000")), renderConfigValue(4000));
        QCOMPARE(renderConfigValue(QStringLiteral("true")), renderConfigValue(true));
        QCOMPARE(renderConfigValue(QStringLiteral("false")), renderConfigValue(false));
    }

    void unstringifiableTypesStillRenderSomething()
    {
        const QString rendered = renderConfigValue(QVariant(QPoint(1, 2)));
        QVERIFY2(!rendered.isEmpty(), "a type QVariant::toString() cannot handle must not render empty");
        QVERIFY(rendered.contains(QStringLiteral("QPoint")));
        QCOMPARE(renderConfigValue(QVariant()), QString());
    }

    void proxyUrlNeverShowsCredentials()
    {
        QCOMPARE(renderProxyUrlForLog(QString()), QString());
        QCOMPARE(renderProxyUrlForLog(QStringLiteral("   ")), QString());
        QCOMPARE(renderProxyUrlForLog(QStringLiteral("socks5://alice:secret@proxy.example:1080")),
                 QStringLiteral("socks5://proxy.example:1080"));
        QCOMPARE(renderProxyUrlForLog(QStringLiteral(" http://proxy.example:3128/ ")),
                 QStringLiteral("http://proxy.example:3128"));
        // Unparsable but with a recognizable authority: redacted summary.
        const QString partial = renderProxyUrlForLog(QStringLiteral("ftp://bob:pw@host.example:21"));
        QVERIFY(!partial.contains(QStringLiteral("bob")));
        QVERIFY(!partial.contains(QStringLiteral("pw")));
        // Nothing recognizable at all: only the length is shown.
        QCOMPARE(renderProxyUrlForLog(QStringLiteral("not a url at all")),
                 QStringLiteral("redacted(length=16)"));
    }
};

QTEST_GUILESS_MAIN(SettingsLogTest)
#include "tst_settingslog.moc"
