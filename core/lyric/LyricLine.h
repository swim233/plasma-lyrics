// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QVector>
#include <QVariant>

namespace Lyrics
{
struct LyricLine
{
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    QString translation;
    // Per-character timing. Always present in the model from day one (DESIGN
    // #13) even though no reachable provider returns it (DESIGN §1.2); null
    // until a weapi/eapi implementation lands.
    QVector<QVector<QVariant>> words;

    bool operator==(const LyricLine &) const = default;
};

using LyricLines = QList<LyricLine>;
} // namespace Lyrics

Q_DECLARE_METATYPE(Lyrics::LyricLine)
