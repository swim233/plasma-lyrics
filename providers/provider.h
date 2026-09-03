// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace Lyrics
{

struct TrackQuery
{
    QString title;
    QStringList artists;
    QString album;
    qint64 lengthMs = 0;
};

struct Candidate
{
    QString trackId;
    QString title;
    QStringList artists;
    QString album;
    qint64 lengthMs = 0;
};

// One provider per CMake option (DESIGN #23). isConfigured() is not ceremony:
// waylyrics' QQ music provider needs a user-run bridge service, so the
// interface cannot assume stateless direct connections from day one. Every
// provider carries its own config block (base URL / cookie / timeout).
class Provider
{
public:
    virtual ~Provider() = default;
    virtual QString id() const = 0;        // "netease" | "lrclib" | "local"
    virtual bool isConfigured() const = 0; // unconfigured providers are skipped
};

} // namespace Lyrics

Q_DECLARE_INTERFACE(Lyrics::Provider, "io.github.swim233.Lyrics.Provider")

