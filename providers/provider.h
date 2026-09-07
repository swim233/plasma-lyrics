#pragma once

#include "core/lyric/lyricmodel.h"
#include "core/match/matcher.h"

#include <QList>
#include <QString>
#include <functional>
#include <optional>

namespace PlasmaLyrics {

struct ProviderSearchResult {
    QList<Candidate> candidates;
    QString error;
};

struct ProviderFetchResult {
    std::optional<LyricDocument> document;
    QString error;
};

class Provider
{
public:
    using SearchCallback = std::function<void(ProviderSearchResult)>;
    using FetchCallback = std::function<void(ProviderFetchResult)>;

    virtual ~Provider() = default;
    virtual QString id() const = 0;
    virtual bool isConfigured() const = 0;
    virtual bool supportsSearch() const { return true; }
    // Network-backed providers must complete through these callbacks without
    // spinning a nested event loop; Resolver discards callbacks from requests
    // that are no longer current.
    virtual void search(const TrackQuery &query, SearchCallback callback) = 0;
    virtual void fetch(const QString &trackId, FetchCallback callback) = 0;
    virtual std::optional<LyricDocument> overrideFor(const QString &providerId,
                                                     const QString &trackId)
    {
        Q_UNUSED(providerId)
        Q_UNUSED(trackId)
        return std::nullopt;
    }
};

} // namespace PlasmaLyrics
