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
    bool recordMiss(const QString &fingerprint, const QString &reason, qint64 triedAt = 0);
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

    static QString defaultPath();

private:
    bool executeSchema(QString *error);
    static qint64 epochSeconds(qint64 supplied);
    std::optional<QString> setting(const QString &key) const;
    bool setSetting(const QString &key, const QString &value);

    QString m_path;
    QString m_connectionName;
    QSqlDatabase m_database;
};

} // namespace PlasmaLyrics
