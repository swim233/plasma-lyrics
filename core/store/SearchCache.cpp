// SPDX-License-Identifier: GPL-2.0-or-later

#include "store/SearchCache.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace Lyrics
{

namespace
{

bool ensureTables(const QSqlDatabase &db)
{
    QSqlQuery q(db);
    return q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS search_cache ("
        "fingerprint TEXT PRIMARY KEY,"
        "track_id TEXT NOT NULL,"
        "offset_ms INTEGER NOT NULL,"
        "cached_at INTEGER NOT NULL)"));
}

} // namespace

SearchCache::SearchCache(const QString &dbPath)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    db.setDatabaseName(dbPath);
    db.open();
    ensureTables(db);
}

CacheHit SearchCache::lookup(const QString &fingerprint) const
{
    CacheHit hit;
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "SELECT track_id, offset_ms FROM search_cache WHERE fingerprint = :fp"));
    q.bindValue(QStringLiteral(":fp"), fingerprint);
    if (q.exec() && q.next()) {
        hit.found = true;
        hit.trackId = q.value(0).toString();
        hit.offsetMs = q.value(1).toLongLong();
    }
    return hit;
}

void SearchCache::store(const QString &fingerprint, const QString &trackId, qint64 offsetMs)
{
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO search_cache (fingerprint, track_id, offset_ms, cached_at)"
        " VALUES (:fp, :tid, :off, :now)"));
    q.bindValue(QStringLiteral(":fp"), fingerprint);
    q.bindValue(QStringLiteral(":tid"), trackId);
    q.bindValue(QStringLiteral(":off"), offsetMs);
    q.bindValue(QStringLiteral(":now"), QDateTime::currentMSecsSinceEpoch());
    q.exec();
}

void SearchCache::expirePastTtl(const QString &fingerprint)
{
    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral("DELETE FROM search_cache WHERE fingerprint = :fp"));
    q.bindValue(QStringLiteral(":fp"), fingerprint);
    q.exec();
}

QString fingerprint(const TrackQuery &query)
{
    return query.title + QLatin1Char('|') + query.artists.join(QLatin1Char(','))
        + QLatin1Char('|') + QString::number(query.lengthMs);
}

} // namespace Lyrics
