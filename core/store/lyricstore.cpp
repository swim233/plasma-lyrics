#include "lyricstore.h"

#include "core/lyric/lyricmodel.h"

#include <algorithm>

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
        QStringLiteral("CREATE TABLE IF NOT EXISTS lyric (provider TEXT NOT NULL, track_id TEXT NOT NULL, fetched_at INTEGER NOT NULL, origin TEXT, translation TEXT, has_words INTEGER NOT NULL DEFAULT 0, metadata TEXT, PRIMARY KEY(provider, track_id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS fingerprint (fingerprint TEXT PRIMARY KEY, provider TEXT NOT NULL, track_id TEXT NOT NULL, matched_at INTEGER NOT NULL, score REAL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS miss (fingerprint TEXT PRIMARY KEY, tried_at INTEGER NOT NULL, reason TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS offset (provider TEXT NOT NULL, track_id TEXT NOT NULL, offset_ms INTEGER NOT NULL, PRIMARY KEY(provider, track_id))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS setting (name TEXT PRIMARY KEY, value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS track_preference (fingerprint TEXT PRIMARY KEY, provider TEXT NOT NULL, updated_at INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS provider_fingerprint (fingerprint TEXT NOT NULL, provider TEXT NOT NULL, track_id TEXT NOT NULL, matched_at INTEGER NOT NULL, score REAL, PRIMARY KEY(fingerprint, provider))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS provider_miss (fingerprint TEXT NOT NULL, provider TEXT NOT NULL, tried_at INTEGER NOT NULL, reason TEXT NOT NULL, cache_version TEXT NOT NULL DEFAULT '', PRIMARY KEY(fingerprint, provider))")};
    QSqlQuery begin(m_database);
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        if (error) *error = begin.lastError().text();
        return false;
    }
    auto fail = [&](const QString &message) {
        m_database.rollback();
        if (error) *error = message;
        return false;
    };
    for (const auto &statement : statements) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            return fail(query.lastError().text());
        }
    }
    if (!hasColumn(QStringLiteral("lyric"), QStringLiteral("metadata"))) {
        QSqlQuery alter(m_database);
        if (!alter.exec(QStringLiteral("ALTER TABLE lyric ADD COLUMN metadata TEXT"))) {
            return fail(alter.lastError().text());
        }
    }
    // Old fingerprint rows are successful actual results, not manual user
    // choices.  Copy them into the provider-scoped result map while leaving
    // track_preference empty.  Old global misses are intentionally not
    // copied: they cannot prove that a newly added provider was attempted.
    QSqlQuery migrate(m_database);
    if (!migrate.exec(QStringLiteral(
            "INSERT OR IGNORE INTO provider_fingerprint(fingerprint, provider, track_id, matched_at, score) "
            "SELECT fingerprint, provider, track_id, matched_at, score FROM fingerprint"))) {
        return fail(migrate.lastError().text());
    }
    QSqlQuery version(m_database);
    if (!version.exec(QStringLiteral("PRAGMA user_version=2"))) {
        return fail(version.lastError().text());
    }
    QSqlQuery commit(m_database);
    if (!commit.exec(QStringLiteral("COMMIT"))) {
        return fail(commit.lastError().text());
    }
    return true;
}

bool LyricStore::hasColumn(const QString &table, const QString &column) const
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == column) return true;
    }
    return false;
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
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO lyric(provider, track_id, fetched_at, origin, translation, has_words, metadata) VALUES(?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(ref.provider);
    query.addBindValue(ref.trackId);
    query.addBindValue(epochSeconds(fetchedAt));
    query.addBindValue(QString::fromUtf8(QJsonDocument(lines).toJson(QJsonDocument::Compact)));
    query.addBindValue(QString::fromUtf8(QJsonDocument(translations).toJson(QJsonDocument::Compact)));
    query.addBindValue(document.hasWords ? 1 : 0);
    query.addBindValue(QString::fromUtf8(QJsonDocument(document.metadata).toJson(QJsonDocument::Compact)));
    return query.exec();
}

