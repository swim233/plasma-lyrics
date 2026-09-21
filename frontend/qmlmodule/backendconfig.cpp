#include "backendconfig.h"

#include "core/config/proxyspec.h"
#include "core/config/stringlistsetting.h"
#include "settingslog.h"

#include <KLocalizedString>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

using PlasmaLyrics::ProxySpec;

namespace {

// Bare i18n() would resolve against whatever domain happens to be active in
// the process that loaded this QML plugin, which is not necessarily this
// applet's -- lyricsource.cpp hits the same problem and uses the same fix.
constexpr auto i18nDomain = "plasma_applet_io.github.swim233.plasma-lyrics";

QSettings settings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"));
}

QString lines(const QStringList &values)
{
    return values.join(QLatin1Char('\n'));
}

QStringList list(const QString &value)
{
    QStringList result;
    for (const auto &line : value.split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty()) {
            result.append(line.trimmed());
        }
    }
    return result;
}

QStringList normalized(const QStringList &values)
{
    QStringList result;
    for (const auto &value : values) {
        const QString id = value.trimmed();
        if (id.isEmpty()) continue;
        bool duplicate = false;
        for (const auto &existing : result) {
            duplicate |= existing.compare(id, Qt::CaseInsensitive) == 0;
        }
        if (!duplicate) result.append(id);
    }
    return result;
}

bool containsProvider(const QStringList &values, const QString &provider)
{
    return values.contains(provider, Qt::CaseInsensitive);
}

QStringList defaultProviders()
{
    // Kept in step with Config::builtInProviderOrder() in the daemon. This
    // list is only the fallback shown when the service is not running to be
    // asked, and the two disagreeing would make a fresh configuration depend
    // on whether the service happened to be up when it was first opened.
    return {QStringLiteral("local"), QStringLiteral("netease"), QStringLiteral("amll"),
            QStringLiteral("qq")};
}

// Q24 / DESIGN.md decision 67: see core/config/stringlistsetting.h for why
// "explicitly empty" needs a companion key rather than the primary key's
// value. Same keys and marker names as daemon/src/config.cpp -- both sides
// must agree or the UI and the daemon would disagree about which state a
// freshly-migrated or freshly-emptied file is in.
const PlasmaLyrics::StringListSetting &blacklistSetting()
{
    static const PlasmaLyrics::StringListSetting setting{.key = QStringLiteral("players/blacklist"),
                                                         .explicitEmptyKey = QStringLiteral("players/blacklistEmpty")};
    return setting;
}

const PlasmaLyrics::StringListSetting &musicUrlPrefixesSetting()
{
    static const PlasmaLyrics::StringListSetting setting{.key = QStringLiteral("filter/musicUrlPrefixes"),
                                                         .explicitEmptyKey = QStringLiteral("filter/musicUrlPrefixesEmpty")};
    return setting;
}

// filter/platforms leaks the same `@Invalid()` literal (unchecking both
// platform checkboxes writes an empty QStringList) and reads/writes
// through the same three-state marker machinery -- but its *migration*
// target differs from the two keys above; see load()'s comment below and
// DESIGN.md decision 67 for why.
const PlasmaLyrics::StringListSetting &platformsSetting()
{
    static const PlasmaLyrics::StringListSetting setting{.key = QStringLiteral("filter/platforms"),
                                                         .explicitEmptyKey = QStringLiteral("filter/platformsEmpty")};
    return setting;
}

} // namespace

BackendConfig::BackendConfig(QObject *parent)
    : BackendConfig(QStringLiteral("systemctl"),
                    {QStringLiteral("--user"), QStringLiteral("restart"),
                     QStringLiteral("plasma-lyricsd")}, parent)
{
}

BackendConfig::BackendConfig(QString restartProgram, QStringList restartArguments, QObject *parent)
    : QObject(parent)
    , m_restartProgram(std::move(restartProgram))
    , m_restartArguments(std::move(restartArguments))
    , m_restartProcess(new QProcess(this))
{
    connect(m_restartProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (m_restartInProgress && error == QProcess::FailedToStart) {
            finishRestart(RestartFailed,
                          QStringLiteral("could not start restart command: %1")
                              .arg(m_restartProcess->errorString()));
        }
    });
    connect(m_restartProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (!m_restartInProgress) {
            return;
        }
        if (exitStatus == QProcess::CrashExit) {
            finishRestart(RestartFailed, QStringLiteral("restart command crashed"));
        } else if (exitCode != 0) {
            finishRestart(RestartFailed,
                          QStringLiteral("restart command exited with code %1").arg(exitCode));
        } else {
            finishRestart(RestartSucceeded);
        }
    });
    load();
}

