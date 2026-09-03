// SPDX-License-Identifier: GPL-2.0-or-later

#include "local/LocalProvider.h"

#include <QDir>

namespace Lyrics
{

LocalProvider::LocalProvider(QString baseDir)
    : m_baseDir(std::move(baseDir))
{
}

bool LocalProvider::isConfigured() const
{
    return QDir(m_baseDir).exists();
}

} // namespace Lyrics
