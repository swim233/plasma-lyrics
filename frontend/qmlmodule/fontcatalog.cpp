#include "fontcatalog.h"

#include <QFont>
#include <QFontDatabase>
#include <QVariantMap>

#include <fontconfig/fontconfig.h>

#include <algorithm>

namespace
{

// Every installed pattern's FC_FAMILY values with their FC_FAMILYLANG. Qt has
// no API for a family's names in other languages -- it lists each family
// under its name in the process language only -- so they come from
// fontconfig directly. FcFontList is what Qt itself builds its list from.
QList<QList<FontMatching::FamilyName>> fontconfigFamilies()
{
    QList<QList<FontMatching::FamilyName>> patterns;
    FcPattern *pattern = FcPatternCreate();
    FcObjectSet *objects = FcObjectSetBuild(FC_FAMILY, FC_FAMILYLANG, static_cast<char *>(nullptr));
    FcFontSet *fonts = pattern && objects ? FcFontList(nullptr, pattern, objects) : nullptr;
    if (fonts) {
        patterns.reserve(fonts->nfont);
        for (int i = 0; i < fonts->nfont; ++i) {
            QList<FontMatching::FamilyName> names;
            FcChar8 *family = nullptr;
            for (int k = 0; FcPatternGetString(fonts->fonts[i], FC_FAMILY, k, &family) == FcResultMatch; ++k) {
                FcChar8 *lang = nullptr;
                const bool hasLang = FcPatternGetString(fonts->fonts[i], FC_FAMILYLANG, k, &lang) == FcResultMatch;
                names.append({QString::fromUtf8(reinterpret_cast<const char *>(family)),
                              hasLang ? QString::fromUtf8(reinterpret_cast<const char *>(lang)) : QString()});
            }
            if (!names.isEmpty()) {
                patterns.append(names);
            }
        }
        FcFontSetDestroy(fonts);
    }
    if (objects) {
        FcObjectSetDestroy(objects);
    }
    if (pattern) {
        FcPatternDestroy(pattern);
    }
    return patterns;
}

bool coversOfferedScript(const QString &family)
{
    const QList<QFontDatabase::WritingSystem> systems = QFontDatabase::writingSystems(family);
    return std::any_of(systems.cbegin(), systems.cend(), [](QFontDatabase::WritingSystem system) {
        switch (system) {
        case QFontDatabase::Latin:
        case QFontDatabase::SimplifiedChinese:
        case QFontDatabase::TraditionalChinese:
        case QFontDatabase::Japanese:
        case QFontDatabase::Korean:
            return true;
        default:
            return false;
        }
    });
}

QList<FontMatching::Face> facesOf(const QString &listed)
{
    QList<FontMatching::Face> faces;
    for (const QString &style : QFontDatabase::styles(listed)) {
        // QFontDatabase::italic() is false for oblique faces; the style of
        // the font it builds is not.
        const bool upright = QFontDatabase::font(listed, style, 12).style() == QFont::StyleNormal;
        faces.append({style, QFontDatabase::weight(listed, style), upright});
    }
    return faces;
}

} // namespace

FontCatalog::FontCatalog(QObject *parent)
    : QObject(parent)
{
}

void FontCatalog::ensureBuilt() const
{
    if (m_built) {
        return;
    }
    m_built = true;

    // Qt fills its database, and initialises fontconfig, on this first call.
    const QStringList listed = QFontDatabase::families();
    m_names = FontMatching::buildNameIndex(fontconfigFamilies(), listed);

    // Qt's generic "Sans Serif", "Serif" and "Monospace" are not backed by any
    // font: each has one synthetic 400 face, so the weight list would offer
    // nothing to choose from. A family with no upright face at all ("Nimbus
    // Sans [URW ]" holds only the italics) would render slanted and offer no
    // weight either.
    for (const QString &family : listed) {
        if (m_names.backed.contains(family) && QFontDatabase::isScalable(family) && coversOfferedScript(family)
            && !FontMatching::uprightWeights(facesOf(family)).isEmpty()) {
            m_families.append(family);
        }
    }
    std::sort(m_families.begin(), m_families.end(), [](const QString &a, const QString &b) {
        const int folded = QString::compare(a, b, Qt::CaseInsensitive);
        return folded != 0 ? folded < 0 : a < b;
    });

    m_searchNames.reserve(m_families.size());
    for (const QString &family : std::as_const(m_families)) {
        m_searchNames.append(QStringList{family} + m_names.alternates.value(family));
    }
}

QStringList FontCatalog::families() const
{
    ensureBuilt();
    return m_families;
}

QStringList FontCatalog::alternateNames(const QString &family) const
{
    ensureBuilt();
    return m_names.alternates.value(family);
}

QStringList FontCatalog::search(const QString &query) const
{
    ensureBuilt();
    if (query.trimmed().isEmpty()) {
        return m_families;
    }
    QStringList result;
    for (const qsizetype i : FontMatching::fuzzyRank(query, m_searchNames)) {
        result.append(m_families.at(i));
    }
    return result;
}

QString FontCatalog::resolveFamily(const QString &stored) const
{
    if (stored.isEmpty()) {
        return {};
    }
    ensureBuilt();
    return m_names.listedByName.value(stored);
}

QVariantList FontCatalog::weights(const QString &family) const
{
    return weightsOf(family, true);
}

QVariantList FontCatalog::italicWeights(const QString &family) const
{
    return weightsOf(family, false);
}

QVariantList FontCatalog::weightsOf(const QString &family, bool upright) const
{
    // Resolved even though callers pass a listed name: Qt reports no styles
    // for a name it only knows as an alias, and the Plasma font's family is
    // stored in whatever language it was picked in.
    const QString listed = resolveFamily(family);
    if (listed.isEmpty() || !m_names.backed.contains(listed)) {
        return {};
    }
    const QList<FontMatching::Face> faces = facesOf(listed);
    QVariantList result;
    for (const FontMatching::Weight &face :
         upright ? FontMatching::uprightWeights(faces) : FontMatching::italicWeights(faces)) {
        result.append(QVariantMap{{QStringLiteral("weight"), face.weight},
                                  {QStringLiteral("styleName"), face.styleName}});
    }
    return result;
}

int FontCatalog::snapWeight(const QVariantList &available, int target) const
{
    QList<int> weights;
    weights.reserve(available.size());
    for (const QVariant &entry : available) {
        weights.append(entry.toMap().value(QStringLiteral("weight")).toInt());
    }
    return FontMatching::snapWeight(weights, target);
}
