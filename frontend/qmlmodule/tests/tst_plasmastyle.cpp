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
        const QString rc = write(QStringLiteral("a/plasmarc"),
                                 "[Theme]\nname=breeze-dark\n\n[Theme-plasmathemeexplorer]\nname=air\n");
        QCOMPARE(PlasmaStyle::styleNameIn({rc}), QStringLiteral("breeze-dark"));
        // Another group's `name` is not the style's.
        QCOMPARE(PlasmaStyle::styleNameIn({write(QStringLiteral("b/plasmarc"), "[Theme-plasmathemeexplorer]\nname=air\n")}),
                 QStringLiteral("default"));
        QCOMPARE(PlasmaStyle::styleNameIn({}), QStringLiteral("default"));
        QCOMPARE(PlasmaStyle::styleNameIn({m_dir.filePath(QStringLiteral("missing/plasmarc"))}), QStringLiteral("default"));
    }

    // What System Settings leaves behind after applying a global theme: the
    // style's name in ~/.config/kdedefaults/plasmarc, and a user plasmarc
    // that still exists for other groups but no longer has the entry.
    void theNameMergesAcrossFiles()
    {
        const QString user = write(QStringLiteral("merge/user/plasmarc"), "[Wallpapers]\nusersWallpapers=\n");
        const QString kdedefaults = write(QStringLiteral("merge/kdedefaults/plasmarc"), "[Theme]\nname=breeze-dark\n");
        const QString system = write(QStringLiteral("merge/xdg/plasmarc"), "[Theme]\nname=oxygen\n");
        QCOMPARE(PlasmaStyle::styleNameIn({user, kdedefaults, system}), QStringLiteral("breeze-dark"));

        // A name the user set themselves wins over the defaults.
        const QString userNamed = write(QStringLiteral("merge/user-named/plasmarc"), "[Theme]\nname=air\n");
        QCOMPARE(PlasmaStyle::styleNameIn({userNamed, kdedefaults, system}), QStringLiteral("air"));
    }

    void anImmutableEntryIsNotOverridden()
    {
        const QString user = write(QStringLiteral("kiosk/user/plasmarc"), "[Theme]\nname=air\n");
        const QString keyLocked = write(QStringLiteral("kiosk/key/plasmarc"), "[Theme]\nname[$i]=breeze-dark\n");
        QCOMPARE(PlasmaStyle::styleNameIn({user, keyLocked}), QStringLiteral("breeze-dark"));
        const QString groupLocked = write(QStringLiteral("kiosk/group/plasmarc"), "[Theme][$i]\nname=breeze-light\n");
        QCOMPARE(PlasmaStyle::styleNameIn({user, groupLocked}), QStringLiteral("breeze-light"));
        const QString fileLocked = write(QStringLiteral("kiosk/file/plasmarc"), "[$i]\n[Theme]\nname=oxygen\n");
        QCOMPARE(PlasmaStyle::styleNameIn({user, fileLocked}), QStringLiteral("oxygen"));
        // Unlocked, the same files give way to the user's.
        const QString open = write(QStringLiteral("kiosk/open/plasmarc"), "[Theme]\nname=breeze-dark\n");
        QCOMPARE(PlasmaStyle::styleNameIn({user, open}), QStringLiteral("air"));
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
    // resolves them, kdedefaults first in XDG_CONFIG_DIRS as Plasma sets it.
    void readsTheStylePlasmarcNames()
    {
        const QString config = m_dir.filePath(QStringLiteral("xdg-config"));
        const QString kdedefaults = m_dir.filePath(QStringLiteral("xdg-config/kdedefaults"));
        const QString data = m_dir.filePath(QStringLiteral("xdg-data"));
        write(QStringLiteral("xdg-data/plasma/desktoptheme/test-dark-style/colors"),
              "[Colors:Window]\nBackgroundNormal=32,35,38\n");
        write(QStringLiteral("xdg-data/plasma/desktoptheme/test-light-style/colors"),
              "[Colors:Window]\nBackgroundNormal=239,240,241\n");
        qputenv("XDG_CONFIG_HOME", config.toUtf8());
        qputenv("XDG_CONFIG_DIRS", kdedefaults.toUtf8());
        qputenv("XDG_DATA_HOME", data.toUtf8());
        PlasmaStyle style;

        write(QStringLiteral("xdg-config/plasmarc"), "[Theme]\nname=test-dark-style\n");
        QCOMPARE(style.isDark(), true);
        write(QStringLiteral("xdg-config/plasmarc"), "[Theme]\nname=test-light-style\n");
        QCOMPARE(style.isDark(), false);

        // A global theme applied in System Settings: the name only in
        // kdedefaults, the user's own plasmarc without it.
        write(QStringLiteral("xdg-config/kdedefaults/plasmarc"), "[Theme]\nname=test-dark-style\n");
        write(QStringLiteral("xdg-config/plasmarc"), "[Wallpapers]\nusersWallpapers=\n");
        QCOMPARE(style.isDark(), true);
    }
};

QTEST_MAIN(PlasmaStyleTest)

#include "tst_plasmastyle.moc"
