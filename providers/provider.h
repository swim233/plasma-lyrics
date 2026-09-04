#pragma once

#include "core/lyric/lyricmodel.h"
#include "core/match/matcher.h"

#include <QList>
#include <QString>
#include <optional>

namespace PlasmaLyrics {

class Provider
{
public:
    virtual ~Provider() = default;
    virtual QString id() const = 0;
    virtual bool isConfigured() const = 0;
    virtual bool supportsSearch() const { return true; }
    virtual QList<Candidate> search(const TrackQuery &query) = 0;
    virtual std::optional<LyricDocument> fetch(const QString &trackId) = 0;
    virtual QString lastError() const = 0;
    virtual std::optional<LyricDocument> overrideFor(const QString &providerId,
                                                     const QString &trackId)
    {
        Q_UNUSED(providerId)
        Q_UNUSED(trackId)
        return std::nullopt;
    }
};

} // namespace PlasmaLyrics
