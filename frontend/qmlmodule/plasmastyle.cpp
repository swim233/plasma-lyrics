#include "plasmastyle.h"

#include <QFile>
#include <QGuiApplication>
#include <QPalette>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// The value of `key` in `[group]` of a KConfig-style file, or a null string.
// KConfig's escapes and localized keys do not occur in the two entries read
// here, so a line scan is enough.
QString readEntry(const QString &path, const QString &group, const QString &key)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QString header = QLatin1Char('[') + group + QLatin1Char(']');
    bool inGroup = false;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inGroup = line == header;
            continue;
        }
        if (!inGroup) {
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals > 0 && line.left(equals).trimmed() == key) {
            return line.mid(equals + 1).trimmed();
        }
    }
    return {};
}

} // namespace

PlasmaStyle::PlasmaStyle(QObject *parent)
    : QObject(parent)
{
}

bool PlasmaStyle::isDark() const
{
    const QString plasmarc = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("plasmarc"));
    const QString colors = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("plasma/desktoptheme/%1/colors").arg(styleNameIn(plasmarc)));
    return isDarkStyle(colors, QGuiApplication::palette().color(QPalette::Window));
}

QString PlasmaStyle::styleNameIn(const QString &plasmarcPath)
{
    const QString name = readEntry(plasmarcPath, QStringLiteral("Theme"), QStringLiteral("name"));
    return name.isEmpty() ? QStringLiteral("default") : name;
}

bool PlasmaStyle::isDarkStyle(const QString &colorsPath, const QColor &colorSchemeWindow)
{
    const QStringList rgb = readEntry(colorsPath, QStringLiteral("Colors:Window"), QStringLiteral("BackgroundNormal"))
                                .split(QLatin1Char(','));
    if (rgb.size() >= 3) {
        bool ok[3] = {false, false, false};
        const QColor background(rgb.at(0).trimmed().toInt(&ok[0]), rgb.at(1).trimmed().toInt(&ok[1]),
                                rgb.at(2).trimmed().toInt(&ok[2]));
        if (ok[0] && ok[1] && ok[2] && background.isValid()) {
            return isDarkBackground(background);
        }
    }
    return isDarkBackground(colorSchemeWindow);
}

bool PlasmaStyle::isDarkBackground(const QColor &background)
{
    return qGray(background.rgb()) < 192;
}
