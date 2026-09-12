#include "config.h"
#include "logging.h"

#include "core/config/stringlistsetting.h"

#include <QStandardPaths>
#include <QDebug>
#include <QUrl>

namespace PlasmaLyrics {

const QStringList &Config::builtInProviderOrder()
{
    // qq goes last: it is a full source, but the existing three keep their
    // established precedence unless the user reorders them.
    static const QStringList order{QStringLiteral("local"), QStringLiteral("netease"),
                                   QStringLiteral("amll"), QStringLiteral("qq")};
    return order;
}

namespace {

// Q24 / DESIGN.md decision 67. See core/config/stringlistsetting.h for why
// "explicitly empty" needs a companion key rather than the primary key's
// value.
const StringListSetting &blacklistSetting()
{
    static const StringListSetting setting{.key = QStringLiteral("players/blacklist"),
                                           .explicitEmptyKey = QStringLiteral("players/blacklistEmpty")};
    return setting;
}

const StringListSetting &musicUrlPrefixesSetting()
{
    static const StringListSetting setting{.key = QStringLiteral("filter/musicUrlPrefixes"),
                                           .explicitEmptyKey = QStringLiteral("filter/musicUrlPrefixesEmpty")};
    return setting;
}

// filter/platforms leaks the same `@Invalid()` literal (unchecking both
// platform checkboxes writes an empty QStringList) and reads/writes
// through the same three-state marker machinery -- but its *migration*
// target differs from the two keys above; see the comment on
// migrateLegacySettings() and DESIGN.md decision 67 for why.
const StringListSetting &platformsSetting()
{
    static const StringListSetting setting{.key = QStringLiteral("filter/platforms"),
                                           .explicitEmptyKey = QStringLiteral("filter/platformsEmpty")};
    return setting;
}

} // namespace

Config::Config()
    : m_settings(QSettings::IniFormat, QSettings::UserScope,
                 QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"))
{
}

void Config::migrateLegacySettings()
{
    // players/blacklist: migrate to "unset", restoring the built-in
    // default. Backed by case-specific evidence that this exact corruption
    // was unintended -- a real journal capture plus the user's own request
    // to have kdeconnect filtering restored -- not by any structural
    // property of the setting, and not by any cost/severity comparison
    // (several earlier attempts to justify the three-key split that way
    // were each disproved by probe; DESIGN.md decision 67, "unset" tier).
    migrateLegacyInvalidEntry(m_settings, blacklistSetting().key);
    // filter/musicUrlPrefixes: also migrates to "unset", but the choice is
    // unobservable, not evidence-backed. The built-in default here is
    // byte-identical to platformRules()'s netease urlPrefixes
    // (mprispolicy.cpp), and MprisPolicy::musicRejectReason() decides via
    // platformFor() before it ever reaches the musicUrlPrefixes loop -- so
    // "unset" and "explicitly empty" produce identical results for every
    // URL the built-in default would have matched (probed across
    // netease/apple/bilibili/bandcamp). It migrates the same way as
    // players/blacklist because Q24 named the two together, not because
    // of an independent argument for this key.
    migrateLegacyInvalidEntry(m_settings, musicUrlPrefixesSetting().key);
    // filter/platforms: migrate to "explicitly empty" instead. The basis
    // is origin, not severity: `@Invalid()` here can only be produced by
    // a user actively unchecking both platform checkboxes, so it is real
    // user state, not a defect artifact. Severity is NOT the argument and
    // in fact points the other way -- MprisPolicy::musicRejectReason()
    // rejects every enabled-platform track unconditionally once its
    // platform is unchecked, more severe than musicUrlPrefixes above --
    // so re-deriving this by severity gives the wrong answer. See
    // DESIGN.md decision 67 for the boundary condition under which the
    // origin argument stops holding.
    migrateLegacyInvalidEntryToExplicitEmpty(m_settings, platformsSetting());
    m_settings.sync();
}

PolicyConfig Config::policy() const
{
    PolicyConfig config;
    config.serviceBlacklist = readStringListOrEmpty(
        m_settings, blacklistSetting(),
        QStringList{QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*")});
    config.musicUrlPrefixes = readStringListOrEmpty(
        m_settings, musicUrlPrefixesSetting(),
        QStringList{QStringLiteral("https://music.163.com/"), QStringLiteral("http://music.163.com/")});
    config.useMetadataHeuristic = m_settings.value(QStringLiteral("filter/metadataHeuristic"), true).toBool();
    config.enabledPlatforms = readStringListOrEmpty(
        m_settings, platformsSetting(),
        QStringList{QStringLiteral("netease"), QStringLiteral("apple")});
    return config;
}

QUrl Config::neteaseBaseUrl() const
{
    return QUrl(m_settings.value(QStringLiteral("providers/netease/baseUrl"),
                                 QStringLiteral("https://music.163.com")).toString());
}

int Config::networkTimeoutMs() const
{
    return m_settings.value(QStringLiteral("providers/netease/timeoutMs"), 4000).toInt();
}

QUrl Config::qqSearchBaseUrl() const
{
    return QUrl(m_settings.value(QStringLiteral("providers/qq/searchBaseUrl"),
                                 QStringLiteral("https://u.y.qq.com")).toString());
}

QUrl Config::qqLyricBaseUrl() const
{
    return QUrl(m_settings.value(QStringLiteral("providers/qq/lyricBaseUrl"),
                                 QStringLiteral("https://c.y.qq.com")).toString());
}

// Its own key rather than borrowing providers/netease/timeoutMs, which is
// what amll does too: sharing one would mean tuning one source silently
// retuned another. Same default, so nothing changes for anyone who never
// sets it.
int Config::qqTimeoutMs() const
{
    return m_settings.value(QStringLiteral("providers/qq/timeoutMs"), 4000).toInt();
}

QStringList Config::providerOrder() const
{
    QStringList result;
    auto configured = m_settings.value(QStringLiteral("providers/order"),
                                       builtInProviderOrder()).toStringList();
    if (configured.isEmpty()) {
        qCWarning(lcDaemon) << "providers/order is empty; using the built-in provider order";
        configured = builtInProviderOrder();
    }
    // Existing configurations predate the local provider. Put the new,
    // failure-free source first once; the settings UI will persist the full
    // three-source order on its next save.
    bool hasLocal = false;
    for (const auto &provider : configured) {
        hasLocal |= provider.trimmed().compare(QStringLiteral("local"), Qt::CaseInsensitive) == 0;
    }
    if (!hasLocal) configured.prepend(QStringLiteral("local"));
    for (const auto &provider : configured) {
        const QString id = provider.trimmed().toCaseFolded();
        if (!id.isEmpty() && !result.contains(id)) {
            result.append(id);
        }
    }
    return result;
}

QStringList Config::enabledProviderOrder() const
{
    const QStringList order = providerOrder();
    QStringList enabled;
    if (m_settings.contains(QStringLiteral("providers/enabled"))) {
        for (const auto &provider : m_settings.value(QStringLiteral("providers/enabled")).toStringList()) {
            const QString id = provider.trimmed().toCaseFolded();
            if (!id.isEmpty() && !enabled.contains(id)) enabled.append(id);
        }
    } else {
        enabled = order;
    }
    QStringList active;
    for (const auto &provider : order) {
        if (enabled.contains(provider)) active.append(provider);
    }
    if (active.isEmpty()) {
        qCWarning(lcDaemon) << "providers/enabled selects no ordered provider; using built-in defaults";
        return builtInProviderOrder();
    }
    return active;
}

QString Config::localLyricsDirectory() const
{
    return m_settings.value(
        QStringLiteral("providers/local/directory"),
        QString(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/plasma-lyrics/lyrics"))).toString();
}

QUrl Config::amllIndexUrl() const
{
    return QUrl(m_settings.value(
        QStringLiteral("providers/amll/indexUrl"),
        QStringLiteral("https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/metadata/raw-lyrics-index.jsonl")).toString());
}

QUrl Config::amllContentBaseUrl() const
{
    return QUrl(m_settings.value(
        QStringLiteral("providers/amll/contentBaseUrl"),
        QStringLiteral("https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/")).toString());
}

int Config::amllTimeoutMs() const
{
    return m_settings.value(QStringLiteral("providers/amll/timeoutMs"), 8000).toInt();
}

qint64 Config::amllIndexMaxAgeSeconds() const
{
    return static_cast<qint64>(m_settings.value(
        QStringLiteral("providers/amll/indexRefreshHours"), 24).toInt()) * 60 * 60;
}

bool Config::fileLoggingEnabled() const
{
    return m_settings.value(QStringLiteral("logging/fileEnabled"), false).toBool();
}

QString Config::logFilePath() const
{
    return m_settings.value(
        QStringLiteral("logging/filePath"),
        QString(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/plasma-lyrics/plasma-lyricsd.log"))).toString();
}

bool Config::debugLoggingEnabled() const
{
    return m_settings.value(QStringLiteral("logging/debug"), false).toBool();
}

bool Config::filterCredits() const
{
    return m_settings.value(QStringLiteral("lyrics/filterLeadingCredits"), true).toBool();
}

QString Config::proxyMode() const
{
    const QString mode = m_settings.value(QStringLiteral("network/proxyMode"),
                                          QStringLiteral("none")).toString();
    if (mode == QStringLiteral("none") || mode == QStringLiteral("system")
        || mode == QStringLiteral("manual")) {
        return mode;
    }
    qCWarning(lcDaemon) << "network/proxyMode has an unknown value; treating it as none:" << mode;
    return QStringLiteral("none");
}

QString Config::proxyUrl() const
{
    return m_settings.value(QStringLiteral("network/proxyUrl"), QString()).toString();
}

} // namespace PlasmaLyrics
