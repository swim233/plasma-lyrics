// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "provider.h"

namespace Lyrics
{

// Reads .lrc files and overrides/ from a directory. Waylyrics users migrate
// lyrics here (tools/import-waylyrics); its per-track files are the dirty
// shapes core/ must survive offline.
class LocalProvider : public Provider
{
public:
    explicit LocalProvider(QString baseDir);

    QString id() const override { return QStringLiteral("local"); }
    bool isConfigured() const override; // false without a reachable baseDir

private:
    QString m_baseDir;
};

} // namespace Lyrics