BackendConfig::~BackendConfig()
{
    if (m_restartProcess->state() != QProcess::NotRunning) {
        disconnect(m_restartProcess, nullptr, this, nullptr);
        m_restartProcess->kill();
        m_restartProcess->waitForFinished(1000);
    }
}

#define GETTER(type, name, member) type BackendConfig::name() const { return member; }
GETTER(QString, serviceBlacklist, m_serviceBlacklist)
GETTER(QString, musicUrlPrefixes, m_musicUrlPrefixes)
GETTER(bool, platformNetease, m_platformNetease)
GETTER(bool, platformApple, m_platformApple)
GETTER(bool, metadataHeuristic, m_metadataHeuristic)
GETTER(bool, filterCredits, m_filterCredits)
GETTER(QString, neteaseBaseUrl, m_neteaseBaseUrl)
GETTER(int, networkTimeoutMs, m_networkTimeoutMs)
GETTER(QString, amllIndexUrl, m_amllIndexUrl)
GETTER(QString, amllContentBaseUrl, m_amllContentBaseUrl)
GETTER(int, amllTimeoutMs, m_amllTimeoutMs)
GETTER(int, amllIndexRefreshHours, m_amllIndexRefreshHours)
GETTER(bool, fileLoggingEnabled, m_fileLoggingEnabled)
GETTER(QString, logFilePath, m_logFilePath)
GETTER(bool, debugLoggingEnabled, m_debugLoggingEnabled)
GETTER(QString, proxyMode, m_proxyMode)
GETTER(QString, proxyUrl, m_proxyUrl)
GETTER(bool, dirty, m_dirty)
GETTER(BackendConfig::RestartState, restartState, m_restartState)
GETTER(bool, restartInProgress, m_restartInProgress)
GETTER(QString, restartError, m_restartError)
#undef GETTER

QStringList BackendConfig::providerOrder() const { return m_providerOrder; }
QStringList BackendConfig::enabledProviders() const { return m_enabledProviders; }
bool BackendConfig::providerDiscoveryFallback() const { return m_providerDiscoveryFallback; }
QString BackendConfig::localLyricsDirectory() const { return m_localLyricsDirectory; }

QStringList BackendConfig::visibleProviderOrder() const
{
    QStringList result;
    for (const auto &ordered : m_providerOrder) {
        for (const auto &available : m_availableProviders) {
            if (ordered.compare(available, Qt::CaseInsensitive) == 0
                && !containsProvider(result, available)) {
                result.append(available);
            }
        }
    }
    return result;
}

QVariantList BackendConfig::providerEntries() const
{
    QVariantList result;
    for (const auto &provider : visibleProviderOrder()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), provider);
        entry.insert(QStringLiteral("enabled"), containsProvider(m_enabledProviders, provider));
        result.append(entry);
    }
    return result;
}

void BackendConfig::markDirty()
{
    if (!m_dirty) {
        m_dirty = true;
        Q_EMIT dirtyChanged();
    }
    Q_EMIT changed();
}

#define SETTER(type, name, member) \
    void BackendConfig::name(type value) \
    { \
        if (member == value) return; \
        member = value; \
        markDirty(); \
    }
