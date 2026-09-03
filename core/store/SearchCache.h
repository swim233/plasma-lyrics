// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QStringList>

namespace Lyrics
{

// A search query (DESIGN #12). The real payload is recorded in
// providers/tests/fixtures/netease/search-mofachuniang.json.
struct TrackQuery
{
    QString title;
    QStringList artists;
    qint64 lengthMs = 0;
};

// A cached search hit: the lyric-vs-audio offset recorded at search time,
// re-applied on replay.
struct CacheHit
{
    bool found = false;
    QString trackId;
    qint64 offsetMs = 0;
};

// SQLite search cache (DESIGN #14): results are cached by fingerprint; a
// miss past the TTL is re-searched, not served stale.
class SearchCache
{
public:
    explicit SearchCache(const QString &dbPath);

    CacheHit lookup(const QString &fingerprint) const;
    void store(const QString &fingerprint, const QString &trackId, qint64 offsetMs);
    void expirePastTtl(const QString &fingerprint);

private:
    QString m_dbPath;
};

// Deterministic fingerprint of a search (title + artist join + length).
QString fingerprint(const TrackQuery &query);

} // namespace Lyrics