std::optional<LyricDocument> LyricStore::lyric(const TrackRef &ref) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT origin, has_words, metadata FROM lyric WHERE provider=? AND track_id=?"));
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
    document.metadata = QJsonDocument::fromJson(query.value(2).toString().toUtf8()).object();
    document.offsetMs = offset(ref);
    return document;
}

bool LyricStore::mapFingerprint(const QString &fingerprint, const TrackRef &ref, qint64 matchedAt)
{
    QSqlQuery begin(m_database);
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) return false;
    const qint64 timestamp = epochSeconds(matchedAt);
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO fingerprint(fingerprint, provider, track_id, matched_at, score) VALUES(?, ?, ?, ?, ?)"));
    query.addBindValue(fingerprint);
    query.addBindValue(ref.provider);
    query.addBindValue(ref.trackId);
    query.addBindValue(timestamp);
    query.addBindValue(ref.score);
    if (!query.exec()) {
        m_database.rollback();
        return false;
    }
    QSqlQuery providerQuery(m_database);
    providerQuery.prepare(QStringLiteral("INSERT OR REPLACE INTO provider_fingerprint(fingerprint, provider, track_id, matched_at, score) VALUES(?, ?, ?, ?, ?)"));
    providerQuery.addBindValue(fingerprint);
    providerQuery.addBindValue(ref.provider);
    providerQuery.addBindValue(ref.trackId);
    providerQuery.addBindValue(timestamp);
    providerQuery.addBindValue(ref.score);
    if (!providerQuery.exec()) {
        m_database.rollback();
        return false;
    }
    QSqlQuery commit(m_database);
    if (!commit.exec(QStringLiteral("COMMIT"))) {
        m_database.rollback();
        return false;
    }
    return true;
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

bool LyricStore::mapProviderFingerprint(const QString &fingerprint, const TrackRef &ref,
                                        qint64 matchedAt)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO provider_fingerprint(fingerprint, provider, track_id, matched_at, score) VALUES(?, ?, ?, ?, ?)"));
    query.addBindValue(fingerprint);
    query.addBindValue(ref.provider);
    query.addBindValue(ref.trackId);
    query.addBindValue(epochSeconds(matchedAt));
    query.addBindValue(ref.score);
    return query.exec();
}

std::optional<TrackRef> LyricStore::refForProvider(const QString &fingerprint,
                                                   const QString &provider) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT provider, track_id, score FROM provider_fingerprint WHERE fingerprint=? AND provider=?"));
    query.addBindValue(fingerprint);
    query.addBindValue(provider);
    if (!query.exec() || !query.next()) return std::nullopt;
    return TrackRef{query.value(0).toString(), query.value(1).toString(), query.value(2).toDouble()};
}

bool LyricStore::setPreferredProvider(const QString &fingerprint, const QString &provider,
                                      qint64 updatedAt)
{
    if (fingerprint.isEmpty() || provider.isEmpty()) return false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO track_preference(fingerprint, provider, updated_at) VALUES(?, ?, ?)"));
    query.addBindValue(fingerprint);
    query.addBindValue(provider);
    query.addBindValue(epochSeconds(updatedAt));
    return query.exec();
}

bool LyricStore::clearPreferredProvider(const QString &fingerprint)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM track_preference WHERE fingerprint=?"));
    query.addBindValue(fingerprint);
    return query.exec();
}

std::optional<QString> LyricStore::preferredProvider(const QString &fingerprint) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT provider FROM track_preference WHERE fingerprint=?"));
    query.addBindValue(fingerprint);
    if (!query.exec() || !query.next()) return std::nullopt;
    return query.value(0).toString();
}

