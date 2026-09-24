#pragma once

#include "core/lyric/lyricmodel.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <optional>

namespace PlasmaLyrics {

struct TrackRef {
    QString provider;
    QString trackId;
    double score = 0;
};

struct MissRecord {
    QString reason;
    qint64 triedAt = 0;
    QString cacheVersion;
};

/// What LyricStore::purgeWordLevelLyricsOnce() removes. A struct rather than
/// three adjacent QString parameters so that a call site can name each value
/// with a designator, as every current one does. That makes a mix-up visible,
/// not impossible: positional initialization still compiles, and so does a
/// designated one with two values swapped (DESIGN.md decision 72).
struct WordLevelPurge {
    /// The setting row that records the purge as done.
    QString marker;
    QString provider;
    /// The reason the resolver records for a fetched lyric with nothing to
    /// show. The store does not own that vocabulary, so it is handed in.
    QString emptyMissReason;
};

struct WordLevelPurgeResult {
    /// The marker was already there and nothing was touched.
    bool alreadyDone = false;
    int lyrics = 0;
    int misses = 0;
};

class LyricStore
{
public:
    explicit LyricStore(QString path = {});
    ~LyricStore();

    bool open(QString *error = nullptr);
    QString path() const;

    bool putLyric(const TrackRef &ref, const LyricDocument &document, qint64 fetchedAt = 0);
    std::optional<LyricDocument> lyric(const TrackRef &ref) const;
    bool mapFingerprint(const QString &fingerprint, const TrackRef &ref, qint64 matchedAt = 0);
    std::optional<TrackRef> refForFingerprint(const QString &fingerprint) const;
    bool mapProviderFingerprint(const QString &fingerprint, const TrackRef &ref,
                                qint64 matchedAt = 0);
    std::optional<TrackRef> refForProvider(const QString &fingerprint,
                                           const QString &provider) const;
    bool setPreferredProvider(const QString &fingerprint, const QString &provider,
                              qint64 updatedAt = 0);
    bool clearPreferredProvider(const QString &fingerprint);
    std::optional<QString> preferredProvider(const QString &fingerprint) const;
    bool recordProviderMiss(const QString &fingerprint, const QString &provider,
                            const QString &reason, const QString &cacheVersion = {},
                            qint64 triedAt = 0);
    std::optional<MissRecord> freshProviderMiss(const QString &fingerprint,
                                                const QString &provider,
                                                const QString &cacheVersion,
                                                qint64 now = 0,
                                                qint64 ttlSeconds = 7 * 24 * 60 * 60) const;
    bool clearProviderMiss(const QString &fingerprint, const QString &provider);
    bool recordMiss(const QString &fingerprint, const QString &reason, qint64 triedAt = 0);
    std::optional<MissRecord> freshMiss(const QString &fingerprint, qint64 now = 0,
                                        qint64 ttlSeconds = 7 * 24 * 60 * 60) const;
    bool hasFreshMiss(const QString &fingerprint, qint64 now = 0, qint64 ttlSeconds = 7 * 24 * 60 * 60) const;
    bool setOffset(const TrackRef &ref, int offsetMs);
    std::optional<int> adjustOffset(const TrackRef &ref, int deltaMs);
    int offset(const TrackRef &ref) const;

    bool globalOffsetEnabled() const;
    bool setGlobalOffsetEnabled(bool enabled);
    int globalOffsetMs() const;
    bool setGlobalOffsetMs(int offsetMs);
    std::optional<int> adjustGlobalOffset(int deltaMs);
    static constexpr int maximumGlobalOffsetMs() { return 10000; }

    /// Once per marker, drops every word-level lyric row of one provider and
    /// the negative-cache rows that would keep those tracks from being
    /// fetched again, so lyrics parsed by an older, faulty parser are
    /// replaced. Mappings, offsets and preferences are kept. Returns nullopt
    /// on failure, with nothing changed.
    std::optional<WordLevelPurgeResult> purgeWordLevelLyricsOnce(const WordLevelPurge &purge,
                                                                 QString *error = nullptr);

    static QString defaultPath();

private:
    bool executeSchema(QString *error);
    bool hasColumn(const QString &table, const QString &column) const;
    static qint64 epochSeconds(qint64 supplied);
    std::optional<QString> setting(const QString &key) const;
    bool setSetting(const QString &key, const QString &value);

    QString m_path;
    QString m_connectionName;
    QSqlDatabase m_database;
};

} // namespace PlasmaLyrics
