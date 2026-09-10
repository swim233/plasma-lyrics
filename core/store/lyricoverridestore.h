#pragma once

#include "core/lyric/lyricmodel.h"
#include "core/store/lyricstore.h"

#include <QString>
#include <optional>

namespace PlasmaLyrics {

// User-authored, provider-result-specific LRC replacements. This is storage,
// not a searchable provider: the key is the source TrackRef being replaced.
class LyricOverrideStore
{
public:
    explicit LyricOverrideStore(QString directory = {});

    std::optional<LyricDocument> lyric(const TrackRef &ref) const;
    QString directory() const;
    static QString defaultDirectory();

private:
    QString m_directory;
};

} // namespace PlasmaLyrics