bool LyricStore::recordProviderMiss(const QString &fingerprint, const QString &provider,
                                    const QString &reason, const QString &cacheVersion,
                                    qint64 triedAt)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO provider_miss(fingerprint, provider, tried_at, reason, cache_version) VALUES(?, ?, ?, ?, ?)"));
    query.addBindValue(fingerprint);
    query.addBindValue(provider);
    query.addBindValue(epochSeconds(triedAt));
    query.addBindValue(reason);
    query.addBindValue(cacheVersion);
    return query.exec();
}

std::optional<MissRecord> LyricStore::freshProviderMiss(const QString &fingerprint,
                                                        const QString &provider,
                                                        const QString &cacheVersion,
                                                        qint64 now,
                                                        qint64 ttlSeconds) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT reason, tried_at, cache_version FROM provider_miss WHERE fingerprint=? AND provider=?"));
    query.addBindValue(fingerprint);
    query.addBindValue(provider);
    if (!query.exec() || !query.next()) return std::nullopt;
    const MissRecord miss{query.value(0).toString(), query.value(1).toLongLong(),
                          query.value(2).toString()};
    if (miss.cacheVersion != cacheVersion || epochSeconds(now) - miss.triedAt >= ttlSeconds) {
        return std::nullopt;
    }
    return miss;
}

bool LyricStore::clearProviderMiss(const QString &fingerprint, const QString &provider)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM provider_miss WHERE fingerprint=? AND provider=?"));
    query.addBindValue(fingerprint);
    query.addBindValue(provider);
    return query.exec();
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

std::optional<MissRecord> LyricStore::freshMiss(const QString &fingerprint, qint64 now,
                                                qint64 ttlSeconds) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT reason, tried_at FROM miss WHERE fingerprint=?"));
    query.addBindValue(fingerprint);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    const MissRecord miss{query.value(0).toString(), query.value(1).toLongLong()};
    return epochSeconds(now) - miss.triedAt < ttlSeconds
        ? std::optional<MissRecord>(miss)
        : std::nullopt;
}

bool LyricStore::hasFreshMiss(const QString &fingerprint, qint64 now, qint64 ttlSeconds) const
{
    return freshMiss(fingerprint, now, ttlSeconds).has_value();
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

std::optional<QString> LyricStore::setting(const QString &key) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT value FROM setting WHERE name=?"));
    query.addBindValue(key);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return query.value(0).toString();
}

bool LyricStore::setSetting(const QString &key, const QString &value)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO setting(name, value) VALUES(?, ?)"));
    query.addBindValue(key);
    query.addBindValue(value);
    return query.exec();
}

bool LyricStore::globalOffsetEnabled() const
{
    const auto value = setting(QStringLiteral("globalOffsetEnabled"));
    return value.has_value() && value->toInt() != 0;
}

bool LyricStore::setGlobalOffsetEnabled(bool enabled)
{
    return setSetting(QStringLiteral("globalOffsetEnabled"), QString::number(enabled ? 1 : 0));
}

int LyricStore::globalOffsetMs() const
{
    const auto value = setting(QStringLiteral("globalOffsetMs"));
    const qint64 stored = value.has_value() ? value->toLongLong() : 0;
    return static_cast<int>(std::clamp<qint64>(stored, -maximumGlobalOffsetMs(), maximumGlobalOffsetMs()));
}

bool LyricStore::setGlobalOffsetMs(int offsetMs)
{
    const int clamped = static_cast<int>(std::clamp<qint64>(offsetMs, -maximumGlobalOffsetMs(), maximumGlobalOffsetMs()));
    return setSetting(QStringLiteral("globalOffsetMs"), QString::number(clamped));
}

std::optional<int> LyricStore::adjustGlobalOffset(int deltaMs)
{
    QSqlQuery begin(m_database);
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        return std::nullopt;
    }
    const qint64 sum = static_cast<qint64>(globalOffsetMs()) + static_cast<qint64>(deltaMs);
    const int adjusted = static_cast<int>(std::clamp<qint64>(sum, -maximumGlobalOffsetMs(), maximumGlobalOffsetMs()));
    if (!setGlobalOffsetMs(adjusted)) {
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

} // namespace PlasmaLyrics
