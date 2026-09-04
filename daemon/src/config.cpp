#include "config.h"

#include <QStandardPaths>
#include <QUrl>

namespace PlasmaLyrics {

Config::Config()
    : m_settings(QSettings::IniFormat, QSettings::UserScope,
                 QStringLiteral("plasma-lyrics"), QStringLiteral("plasma-lyricsd"))
{
}

PolicyConfig Config::policy() const
{
    PolicyConfig config;
    config.serviceBlacklist = m_settings.value(
        QStringLiteral("players/blacklist"),
        QStringList{QStringLiteral("org.mpris.MediaPlayer2.kdeconnect.*")}).toStringList();
    config.musicUrlPrefixes = m_settings.value(
        QStringLiteral("filter/musicUrlPrefixes"),
        QStringList{QStringLiteral("https://music.163.com/"), QStringLiteral("http://music.163.com/")}).toStringList();
    config.useMetadataHeuristic = m_settings.value(QStringLiteral("filter/metadataHeuristic"), true).toBool();
    return config;
}

QUrl Config::neteaseBaseUrl() const
{
    return QUrl(m_settings.value(QStringLiteral("providers/netease/baseUrl"),
                                 QStringLiteral("https://music.163.com")).toString());
}

int Config::networkTimeoutMs() const
{
    return m_settings.value(QStringLiteral("providers/netease/timeoutMs"), 8000).toInt();
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

bool Config::filterCredits() const
{
    return m_settings.value(QStringLiteral("lyrics/filterLeadingCredits"), true).toBool();
}

} // namespace PlasmaLyrics
