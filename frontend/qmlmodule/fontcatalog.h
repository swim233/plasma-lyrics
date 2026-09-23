#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>

// The installed font families the applet offers, and the per-family facts the
// renderer and the config pages both need. One instance per QML engine; the
// list is built once, on first use.
//
// Every "family" this class hands out is a name exactly as families() lists
// it. Qt lists a family under its name in the current locale (under zh_CN,
// "微软雅黑", not "Microsoft YaHei"), so a name saved in one locale is not
// necessarily listed in another; resolveFamily() is the only way back from a
// stored name to a listed one. QFontDatabase::hasFamily() is no substitute:
// its alias lookup depends on whether families() has already been called.
//
// FontPolicy.js composes these calls into the effective family and weight;
// QML code should go through it rather than calling this class directly.
class FontCatalog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit FontCatalog(QObject *parent = nullptr);

    // Scalable families covering Latin or any of Simplified Chinese,
    // Traditional Chinese, Japanese or Korean, sorted case-insensitively.
    Q_INVOKABLE QStringList families() const;

    // Every other name fontconfig knows `family` by (other languages), for
    // the secondary label beside it. Never contains `family` itself; empty
    // for an unknown family.
    Q_INVOKABLE QStringList alternateNames(const QString &family) const;

    // Fuzzy search over families(), matching each family's listed name and
    // all its alternateNames(). Best match first, ties in families() order.
    // An empty or whitespace-only query returns families() unchanged.
    Q_INVOKABLE QStringList search(const QString &query) const;

    // The listed name for a stored name given in any language fontconfig
    // knows it by. "" for "" and for a family that is not installed. Checks
    // every installed family, not only the filtered families() list.
    Q_INVOKABLE QString resolveFamily(const QString &stored) const;

    // The family's upright (non-italic) faces as [{weight: int, styleName:
    // string}], one entry per distinct weight, lightest first. `family` must
    // be a listed name (resolveFamily()'s result, or the Plasma font's
    // family); empty list for an unknown family.
    Q_INVOKABLE QVariantList weights(const QString &family) const;

    // The weight from `available` (a weights() result) that the CSS Fonts
    // level 4 matching rule picks for `target`. `target` itself when present
    // or when `available` is empty. Otherwise, for a target in [400, 500]:
    // the lightest weight in (target, 500], else the heaviest below target,
    // else the lightest above 500. Below 400: the heaviest below target,
    // else the lightest above. Above 500: the lightest above target, else
    // the heaviest below. So 400 prefers 500, and 500 prefers 400.
    Q_INVOKABLE int snapWeight(const QVariantList &available, int target) const;
};
