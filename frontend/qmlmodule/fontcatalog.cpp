#include "fontcatalog.h"

#include <QFontDatabase>

// Placeholder bodies so the QML side can be built against the final
// interface; each one is replaced by the real implementation.

FontCatalog::FontCatalog(QObject *parent)
    : QObject(parent)
{
}

QStringList FontCatalog::families() const
{
    QStringList result = QFontDatabase::families();
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList FontCatalog::alternateNames(const QString &family) const
{
    Q_UNUSED(family)
    return {};
}

QStringList FontCatalog::search(const QString &query) const
{
    const QString needle = query.trimmed();
    if (needle.isEmpty()) {
        return families();
    }
    QStringList result;
    for (const QString &family : families()) {
        if (family.contains(needle, Qt::CaseInsensitive)) {
            result.append(family);
        }
    }
    return result;
}

QString FontCatalog::resolveFamily(const QString &stored) const
{
    return families().contains(stored) ? stored : QString();
}

QVariantList FontCatalog::weights(const QString &family) const
{
    if (!families().contains(family)) {
        return {};
    }
    const QList<std::pair<int, QString>> faces = {
        {300, QStringLiteral("Light")}, {400, QStringLiteral("Regular")},
        {500, QStringLiteral("Medium")}, {600, QStringLiteral("DemiBold")},
        {700, QStringLiteral("Bold")}, {900, QStringLiteral("Black")},
    };
    QVariantList result;
    for (const auto &[weight, styleName] : faces) {
        result.append(QVariantMap{{QStringLiteral("weight"), weight},
                                  {QStringLiteral("styleName"), styleName}});
    }
    return result;
}

int FontCatalog::snapWeight(const QVariantList &available, int target) const
{
    int best = target;
    int bestDistance = -1;
    for (const QVariant &entry : available) {
        const int weight = entry.toMap().value(QStringLiteral("weight")).toInt();
        const int distance = qAbs(weight - target);
        if (bestDistance < 0 || distance < bestDistance) {
            best = weight;
            bestDistance = distance;
        }
    }
    return best;
}
