#include "backendconfig.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace {

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
    return {QStringLiteral("local"), QStringLiteral("netease"), QStringLiteral("amll")};
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
SETTER(const QString &, setLocalLyricsDirectory, m_localLyricsDirectory)
#undef SETTER

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
    m_serviceBlacklist = lines(config.value(
        QStringLiteral("players/blacklist"),
        QStringList{QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*")}).toStringList());
    m_musicUrlPrefixes = lines(config.value(
        QStringLiteral("filter/musicUrlPrefixes"),
        QStringList{QStringLiteral("https://music.163.com/"), QStringLiteral("http://music.163.com/")}).toStringList());
    m_metadataHeuristic = config.value(QStringLiteral("filter/metadataHeuristic"), true).toBool();
    const auto platforms = config.value(
        QStringLiteral("filter/platforms"),
        QStringList{QStringLiteral("netease"), QStringLiteral("apple")}).toStringList();
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
    const bool wasDirty = m_dirty;
    m_dirty = false;
    Q_EMIT changed();
    if (wasDirty) {
        Q_EMIT dirtyChanged();
    }
}

bool BackendConfig::save()
{
    auto config = settings();
    config.setValue(QStringLiteral("players/blacklist"), list(m_serviceBlacklist));
    config.setValue(QStringLiteral("filter/musicUrlPrefixes"), list(m_musicUrlPrefixes));
    config.setValue(QStringLiteral("filter/metadataHeuristic"), m_metadataHeuristic);
    QStringList platforms;
    if (m_platformNetease) {
        platforms.append(QStringLiteral("netease"));
    }
    if (m_platformApple) {
        platforms.append(QStringLiteral("apple"));
    }
    config.setValue(QStringLiteral("filter/platforms"), platforms);
    config.setValue(QStringLiteral("lyrics/filterLeadingCredits"), m_filterCredits);
    config.setValue(QStringLiteral("providers/netease/baseUrl"), m_neteaseBaseUrl);
    config.setValue(QStringLiteral("providers/netease/timeoutMs"), m_networkTimeoutMs);
    config.setValue(QStringLiteral("providers/order"), m_providerOrder);
    config.setValue(QStringLiteral("providers/enabled"), m_enabledProviders);
    config.setValue(QStringLiteral("providers/local/directory"), m_localLyricsDirectory);
    config.setValue(QStringLiteral("providers/amll/indexUrl"), m_amllIndexUrl);
    config.setValue(QStringLiteral("providers/amll/contentBaseUrl"), m_amllContentBaseUrl);
    config.setValue(QStringLiteral("providers/amll/timeoutMs"), m_amllTimeoutMs);
    config.setValue(QStringLiteral("providers/amll/indexRefreshHours"), m_amllIndexRefreshHours);
    config.setValue(QStringLiteral("logging/fileEnabled"), m_fileLoggingEnabled);
    config.setValue(QStringLiteral("logging/filePath"), m_logFilePath);
    config.setValue(QStringLiteral("logging/debug"), m_debugLoggingEnabled);
    config.sync();
    if (config.status() != QSettings::NoError) {
        return false;
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
}
