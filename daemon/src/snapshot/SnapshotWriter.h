// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QList>
#include <QString>

class QJsonObject;

namespace Lyrics
{

struct LyricLine
{
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
};

} // namespace Lyrics
