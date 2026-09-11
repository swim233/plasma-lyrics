#include "config.h"
#include "logging.h"

#include <QStandardPaths>
#include <QDebug>
#include <QUrl>

namespace PlasmaLyrics {
namespace {

const QStringList &builtInProviderOrder()
{
    static const QStringList order{QStringLiteral("local"), QStringLiteral("netease"),
                                   QStringLiteral("amll")};
    return order;
}

} // namespace

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