SETTER(const QString &, setServiceBlacklist, m_serviceBlacklist)
SETTER(const QString &, setMusicUrlPrefixes, m_musicUrlPrefixes)
SETTER(bool, setPlatformNetease, m_platformNetease)
SETTER(bool, setPlatformApple, m_platformApple)
SETTER(bool, setMetadataHeuristic, m_metadataHeuristic)
SETTER(bool, setFilterCredits, m_filterCredits)
SETTER(const QString &, setNeteaseBaseUrl, m_neteaseBaseUrl)
SETTER(int, setNetworkTimeoutMs, m_networkTimeoutMs)
SETTER(const QString &, setAmllIndexUrl, m_amllIndexUrl)
SETTER(const QString &, setAmllContentBaseUrl, m_amllContentBaseUrl)
SETTER(int, setAmllTimeoutMs, m_amllTimeoutMs)
SETTER(int, setAmllIndexRefreshHours, m_amllIndexRefreshHours)
SETTER(bool, setFileLoggingEnabled, m_fileLoggingEnabled)
SETTER(const QString &, setLogFilePath, m_logFilePath)
SETTER(bool, setDebugLoggingEnabled, m_debugLoggingEnabled)
SETTER(const QString &, setProxyMode, m_proxyMode)
SETTER(const QString &, setProxyUrl, m_proxyUrl)
SETTER(const QString &, setLocalLyricsDirectory, m_localLyricsDirectory)
#undef SETTER

QString BackendConfig::proxyUrlError(const QString &url) const
{
    ProxySpec::Error error = ProxySpec::Error::None;
    if (ProxySpec::parse(url, &error)) {
        return QString();
    }
    switch (error) {
    case ProxySpec::Error::InvalidUrl:
        return i18nd(i18nDomain, "Could not parse the address.");
    case ProxySpec::Error::UnsupportedScheme:
        return i18nd(i18nDomain, "Unsupported protocol. Only socks5 and http are supported.");
    case ProxySpec::Error::MissingHost:
        return i18nd(i18nDomain, "The address is missing a host.");
    case ProxySpec::Error::MissingPort:
        return i18nd(i18nDomain, "The address is missing a port.");
    case ProxySpec::Error::UnexpectedPath:
        return i18nd(i18nDomain, "The address must not include a path or query.");
    case ProxySpec::Error::None:
        break;
    }
    return QString();
}

void BackendConfig::setProviderOrder(const QStringList &value)
{
    const auto order = normalized(value);
    if (m_providerOrder == order) return;
    m_providerOrder = order;
    markDirty();
}

void BackendConfig::setEnabledProviders(const QStringList &value)
{
    const auto enabled = normalized(value);
    if (m_enabledProviders == enabled) return;
    m_enabledProviders = enabled;
    markDirty();
}

void BackendConfig::discoverProviders()
{
    QStringList available;
    QDBusInterface interface(
        QStringLiteral("io.github.swim233.PlasmaLyrics"),
        QStringLiteral("/io/github/swim233/PlasmaLyrics"),
        QStringLiteral("io.github.swim233.PlasmaLyrics.Control"),
        QDBusConnection::sessionBus());
    interface.setTimeout(1000);
    const QDBusReply<QStringList> reply = interface.call(QStringLiteral("AvailableProviders"));
    if (reply.isValid() && !reply.value().isEmpty()) {
        available = normalized(reply.value());
        m_providerDiscoveryFallback = false;
    } else {
        available = defaultProviders();
        m_providerDiscoveryFallback = true;
    }
    m_availableProviders = available;
}

