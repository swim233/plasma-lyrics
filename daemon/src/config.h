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
    QStringList providerOrder() const;
    QString localLyricsDirectory() const;
    QUrl amllIndexUrl() const;
    QUrl amllContentBaseUrl() const;
    int amllTimeoutMs() const;
    qint64 amllIndexMaxAgeSeconds() const;
    bool fileLoggingEnabled() const;
    QString logFilePath() const;
    bool filterCredits() const;

private:
    mutable QSettings m_settings;
};

} // namespace PlasmaLyrics
