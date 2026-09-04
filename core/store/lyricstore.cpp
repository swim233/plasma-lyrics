#include "lyricstore.h"

#include "core/lyric/lyricmodel.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace PlasmaLyrics {

LyricStore::LyricStore(QString path)
    : m_path(path.isEmpty() ? defaultPath() : std::move(path))
    , m_connectionName(QStringLiteral("plasma-lyrics-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

LyricStore::~LyricStore()
{
    if (m_database.isValid()) {
        m_database.close();
        m_database = {};
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

QString LyricStore::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasma-lyrics/lyrics.db");
}

bool LyricStore::open(QString *error)
{
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        if (error) {
            *error = QStringLiteral("cannot create database directory");
        }
        return false;
    }
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_path);
    if (!m_database.open()) {
        if (error) {
            *error = m_database.lastError().text();
        }
        return false;
    }
    return executeSchema(error);
}

QString LyricStore::path() const
{
    return m_path;
}

bool LyricStore::executeSchema(QString *error)
{
    static const QStringList statements{
        QStringLiteral("CREATE TABLE IF NOT EXISTS lyric (provider TEXT NOT NULL, track_id TEXT NOT NULL, fetched_at INTEGER NOT NULL, origin TEXT, translation TEXT, has_words INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(provider, track_id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS fingerprint (fingerprint TEXT PRIMARY KEY, provider TEXT NOT NULL, track_id TEXT NOT NULL, matched_at INTEGER NOT NULL, score REAL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS miss (fingerprint TEXT PRIMARY KEY, tried_at INTEGER NOT NULL, reason TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS offset (provider TEXT NOT NULL, track_id TEXT NOT NULL, offset_ms INTEGER NOT NULL, PRIMARY KEY(provider, track_id))")};
    for (const auto &statement : statements) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
    }
    return true;
}

qint64 LyricStore::epochSeconds(qint64 supplied)
{
    return supplied == 0 ? QDateTime::currentSecsSinceEpoch() : supplied;
}

bool LyricStore::putLyric(const TrackRef &ref, const LyricDocument &document, qint64 fetchedAt)
{
    QJsonArray lines;
    QJsonArray translations;
    for (const auto &line : document.lines) {
        lines.append(lineToJson(line));
        if (line.translation) {
            translations.append(QJsonObject{{QStringLiteral("startMs"), line.startMs},
                                            {QStringLiteral("text"), *line.translation}});
        }
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO lyric(provider, track_id, fetched_at, origin, translation, has_words) VALUES(?, ?, ?, ?, ?, ?)"));
    query.addBindValue(ref.provider);
    query.addBindValue(ref.trackId);
    query.addBindValue(epochSeconds(fetchedAt));
    query.addBindValue(QString::fromUtf8(QJsonDocument(lines).toJson(QJsonDocument::Compact)));
    query.addBindValue(QString::fromUtf8(QJsonDocument(translations).toJson(QJsonDocument::Compact)));
    query.addBindValue(document.hasWords ? 1 : 0);
    return query.exec();
}

std::optional<LyricDocument> LyricStore::lyric(const TrackRef &ref) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT origin, has_words FROM lyric WHERE provider=? AND track_id=?"));
    query.addBindValue(ref.provider);
    query.addBindValue(ref.trackId);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    LyricDocument document;
    const auto lines = QJsonDocument::fromJson(query.value(0).toString().toUtf8()).array();
    for (const auto &value : lines) {
        if (const auto line = lineFromJson(value.toObject())) {
            document.lines.append(*line);
        }
    }
    document.hasWords = query.value(1).toBool();
    document.offsetMs = offset(ref);
    return document;
}

bool LyricStore::mapFingerprint(const QString &fingerprint, const TrackRef &ref, qint64 matchedAt)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO fingerprint(fingerprint, provider, track_id, matched_at, score) VALUES(?, ?, ?, ?, ?)"));
    query.addBindValue(fingerprint);
    query.addBindValue(ref.provider);
    query.addBindValue(ref.trackId);
    query.addBindValue(epochSeconds(matchedAt));
    query.addBindValue(ref.score);
    return query.exec();
}

std::optional<TrackRef> LyricStore::refForFingerprint(const QString &fingerprint) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT provider, track_id, score FROM fingerprint WHERE fingerprint=?"));
    query.addBindValue(fingerprint);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return TrackRef{query.value(0).toString(), query.value(1).toString(), query.value(2).toDouble()};
}

bool LyricStore::recordMiss(const QString &fingerprint, const QString &reason, qint64 triedAt)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO miss(fingerprint, tried_at, reason) VALUES(?, ?, ?)"));
    query.addBindValue(fingerprint);
    query.addBindValue(epochSeconds(triedAt));
    query.addBindValue(reason);
    return query.exec();
}

bool LyricStore::hasFreshMiss(const QString &fingerprint, qint64 now, qint64 ttlSeconds) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT tried_at FROM miss WHERE fingerprint=?"));
    query.addBindValue(fingerprint);
    if (!query.exec() || !query.next()) {
        return false;
    }
    return epochSeconds(now) - query.value(0).toLongLong() < ttlSeconds;
}

bool LyricStore::setOffset(const TrackRef &ref, int offsetMs)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO offset(provider, track_id, offset_ms) VALUES(?, ?, ?)"));
    query.addBindValue(ref.provider);
    query.addBindValue(ref.trackId);
    query.addBindValue(offsetMs);
    return query.exec();
}

std::optional<int> LyricStore::adjustOffset(const TrackRef &ref, int deltaMs)
{
    QSqlQuery begin(m_database);
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        return std::nullopt;
    }
    const int adjusted = offset(ref) + deltaMs;
    if (!setOffset(ref, adjusted)) {
        m_database.rollback();
        return std::nullopt;
    }
    QSqlQuery commit(m_database);
    if (!commit.exec(QStringLiteral("COMMIT"))) {
        m_database.rollback();
        return std::nullopt;
    }
    return adjusted;
}

int LyricStore::offset(const TrackRef &ref) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT offset_ms FROM offset WHERE provider=? AND track_id=?"));
    query.addBindValue(ref.provider);
    query.addBindValue(ref.trackId);
    return query.exec() && query.next() ? query.value(0).toInt() : 0;
}

} // namespace PlasmaLyrics
