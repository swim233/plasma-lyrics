#include "core/log/configlog.h"

#include <QTest>

using namespace PlasmaLyrics;

class ConfigLogTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void changedLineWithoutInstance()
    {
        const ConfigChange change{.store = QStringLiteral("ini"),
                                  .key = QStringLiteral("providers/enabled"),
                                  .oldValue = QStringLiteral("local,netease"),
                                  .newValue = QStringLiteral("local,netease,qq")};
        QCOMPARE(configChangedLine(change),
                 QStringLiteral("config changed store=ini key=providers/enabled "
                                "old=\"local,netease\" new=\"local,netease,qq\""));
    }

    void changedLineWithInstance()
    {
        const ConfigChange change{.store = QStringLiteral("applet"),
                                  .applet = QStringLiteral("12"),
                                  .form = QStringLiteral("desktop"),
                                  .key = QStringLiteral("desktopFontSize"),
                                  .oldValue = QStringLiteral("34"),
                                  .newValue = QStringLiteral("36")};
        QCOMPARE(configChangedLine(change),
                 QStringLiteral("config changed store=applet applet=12 form=desktop "
                                "key=desktopFontSize old=\"34\" new=\"36\""));
    }

    void valuesAreAlwaysQuotedAndEscaped()
    {
        const ConfigChange change{.store = QStringLiteral("db"),
                                  .key = QStringLiteral("globalOffsetEnabled"),
                                  .oldValue = QStringLiteral("false"),
                                  .newValue = QStringLiteral("say \"hi\"\nthere")};
        QCOMPARE(configChangedLine(change),
                 QStringLiteral("config changed store=db key=globalOffsetEnabled "
                                "old=\"false\" new=\"say \\\"hi\\\"\\nthere\""));
    }

    void emptyValuesStayVisible()
    {
        const ConfigChange change{.store = QStringLiteral("ini"),
                                  .key = QStringLiteral("players/blacklist"),
                                  .oldValue = QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*"),
                                  .newValue = QString()};
        QCOMPARE(configChangedLine(change),
                 QStringLiteral("config changed store=ini key=players/blacklist "
                                "old=\"org.mpris.MediaPlayer2.kdeconnect.*\" new=\"\""));
    }

    void saveFailedLine()
    {
        QCOMPARE(configSaveFailedLine(QStringLiteral("ini"), QStringLiteral("proxy-url-invalid")),
                 QStringLiteral("config save failed store=ini reason=\"proxy-url-invalid\""));
    }

    void restartLines()
    {
        QCOMPARE(restartRequestedLine(), QStringLiteral("config restart requested"));
        QCOMPARE(restartFinishedLine(true, QString()),
                 QStringLiteral("config restart finished result=ok"));
        QCOMPARE(restartFinishedLine(true, QStringLiteral("ignored when successful")),
                 QStringLiteral("config restart finished result=ok"));
        QCOMPARE(restartFinishedLine(false, QStringLiteral("restart command exited with code 1")),
                 QStringLiteral("config restart finished result=failed "
                                "error=\"restart command exited with code 1\""));
    }
};

QTEST_GUILESS_MAIN(ConfigLogTest)
#include "tst_configlog.moc"
