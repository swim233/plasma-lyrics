#pragma once

#include "core/lyric/lyricmodel.h"
#include "core/store/lyricstore.h"
#include "daemon/src/mpris/mpristypes.h"

#include <QString>
#include <QStringList>

namespace PlasmaLyrics {

struct ResolvedLyric {
    QString state = QStringLiteral("searching");
    std::optional<TrackRef> ref;
    LyricDocument document;
    // Empty means automatic/global ordering.  effectivePreferredProvider is
    // the first configured source after applying that ordering.
    QString preferredProvider;
    QString effectivePreferredProvider;
    bool temporaryFallback = false;
    QStringList availableProviders;
    // Snapshot consumers use the daemon-computed effective offset in
    // document.offsetMs instead of opening the store; the flag says which
    // mode produced it.
    bool globalOffsetEnabled = false;
    // This ref's own offset, which the effective one includes; 0 without a
    // ref. Filled in by applyStoredOffsets().
    int trackOffsetMs = 0;
    // Non-empty while a forced resolve is replacing/confirming this source.
    QString switchingProvider;
};

// Fills in the offsets a snapshot carries from the store (DESIGN.md
// decision 79): trackOffsetMs is the ref's own offset, and
// document.offsetMs the effective one, which adds the global offset while
// that is enabled.
void applyStoredOffsets(ResolvedLyric &lyric, const LyricStore &store);

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
