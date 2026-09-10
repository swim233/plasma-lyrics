#include "daemon/src/config.h"

#include <QSettings>
#include <QStandardPaths>
#include <QTest>

using namespace PlasmaLyrics;

class ConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.clear();
        settings.sync();
    }

    void emptyOrderFallsBackToBuiltInOrder()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("providers/order"), QStringList{});
        settings.sync();
        QCOMPARE(Config().providerOrder(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                              QStringLiteral("amll")}));
    }

    void oldOrderIsUpgradedWithLocalFirst()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("providers/order"),
                          QStringList({QStringLiteral("netease"), QStringLiteral("amll")}));
        settings.sync();
        QCOMPARE(Config().providerOrder(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                              QStringLiteral("amll")}));
    }

    void enabledSetFiltersOnlyTheActiveChainWithoutChangingOrder()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("providers/order"),
                          QStringList({QStringLiteral("netease"), QStringLiteral("amll")}));
        settings.setValue(QStringLiteral("providers/enabled"),
                          QStringList({QStringLiteral("amll")}));
        settings.sync();
        const Config config;
        QCOMPARE(config.providerOrder(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                              QStringLiteral("amll")}));
        QCOMPARE(config.enabledProviderOrder(), QStringList({QStringLiteral("amll")}));
    }

    void emptyEnabledSetFallsBackToBuiltInOrder()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("providers/order"),
                          QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                                       QStringLiteral("amll")}));
        settings.setValue(QStringLiteral("providers/enabled"), QStringList{});
        settings.sync();
        const Config config;
        QCOMPARE(config.providerOrder(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                              QStringLiteral("amll")}));
        QCOMPARE(config.enabledProviderOrder(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease"),
                              QStringLiteral("amll")}));
    }

    void disabledProviderKeepsItsConfiguredPosition()
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
        settings.setValue(QStringLiteral("providers/order"),
                          QStringList({QStringLiteral("amll"), QStringLiteral("local"),
                                       QStringLiteral("netease")}));
        settings.setValue(QStringLiteral("providers/enabled"),
                          QStringList({QStringLiteral("local"), QStringLiteral("netease")}));
        settings.sync();

        const Config config;
        QCOMPARE(config.providerOrder(),
                 QStringList({QStringLiteral("amll"), QStringLiteral("local"),
                              QStringLiteral("netease")}));
        QCOMPARE(config.enabledProviderOrder(),
                 QStringList({QStringLiteral("local"), QStringLiteral("netease")}));
    }
};

QTEST_GUILESS_MAIN(ConfigTest)
#include "tst_config.moc"
