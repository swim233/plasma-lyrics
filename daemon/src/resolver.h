#pragma once

#include "daemon/src/snapshot.h"
#include "providers/provider.h"

#include <QList>
#include <memory>

namespace PlasmaLyrics {

class LyricStore;

class Resolver
{
public:
    Resolver(LyricStore &store, QList<Provider *> providers, bool filterCredits = true);
    ResolvedLyric resolve(const MprisState &state);
    static QStringList legacyWaylyricsIds(const MprisState &state);

private:
    std::optional<LyricDocument> overridden(const TrackRef &ref) const;
    LyricDocument forDisplay(LyricDocument document, const TrackRef &ref) const;

    LyricStore &m_store;
    QList<Provider *> m_providers;
    bool m_filterCredits;
};

} // namespace PlasmaLyrics
