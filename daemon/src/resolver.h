#pragma once

#include "daemon/src/snapshot.h"
#include "providers/provider.h"

#include <QList>
#include <QObject>
#include <memory>

namespace PlasmaLyrics {

class LyricStore;

class Resolver : public QObject
{
    Q_OBJECT

public:
    struct ResolveOptions {
        bool force = false;
        // Manual commands may keep an already displayed complete lyric while
        // the forced request runs. Carry the actual value instead of a bool so
        // Resolver can reject searching/error/empty snapshots and still emit
        // a terminal failure for them.
        std::optional<ResolvedLyric> existing;
    };

    Resolver(LyricStore &store, QList<Provider *> providers, bool filterCredits = true,
             QObject *parent = nullptr);
    void resolve(const MprisState &state);
    void resolve(const MprisState &state, ResolveOptions options);
    void cancel();
    QStringList availableProviders() const;
    static QStringList legacyWaylyricsIds(const MprisState &state);

Q_SIGNALS:
    void resolved(const QString &fingerprint, const PlasmaLyrics::ResolvedLyric &lyric);

private:
    struct Request;
    std::optional<LyricDocument> overridden(const TrackRef &ref) const;
    LyricDocument forDisplay(LyricDocument document, const TrackRef &ref) const;
    ResolvedLyric resolvedLyric(const std::shared_ptr<Request> &request,
                                const QString &state, const std::optional<TrackRef> &ref,
                                LyricDocument document = {}) const;
    QList<Provider *> searchChain(const QString &preferred) const;
    void recordProviderFailure(const std::shared_ptr<Request> &request,
                               Provider *provider, const QString &reason,
                               const QString &cacheVersion);
    bool retryProviderIfIndexChanged(const std::shared_ptr<Request> &request,
                                     Provider *provider,
                                     const QString &attemptedCacheVersion);
    void mapFingerprint(const QString &fingerprint, const TrackRef &ref);
    void clearProviderMiss(const QString &fingerprint, const QString &provider);
    void continueWithProvider(const std::shared_ptr<Request> &request);
    void finish(const std::shared_ptr<Request> &request, ResolvedLyric lyric);

    LyricStore &m_store;
    QList<Provider *> m_providers;
    bool m_filterCredits;
    quint64 m_generation = 0;
};

} // namespace PlasmaLyrics
