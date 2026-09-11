#pragma once

#include "daemon/src/mpris/mprispolicy.h"

#include <QSettings>
#include <QString>
#include <QStringList>

namespace PlasmaLyrics {

class Config
{
public:
    Config();

    PolicyConfig policy() const;
    QUrl neteaseBaseUrl() const;
    int networkTimeoutMs() const;
    // The persisted priority list is independent from the enabled subset so
    // disabling a provider never erases its position from configuration UI.
    QStringList providerOrder() const;
    QStringList enabledProviderOrder() const;
    QString localLyricsDirectory() const;
    QUrl amllIndexUrl() const;
    QUrl amllContentBaseUrl() const;
    int amllTimeoutMs() const;
    qint64 amllIndexMaxAgeSeconds() const;
    bool fileLoggingEnabled() const;
    QString logFilePath() const;
    bool debugLoggingEnabled() const;
    bool filterCredits() const;
    // "none", "system" or "manual"; an unknown stored value reads back as
    // "none" (a qCWarning is logged when that happens).
    QString proxyMode() const;
    QString proxyUrl() const;

private:
    mutable QSettings m_settings;
};

} // namespace PlasmaLyrics
