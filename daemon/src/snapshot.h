#pragma once

#include "core/lyric/lyricmodel.h"
#include "core/store/lyricstore.h"
#include "daemon/src/mpris/mpristypes.h"

#include <QString>

namespace PlasmaLyrics {

struct ResolvedLyric {
    QString state = QStringLiteral("searching");
    std::optional<TrackRef> ref;
    LyricDocument document;
};

class SnapshotWriter
{
public:
    explicit SnapshotWriter(QString path = {});
    bool write(const std::optional<MprisState> &player, const ResolvedLyric &lyric, QString *error = nullptr);
    quint64 sequence() const;
    QString path() const;
    static QString defaultPath();

private:
    void restoreSequence();

    QString m_path;
    quint64 m_sequence = 0;
};

} // namespace PlasmaLyrics

