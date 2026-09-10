#pragma once

#include "core/lyric/lyricmodel.h"
#include "core/match/matcher.h"

#include <QList>
#include <QString>
#include <functional>
#include <optional>
#include <utility>

namespace PlasmaLyrics {

struct ProviderSearchResult {
    QList<Candidate> candidates;
    QString error;
    // True only when the request failed in transport. HTTP and payload
    // parsing errors can carry error text without becoming network-error.
    bool transportFailed = false;
    // Immutable identity of the provider index/source snapshot that produced
    // candidates. Resolver must use this value, not a later cacheVersion(),
    // when persisting a fetch miss after an asynchronous refresh.
    QString cacheVersion;
    // False means no search/fetch/empty-result failure from this attempt may
    // be persisted. A local-file sidecar can appear or be repaired without
    // changing the provider-wide lyrics-directory version.
    bool cacheableMiss = true;
};

struct ProviderFetchResult {
    std::optional<LyricDocument> document;
    QString error;
    // Kept separate from error so Resolver can choose the state and miss TTL
    // without interpreting provider-specific error strings.
    bool transportFailed = false;
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
    virtual MatchPolicy matchPolicy() const { return MatchPolicy::Default; }
    // Included in provider-scoped negative-cache rows.  Changing a source
    // endpoint or index revision makes older misses inapplicable.
    virtual QString cacheVersion() const { return id(); }
    // Returns the version already held by the provider without refreshing an
    // expensive local index. Resolver uses this only after an asynchronous
    // fetch failure to detect a source revision that changed during the fetch.
    virtual QString knownCacheVersion() const { return cacheVersion(); }
    // Network-backed providers must complete through these callbacks without
    // spinning a nested event loop; Resolver discards callbacks from requests
    // that are no longer current.
    virtual void search(const TrackQuery &query, SearchCallback callback) = 0;
    // Resolver has already read cacheVersion() for negative-cache lookup. An
    // indexed provider may reuse the snapshot prepared by that read instead
    // of walking its source again. Other providers keep their normal search.
    virtual void searchPrepared(const TrackQuery &query, const QString &cacheVersion,
                                SearchCallback callback)
    {
        (void)cacheVersion;
        search(query, std::move(callback));
    }
    virtual void fetch(const QString &trackId, FetchCallback callback) = 0;
};

} // namespace PlasmaLyrics
