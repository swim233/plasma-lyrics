#include "stringlistsetting.h"

namespace PlasmaLyrics {

QStringList readStringListOrEmpty(const QSettings &settings, const StringListSetting &setting,
                                   const QStringList &defaultValue)
{
    if (settings.contains(setting.key)) {
        const QVariant stored = settings.value(setting.key);
        if (stored.isValid()) {
            // A bare hand-written `key=` is a single, valid, empty-string
            // element (not `@Invalid()`), so it reaches here rather than
            // the marker check below. Blank/whitespace-only entries are
            // dropped rather than passed through: a caller matching these
            // as prefixes (QString::startsWith()) would otherwise treat an
            // empty entry as matching every string, turning "no custom
            // prefixes configured" into "every prefix accepted" -- the
            // opposite of what a person clearing this field by hand meant.
            // This does not make `key=` the *sanctioned* way to write
            // "empty" -- writeStringListOrEmpty() still only ever produces
            // the marker for that -- it just makes the hand-edited form
            // read safely in the meantime, and the next save through this
            // module normalizes it to the marker form.
            QStringList result;
            for (const auto &entry : stored.toStringList()) {
                if (!entry.trimmed().isEmpty()) {
                    result.append(entry);
                }
            }
            return result;
        }
    }
    if (settings.value(setting.explicitEmptyKey, false).toBool()) {
        return QStringList();
    }
    return defaultValue;
}

void writeStringListOrEmpty(QSettings &settings, const StringListSetting &setting,
                             const QStringList &value)
{
    if (value.isEmpty()) {
        settings.remove(setting.key);
        settings.setValue(setting.explicitEmptyKey, true);
    } else {
        settings.setValue(setting.key, value);
        settings.remove(setting.explicitEmptyKey);
    }
}

bool migrateLegacyInvalidEntry(QSettings &settings, const QString &key)
{
    if (!settings.contains(key)) {
        return false;
    }
    if (settings.value(key).isValid()) {
        return false;
    }
    settings.remove(key);
    return true;
}

bool migrateLegacyInvalidEntryToExplicitEmpty(QSettings &settings, const StringListSetting &setting)
{
    if (!settings.contains(setting.key)) {
        return false;
    }
    if (settings.value(setting.key).isValid()) {
        return false;
    }
    settings.remove(setting.key);
    settings.setValue(setting.explicitEmptyKey, true);
    return true;
}

void writeStringListNoInvalid(QSettings &settings, const QString &key, const QStringList &value,
                               EmptyStringListPolicy policy)
{
    if (!value.isEmpty()) {
        settings.setValue(key, value);
        return;
    }
    switch (policy) {
    case EmptyStringListPolicy::RemoveKey:
        settings.remove(key);
        break;
    case EmptyStringListPolicy::KeepPresentAsBlank:
        settings.setValue(key, QStringList{QString()});
        break;
    }
}

} // namespace PlasmaLyrics
