#include "backendconfig.h"

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
GETTER(bool, fileLoggingEnabled, m_fileLoggingEnabled)
GETTER(QString, logFilePath, m_logFilePath)
GETTER(bool, dirty, m_dirty)
GETTER(BackendConfig::RestartState, restartState, m_restartState)
GETTER(bool, restartInProgress, m_restartInProgress)
GETTER(QString, restartError, m_restartError)
#undef GETTER

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
SETTER(bool, setFileLoggingEnabled, m_fileLoggingEnabled)
SETTER(const QString &, setLogFilePath, m_logFilePath)
#undef SETTER

void BackendConfig::load()
{
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
    m_fileLoggingEnabled = config.value(QStringLiteral("logging/fileEnabled"), false).toBool();
    m_logFilePath = config.value(
        QStringLiteral("logging/filePath"),
        QString(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/plasma-lyrics/plasma-lyricsd.log"))).toString();
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
    config.setValue(QStringLiteral("logging/fileEnabled"), m_fileLoggingEnabled);
    config.setValue(QStringLiteral("logging/filePath"), m_logFilePath);
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