void BackendConfig::load()
{
    discoverProviders();
    auto config = settings();
    // One-time, idempotent: same cleanup as Config::migrateLegacySettings()
    // (daemon/src/config.cpp), run here too so the settings UI shows the
    // post-migration state even if opened before the daemon next restarts.
    // Synced explicitly rather than left to `config`'s destructor, so it
    // matches Config::migrateLegacySettings()'s own explicit sync() and a
    // later reader isn't left wondering which of the two is the odd one out.
    // filter/platforms migrates to "explicitly empty" rather than "unset"
    // -- see Config::migrateLegacySettings() (daemon/src/config.cpp) and
    // DESIGN.md decision 67 for why this one key's migration semantics
    // differ.
    PlasmaLyrics::migrateLegacyInvalidEntry(config, blacklistSetting().key);
    PlasmaLyrics::migrateLegacyInvalidEntry(config, musicUrlPrefixesSetting().key);
    PlasmaLyrics::migrateLegacyInvalidEntryToExplicitEmpty(config, platformsSetting());
    config.sync();
    m_serviceBlacklist = lines(PlasmaLyrics::readStringListOrEmpty(
        config, blacklistSetting(),
        QStringList{QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*")}));
    m_musicUrlPrefixes = lines(PlasmaLyrics::readStringListOrEmpty(
        config, musicUrlPrefixesSetting(),
        QStringList{QStringLiteral("https://music.163.com/"), QStringLiteral("http://music.163.com/")}));
    m_metadataHeuristic = config.value(QStringLiteral("filter/metadataHeuristic"), true).toBool();
    const auto platforms = PlasmaLyrics::readStringListOrEmpty(
        config, platformsSetting(),
        QStringList{QStringLiteral("netease"), QStringLiteral("apple")});
    m_platformNetease = platforms.contains(QStringLiteral("netease"));
    m_platformApple = platforms.contains(QStringLiteral("apple"));
    m_filterCredits = config.value(QStringLiteral("lyrics/filterLeadingCredits"), true).toBool();
    m_neteaseBaseUrl = config.value(QStringLiteral("providers/netease/baseUrl"),
                                    QStringLiteral("https://music.163.com")).toString();
    m_networkTimeoutMs = config.value(QStringLiteral("providers/netease/timeoutMs"), 4000).toInt();
    m_providerOrder = normalized(config.value(
        QStringLiteral("providers/order"), defaultProviders()).toStringList());
    if (m_providerOrder.isEmpty()) m_providerOrder = defaultProviders();
    if (!m_providerOrder.contains(QStringLiteral("local"), Qt::CaseInsensitive)) {
        m_providerOrder.prepend(QStringLiteral("local"));
    }
    for (const auto &provider : m_availableProviders) {
        if (!m_providerOrder.contains(provider, Qt::CaseInsensitive)) {
            m_providerOrder.append(provider);
        }
    }
    m_enabledProviders = config.contains(QStringLiteral("providers/enabled"))
        ? normalized(config.value(QStringLiteral("providers/enabled")).toStringList())
        : m_providerOrder;
    bool hasVisibleEnabled = false;
    for (const auto &provider : visibleProviderOrder()) {
        hasVisibleEnabled |= containsProvider(m_enabledProviders, provider);
    }
    if (!hasVisibleEnabled && !visibleProviderOrder().isEmpty()) {
        m_enabledProviders.append(visibleProviderOrder().first());
    }
    m_localLyricsDirectory = config.value(
        QStringLiteral("providers/local/directory"),
        QString(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/plasma-lyrics/lyrics"))).toString();
    m_amllIndexUrl = config.value(
        QStringLiteral("providers/amll/indexUrl"),
        QStringLiteral("https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/metadata/raw-lyrics-index.jsonl")).toString();
    m_amllContentBaseUrl = config.value(
        QStringLiteral("providers/amll/contentBaseUrl"),
        QStringLiteral("https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/")).toString();
    m_amllTimeoutMs = config.value(QStringLiteral("providers/amll/timeoutMs"), 8000).toInt();
    m_amllIndexRefreshHours = config.value(QStringLiteral("providers/amll/indexRefreshHours"), 24).toInt();
    m_fileLoggingEnabled = config.value(QStringLiteral("logging/fileEnabled"), false).toBool();
    m_logFilePath = config.value(
        QStringLiteral("logging/filePath"),
        QString(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/plasma-lyrics/plasma-lyricsd.log"))).toString();
    m_debugLoggingEnabled = config.value(QStringLiteral("logging/debug"), false).toBool();
    m_proxyMode = config.value(QStringLiteral("network/proxyMode"), QStringLiteral("none")).toString();
    if (m_proxyMode != QStringLiteral("none") && m_proxyMode != QStringLiteral("system")
        && m_proxyMode != QStringLiteral("manual")) {
        m_proxyMode = QStringLiteral("none");
    }
    m_proxyUrl = config.value(QStringLiteral("network/proxyUrl"), QString()).toString();
    const bool wasDirty = m_dirty;
    m_dirty = false;
    Q_EMIT changed();
    if (wasDirty) {
        Q_EMIT dirtyChanged();
    }
}

bool BackendConfig::save()
{
    // Checked before anything is written: an invalid manual address must
    // leave every other pending change unsaved too, not just the proxy
    // fields, so a retry after fixing the address has nothing left stale.
    if (m_proxyMode == QStringLiteral("manual") && !proxyUrlError(m_proxyUrl).isEmpty()) {
        PlasmaLyrics::reportSaveFailed(QStringLiteral("ini"), QStringLiteral("proxy-url-invalid"));
        return false;
    }
    auto config = settings();

    QStringList platforms;
    if (m_platformNetease) {
        platforms.append(QStringLiteral("netease"));
    }
    if (m_platformApple) {
        platforms.append(QStringLiteral("apple"));
    }

    // Snapshot of every key this call is about to write: the value the
    // CURRENT file holds (read with load()'s own helpers and defaults, so
    // an in-between hand edit or daemon write is picked up) paired with the
    // value the member about to be written renders to. Read before any
    // write below touches `config`, and compared once the sync below
    // succeeds -- a divergence becomes one `config changed` line each
    // (DESIGN.md decision 75). Whether a key changed is decided from the
    // rendered forms for every key except network/proxyUrl (see below);
    // the two are the same value for everything else, so this is also what
    // gets reported.
    using PlasmaLyrics::renderConfigValue;
    using PlasmaLyrics::renderProxyUrlForLog;
    struct PendingChange {
        QString key;
        QString oldRendered;
        QString newRendered;
        bool changed;
    };
    QList<PendingChange> pending;
    auto record = [&](const QString &key, const QVariant &oldValue, const QVariant &newValue) {
        const QString oldRendered = renderConfigValue(oldValue);
        const QString newRendered = renderConfigValue(newValue);
        pending.append({key, oldRendered, newRendered, oldRendered != newRendered});
    };

    const QStringList oldBlacklist = PlasmaLyrics::readStringListOrEmpty(
        config, blacklistSetting(),
        QStringList{QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*")});
    const QStringList oldMusicUrlPrefixes = PlasmaLyrics::readStringListOrEmpty(
        config, musicUrlPrefixesSetting(),
        QStringList{QStringLiteral("https://music.163.com/"), QStringLiteral("http://music.163.com/")});
    const QStringList oldPlatforms = PlasmaLyrics::readStringListOrEmpty(
        config, platformsSetting(),
        QStringList{QStringLiteral("netease"), QStringLiteral("apple")});
    const QStringList oldProviderOrder = config.value(
        QStringLiteral("providers/order"), defaultProviders()).toStringList();
    const QStringList oldEnabledProviders = config.contains(QStringLiteral("providers/enabled"))
        ? config.value(QStringLiteral("providers/enabled")).toStringList()
        : oldProviderOrder;

    record(QStringLiteral("players/blacklist"), oldBlacklist, list(m_serviceBlacklist));
    record(QStringLiteral("filter/musicUrlPrefixes"), oldMusicUrlPrefixes, list(m_musicUrlPrefixes));
    record(QStringLiteral("filter/metadataHeuristic"),
           config.value(QStringLiteral("filter/metadataHeuristic"), true), m_metadataHeuristic);
    record(QStringLiteral("filter/platforms"), oldPlatforms, platforms);
    record(QStringLiteral("lyrics/filterLeadingCredits"),
           config.value(QStringLiteral("lyrics/filterLeadingCredits"), true), m_filterCredits);
    record(QStringLiteral("providers/netease/baseUrl"),
           config.value(QStringLiteral("providers/netease/baseUrl"), QStringLiteral("https://music.163.com")),
           m_neteaseBaseUrl);
    record(QStringLiteral("providers/netease/timeoutMs"),
           config.value(QStringLiteral("providers/netease/timeoutMs"), 4000), m_networkTimeoutMs);
    record(QStringLiteral("providers/order"), oldProviderOrder, m_providerOrder);
    record(QStringLiteral("providers/enabled"), oldEnabledProviders, m_enabledProviders);
    record(QStringLiteral("providers/local/directory"),
           config.value(QStringLiteral("providers/local/directory"),
               QString(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                   + QStringLiteral("/plasma-lyrics/lyrics"))),
           m_localLyricsDirectory);
    record(QStringLiteral("providers/amll/indexUrl"),
           config.value(QStringLiteral("providers/amll/indexUrl"),
               QStringLiteral("https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/metadata/raw-lyrics-index.jsonl")),
           m_amllIndexUrl);
    record(QStringLiteral("providers/amll/contentBaseUrl"),
           config.value(QStringLiteral("providers/amll/contentBaseUrl"),
               QStringLiteral("https://raw.githubusercontent.com/amll-dev/amll-ttml-db/main/")),
           m_amllContentBaseUrl);
    record(QStringLiteral("providers/amll/timeoutMs"),
           config.value(QStringLiteral("providers/amll/timeoutMs"), 8000), m_amllTimeoutMs);
    record(QStringLiteral("providers/amll/indexRefreshHours"),
           config.value(QStringLiteral("providers/amll/indexRefreshHours"), 24), m_amllIndexRefreshHours);
    record(QStringLiteral("logging/fileEnabled"),
           config.value(QStringLiteral("logging/fileEnabled"), false), m_fileLoggingEnabled);
    record(QStringLiteral("logging/filePath"),
           config.value(QStringLiteral("logging/filePath"),
               QString(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                   + QStringLiteral("/plasma-lyrics/plasma-lyricsd.log"))),
           m_logFilePath);
    record(QStringLiteral("logging/debug"),
           config.value(QStringLiteral("logging/debug"), false), m_debugLoggingEnabled);
    record(QStringLiteral("network/proxyMode"),
           config.value(QStringLiteral("network/proxyMode"), QStringLiteral("none")), m_proxyMode);
    // network/proxyUrl may carry credentials, so it is rendered through
    // renderProxyUrlForLog() rather than renderConfigValue() for the
    // reported/forwarded old/new values. But that rendering hides
    // credentials by design, so it cannot also be what decides whether the
    // key changed: a credentials-only edit (same host:port) would then
    // rewrite the ini file while reporting and forwarding nothing. Whether
    // it changed is therefore decided from the RAW strings instead; the
    // reported old/new values stay the redacted ones (identical to each
    // other in that case, which is the point -- a change happened, but
    // nothing more can be shown about it).
    const QString oldProxyUrlRaw = config.value(QStringLiteral("network/proxyUrl"), QString()).toString();
    const QString newProxyUrlRaw = m_proxyUrl;
    pending.append({QStringLiteral("network/proxyUrl"),
                    renderProxyUrlForLog(oldProxyUrlRaw),
                    renderProxyUrlForLog(newProxyUrlRaw),
                    oldProxyUrlRaw != newProxyUrlRaw});

    PlasmaLyrics::writeStringListOrEmpty(config, blacklistSetting(), list(m_serviceBlacklist));
    PlasmaLyrics::writeStringListOrEmpty(config, musicUrlPrefixesSetting(), list(m_musicUrlPrefixes));
    config.setValue(QStringLiteral("filter/metadataHeuristic"), m_metadataHeuristic);
    PlasmaLyrics::writeStringListOrEmpty(config, platformsSetting(), platforms);
    config.setValue(QStringLiteral("lyrics/filterLeadingCredits"), m_filterCredits);
    config.setValue(QStringLiteral("providers/netease/baseUrl"), m_neteaseBaseUrl);
    config.setValue(QStringLiteral("providers/netease/timeoutMs"), m_networkTimeoutMs);
    // providers/order and providers/enabled keep their existing "empty
    // means: use the built-in default" semantics (decision 67); only the
    // `@Invalid()` litter an empty QStringList write would otherwise
    // produce is avoided, via two different substitutes because the two
    // keys' readers treat an absent key differently -- see
    // core/config/stringlistsetting.h.
    PlasmaLyrics::writeStringListNoInvalid(config, QStringLiteral("providers/order"), m_providerOrder,
                                           PlasmaLyrics::EmptyStringListPolicy::RemoveKey);
    PlasmaLyrics::writeStringListNoInvalid(config, QStringLiteral("providers/enabled"), m_enabledProviders,
                                           PlasmaLyrics::EmptyStringListPolicy::KeepPresentAsBlank);
    config.setValue(QStringLiteral("providers/local/directory"), m_localLyricsDirectory);
    config.setValue(QStringLiteral("providers/amll/indexUrl"), m_amllIndexUrl);
    config.setValue(QStringLiteral("providers/amll/contentBaseUrl"), m_amllContentBaseUrl);
    config.setValue(QStringLiteral("providers/amll/timeoutMs"), m_amllTimeoutMs);
    config.setValue(QStringLiteral("providers/amll/indexRefreshHours"), m_amllIndexRefreshHours);
    config.setValue(QStringLiteral("logging/fileEnabled"), m_fileLoggingEnabled);
    config.setValue(QStringLiteral("logging/filePath"), m_logFilePath);
    config.setValue(QStringLiteral("logging/debug"), m_debugLoggingEnabled);
    config.setValue(QStringLiteral("network/proxyMode"), m_proxyMode);
    config.setValue(QStringLiteral("network/proxyUrl"), m_proxyUrl);
    config.sync();
    if (config.status() != QSettings::NoError) {
        PlasmaLyrics::reportSaveFailed(QStringLiteral("ini"),
            config.status() == QSettings::AccessError
                ? QStringLiteral("ini-access-error")
                : QStringLiteral("ini-format-error"));
        return false;
    }
    for (const auto &change : pending) {
        if (change.changed) {
            PlasmaLyrics::reportConfigChange({.store = QStringLiteral("ini"),
                                              .applet = QString(),
                                              .form = QString(),
                                              .key = change.key,
                                              .oldValue = change.oldRendered,
                                              .newValue = change.newRendered});
        }
    }
    if (m_dirty) {
        m_dirty = false;
        Q_EMIT dirtyChanged();
    }
    Q_EMIT saved();
    return true;
}

bool BackendConfig::moveProvider(int from, int to)
{
    auto visible = visibleProviderOrder();
    if (from < 0 || to < 0 || from >= visible.size() || to >= visible.size()
        || from == to) {
        return false;
    }
    visible.move(from, to);
    qsizetype visibleIndex = 0;
    QStringList reordered = m_providerOrder;
    for (qsizetype i = 0; i < reordered.size(); ++i) {
        if (m_availableProviders.contains(reordered.at(i), Qt::CaseInsensitive)) {
            reordered[i] = visible.at(visibleIndex++);
        }
    }
    m_providerOrder = reordered;
    markDirty();
    return true;
}

bool BackendConfig::setProviderEnabled(const QString &provider, bool enabled)
{
    if (!containsProvider(m_availableProviders, provider)) return false;
    const bool currentlyEnabled = containsProvider(m_enabledProviders, provider);
    if (currentlyEnabled == enabled) return true;
    if (!enabled) {
        int visibleEnabled = 0;
        for (const auto &id : visibleProviderOrder()) {
            if (containsProvider(m_enabledProviders, id)) ++visibleEnabled;
        }
        if (visibleEnabled <= 1) return false;
        m_enabledProviders.removeIf([&provider](const QString &id) {
            return id.compare(provider, Qt::CaseInsensitive) == 0;
        });
    } else {
        m_enabledProviders.append(provider);
    }
    markDirty();
    return true;
}

bool BackendConfig::restartService()
{
    if (m_restartInProgress) {
        return false;
    }
    const bool hadError = !m_restartError.isEmpty();
    m_restartError.clear();
    m_restartState = Restarting;
    m_restartInProgress = true;
    if (hadError) {
        Q_EMIT restartErrorChanged();
    }
    Q_EMIT restartStateChanged();
    Q_EMIT restartInProgressChanged();
    // Reported before start(), not after: start() can fail synchronously
    // (an empty/invalid program name raises errorOccurred() before
    // returning), which would otherwise let a "restart finished" line beat
    // "restart requested" into the log.
    PlasmaLyrics::reportRestartRequested();
    m_restartProcess->start(m_restartProgram, m_restartArguments);
    return true;
}

void BackendConfig::finishRestart(RestartState state, const QString &error)
{
    const bool errorChanged = m_restartError != error;
    m_restartError = error;
    m_restartState = state;
    m_restartInProgress = false;
    if (errorChanged) {
        Q_EMIT restartErrorChanged();
    }
    Q_EMIT restartStateChanged();
    Q_EMIT restartInProgressChanged();
    Q_EMIT restartFinished(state == RestartSucceeded, error);
    PlasmaLyrics::reportRestartFinished(state == RestartSucceeded, error);
}
