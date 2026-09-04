#pragma once

#include "daemon/src/mpris/mprispolicy.h"

#include <QSettings>
#include <QString>

namespace PlasmaLyrics {

class Config
{
public:
    Config();

    PolicyConfig policy() const;
    QUrl neteaseBaseUrl() const;
    int networkTimeoutMs() const;
    bool fileLoggingEnabled() const;
    QString logFilePath() const;
    bool filterCredits() const;

private:
    mutable QSettings m_settings;
};

} // namespace PlasmaLyrics

