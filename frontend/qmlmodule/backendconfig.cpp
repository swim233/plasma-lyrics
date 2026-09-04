#include "backendconfig.h"

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
    : QObject(parent)
{
    load();
}

#define GETTER(type, name, member) type BackendConfig::name() const { return member; }
GETTER(QString, serviceBlacklist, m_serviceBlacklist)
GETTER(QString, musicUrlPrefixes, m_musicUrlPrefixes)
GETTER(bool, metadataHeuristic, m_metadataHeuristic)
GETTER(bool, filterCredits, m_filterCredits)
GETTER(QString, neteaseBaseUrl, m_neteaseBaseUrl)
GETTER(int, networkTimeoutMs, m_networkTimeoutMs)
GETTER(bool, fileLoggingEnabled, m_fileLoggingEnabled)
GETTER(QString, logFilePath, m_logFilePath)
GETTER(bool, dirty, m_dirty)
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
    m_filterCredits = config.value(QStringLiteral("lyrics/filterLeadingCredits"), true).toBool();
    m_neteaseBaseUrl = config.value(QStringLiteral("providers/netease/baseUrl"),
                                    QStringLiteral("https://music.163.com")).toString();
    m_networkTimeoutMs = config.value(QStringLiteral("providers/netease/timeoutMs"), 8000).toInt();
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
