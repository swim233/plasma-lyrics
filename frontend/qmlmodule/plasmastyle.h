#pragma once

#include <QColor>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

// DESIGN.md decision 76: whether the Plasma style is a dark one, for the
// config pages. The style is what draws the widget's "ksvg" plate and the
// panel it sits in, so it is what the appearance sets follow. Inside the
// widget Kirigami.Theme already reports the style's colours and main.qml reads
// them there; the config dialog is an ordinary window whose Kirigami.Theme
// reports the colour scheme instead, so this reads the style the way libplasma
// does -- the style plasmarc names, merged across the config directories as
// KConfig merges them, its own `colors` file when it ships one, the colour
// scheme when it does not (Breeze's "default" ships none). The merging
// matters: applying a global theme in System Settings writes the style's name
// to ~/.config/kdedefaults/plasmarc, which Plasma puts first in
// XDG_CONFIG_DIRS, and reverts the user's own plasmarc entry.
class PlasmaStyle : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit PlasmaStyle(QObject *parent = nullptr);

    // Read afresh on every call: a config page asks once, when it opens.
    Q_INVOKABLE bool isDark() const;

    // The pieces isDark() is made of, public for the tests.
    // [Theme] name merged across `plasmarcPaths`, highest priority first as
    // QStandardPaths::locateAll() lists them: the first file that has the
    // entry wins, unless a file after it marked the entry immutable ([$i]).
    // "default" when none of them has it.
    static QString styleNameIn(const QStringList &plasmarcPaths);
    // [Colors:Window] BackgroundNormal of the colors file at `colorsPath`;
    // `colorSchemeWindow` when there is no such file or entry.
    static bool isDarkStyle(const QString &colorsPath, const QColor &colorSchemeWindow);
    // qGray() below 192, the rule plasma-integration and xdg-desktop-portal-kde
    // apply to a window background and ThemePolicy.isDarkBackground() mirrors.
    static bool isDarkBackground(const QColor &background);
};
