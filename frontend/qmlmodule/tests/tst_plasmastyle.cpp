#include "frontend/qmlmodule/plasmastyle.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

// DESIGN.md decision 76: the config pages' reading of whether the Plasma
// style is dark. The colours below are the Window backgrounds of the styles
// plasmoidviewer was checked against: breeze-dark's own colors file, and
// BreezeLight's colour scheme, which "default" follows.
class PlasmaStyleTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString write(const QString &relativePath, const QByteArray &contents)
    {
        const QString path = m_dir.filePath(relativePath);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return {};
        }
        file.write(contents);
        return path;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    void styleName()
    {
        QCOMPARE(PlasmaStyle::styleNameIn(write(QStringLiteral("a/plasmarc"),
                                                "[Theme]\nname=breeze-dark\n\n[Theme-plasmathemeexplorer]\nname=air\n")),
                 QStringLiteral("breeze-dark"));
        // Another group's `name` is not the style's.
        QCOMPARE(PlasmaStyle::styleNameIn(write(QStringLiteral("b/plasmarc"), "[Theme-plasmathemeexplorer]\nname=air\n")),
                 QStringLiteral("default"));
        QCOMPARE(PlasmaStyle::styleNameIn(QString()), QStringLiteral("default"));
        QCOMPARE(PlasmaStyle::styleNameIn(m_dir.filePath(QStringLiteral("missing/plasmarc"))), QStringLiteral("default"));
    }

    void ownColoursWinOverTheColourScheme()
    {
        const QString dark = write(QStringLiteral("breeze-dark/colors"),
                                   "[Colors:View]\nBackgroundNormal=255,255,255\n\n"
                                   "[Colors:Window]\nBackgroundAlternate=41,44,48\nBackgroundNormal=32,35,38\n");
        QCOMPARE(PlasmaStyle::isDarkStyle(dark, QColor(QStringLiteral("#eff0f1"))), true);

        const QString light = write(QStringLiteral("breeze-light/colors"), "[Colors:Window]\nBackgroundNormal=239,240,241\n");
        QCOMPARE(PlasmaStyle::isDarkStyle(light, QColor(QStringLiteral("#202326"))), false);
    }

    void aStyleWithoutColoursFollowsTheColourScheme()
    {
        QCOMPARE(PlasmaStyle::isDarkStyle(QString(), QColor(QStringLiteral("#202326"))), true);
        QCOMPARE(PlasmaStyle::isDarkStyle(QString(), QColor(QStringLiteral("#eff0f1"))), false);
        const QString partial = write(QStringLiteral("partial/colors"), "[Colors:Window]\nBackgroundNormal=oops\n");
        QCOMPARE(PlasmaStyle::isDarkStyle(partial, QColor(QStringLiteral("#202326"))), true);
    }

    void thresholdIsQGray192()
    {
        QCOMPARE(PlasmaStyle::isDarkBackground(QColor(192, 192, 192)), false);
        QCOMPARE(PlasmaStyle::isDarkBackground(QColor(191, 191, 191)), true);
    }

    // The whole lookup, through the XDG directories the way plasmashell
    // resolves them.
    void readsTheStylePlasmarcNames()
    {
        const QString config = m_dir.filePath(QStringLiteral("xdg-config"));
        const QString data = m_dir.filePath(QStringLiteral("xdg-data"));
        write(QStringLiteral("xdg-config/plasmarc"), "[Theme]\nname=test-dark-style\n");
        write(QStringLiteral("xdg-data/plasma/desktoptheme/test-dark-style/colors"),
              "[Colors:Window]\nBackgroundNormal=32,35,38\n");
        qputenv("XDG_CONFIG_HOME", config.toUtf8());
        qputenv("XDG_DATA_HOME", data.toUtf8());
        PlasmaStyle style;
        QCOMPARE(style.isDark(), true);

        write(QStringLiteral("xdg-config/plasmarc"), "[Theme]\nname=test-light-style\n");
        write(QStringLiteral("xdg-data/plasma/desktoptheme/test-light-style/colors"),
              "[Colors:Window]\nBackgroundNormal=239,240,241\n");
        QCOMPARE(style.isDark(), false);
    }
};

QTEST_MAIN(PlasmaStyleTest)

#include "tst_plasmastyle.moc"
