#include "plasmastyle.h"

#include <QFile>
#include <QGuiApplication>
#include <QPalette>
#include <QStandardPaths>
#include <QTextStream>

namespace {

struct Entry {
    bool found = false;
    QString value;
    // Marked [$i] on the key, its group or the whole file: KConfig lets no
    // file of higher priority override it.
    bool immutable = false;
};

// Strips KConfig's trailing option markers ("[$i]", "[$e]", ...) off a group
// header's name or a key, reporting whether one of them is $i.
QString withoutOptions(QString text, bool *immutable)
{
    while (text.endsWith(QLatin1Char(']'))) {
        const qsizetype open = text.lastIndexOf(QLatin1String("[$"));
        if (open < 0) {
            break;
        }
        if (text.mid(open + 2, text.size() - open - 3).contains(QLatin1Char('i'))) {
            *immutable = true;
        }
        text.truncate(open);
    }
    return text;
}

// `key` in `[group]` of one KConfig-style file. KConfig's value escapes and
// localized keys do not occur in the entry read here, so a line scan is
// enough.
Entry readEntry(const QString &path, const QString &group, const QString &key)
{
    Entry entry;
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return entry;
    }
    bool fileImmutable = false;
    bool sawGroup = false;
    bool inGroup = false;
    bool groupImmutable = false;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('['))) {
            if (!sawGroup && line == QLatin1String("[$i]")) {
                fileImmutable = true;
                continue;
            }
            sawGroup = true;
            groupImmutable = false;
            const QString name = withoutOptions(line, &groupImmutable);
            inGroup = name == QLatin1Char('[') + group + QLatin1Char(']');
            continue;
        }
        if (!inGroup) {
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            continue;
        }
        bool keyImmutable = false;
        if (withoutOptions(line.left(equals).trimmed(), &keyImmutable) == key) {
            entry.found = true;
            entry.value = line.mid(equals + 1).trimmed();
            entry.immutable = fileImmutable || groupImmutable || keyImmutable;
        }
    }
    return entry;
}

} // namespace

PlasmaStyle::PlasmaStyle(QObject *parent)
    : QObject(parent)
{
}

bool PlasmaStyle::isDark() const
{
    const QStringList plasmarcs = QStandardPaths::locateAll(QStandardPaths::GenericConfigLocation, QStringLiteral("plasmarc"));
    const QString colors = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("plasma/desktoptheme/%1/colors").arg(styleNameIn(plasmarcs)));
    return isDarkStyle(colors, QGuiApplication::palette().color(QPalette::Window));
}

QString PlasmaStyle::styleNameIn(const QStringList &plasmarcPaths)
{
    // Lowest priority first, each file overriding the ones before it unless
    // they marked the entry immutable -- the order KConfig merges them in.
    QString name;
    for (auto it = plasmarcPaths.crbegin(); it != plasmarcPaths.crend(); ++it) {
        const Entry entry = readEntry(*it, QStringLiteral("Theme"), QStringLiteral("name"));
        if (entry.found) {
            name = entry.value;
            if (entry.immutable) {
                break;
            }
        }
    }
    return name.isEmpty() ? QStringLiteral("default") : name;
}

bool PlasmaStyle::isDarkStyle(const QString &colorsPath, const QColor &colorSchemeWindow)
{
    const QStringList rgb = readEntry(colorsPath, QStringLiteral("Colors:Window"), QStringLiteral("BackgroundNormal"))
                                .value.split(QLatin1Char(','));
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
