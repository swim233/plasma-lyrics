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
    config.enabledPlatforms = m_settings.value(
        QStringLiteral("filter/platforms"),
        QStringList{QStringLiteral("netease"), QStringLiteral("apple")}).toStringList();
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

QStringList Config::providerOrder() const
{
    QStringList result;
    const auto configured = m_settings.value(
        QStringLiteral("providers/order"),
        QStringList{QStringLiteral("netease"), QStringLiteral("amll")}).toStringList();
    for (const auto &provider : configured) {
        const QString id = provider.trimmed().toCaseFolded();
        if ((id == QStringLiteral("netease") || id == QStringLiteral("amll"))
            && !result.contains(id)) {
            result.append(id);
        }
    }
    return result;
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

bool Config::filterCredits() const
{
    return m_settings.value(QStringLiteral("lyrics/filterLeadingCredits"), true).toBool();
}

} // namespace PlasmaLyrics
