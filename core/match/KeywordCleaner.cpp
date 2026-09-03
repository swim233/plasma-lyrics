// SPDX-License-Identifier: GPL-2.0-or-later

#include "match/KeywordCleaner.h"

namespace Lyrics
{

QStringList cleanArtists(const QStringList &raw)
{
    QStringList out;
    for (const QString &entry : raw) {
        const QStringList parts = entry.split(QLatin1Char('/'));
        for (const QString &part : parts) {
            const QString trimmed = part.trimmed();
            if (!trimmed.isEmpty()) {
                out << trimmed;
            }
        }
    }
    return out;
}

} // namespace Lyrics
