#pragma once

#include "core/store/lyricoverridestore.h"
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
        // What caused this request; see the trigger vocabulary in
        // DESIGN.md's diagnostics decision. Logged verbatim on the "resolve:"
        // head line.
        QString trigger;
    };

    Resolver(LyricStore &store, QList<Provider *> providers, bool filterCredits = true,
             QObject *parent = nullptr, QString overrideDirectory = {});
    // Convenience overload for callers that do not need force/existing.
    void resolve(const MprisState &state, QString trigger);
    void resolve(const MprisState &state, ResolveOptions options);
    void cancel();
    QStringList availableProviders() const;
    static QStringList legacyWaylyricsIds(const MprisState &state);

Q_SIGNALS:
    void resolved(const QString &fingerprint, const PlasmaLyrics::ResolvedLyric &lyric);

private:
    struct Request;
    std::optional<LyricDocument> overridden(quint64 generation, const TrackRef &ref) const;
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
                                     const QString &attemptedCacheVersion,
                                     const std::optional<QString> &observedCacheVersion = std::nullopt);
    void mapFingerprint(quint64 generation, const QString &fingerprint, const TrackRef &ref);
    void clearProviderMiss(quint64 generation, const QString &fingerprint, const QString &provider);
    void continueWithProvider(const std::shared_ptr<Request> &request);
    void finish(const std::shared_ptr<Request> &request, ResolvedLyric lyric);

    // Builds the one-per-request "#gen state=..." trailer line without
    // emitting it -- used both by finishTerminal() and by the log-only
    // retained-on-exhaustion path that must not re-publish already-published
    // content (see the "retained" contract in resolve()/continueWithProvider()).
    QString terminalLine(const std::shared_ptr<Request> &request, const QString &state,
                         const std::optional<TrackRef> &ref, int lines,
                         const QString &from) const;
    // Logs the trailer, then finish()es the request. `from` is one of
    // provider|cache|override|retained, or empty for a sourceless terminal
    // state (not-found/network-error/no-lyric/filtered). A legacy waylyrics
    // import is never itself a terminal source -- it always sits outside the
    // provider chain, so a hit always goes on to (re)try the preferred
    // provider and its eventual terminal `from` is "retained".
    void finishTerminal(const std::shared_ptr<Request> &request, ResolvedLyric lyric,
                        const QString &from);
    // Logs the "retained:" head line that announces "keep showing this while
    // the preferred source is (re)tried" -- does not itself publish anything.
    void logRetainedStart(const std::shared_ptr<Request> &request, const TrackRef &ref, int lines);

    LyricStore &m_store;
    LyricOverrideStore m_overrides;
    QList<Provider *> m_providers;
    bool m_filterCredits;
    quint64 m_generation = 0;
};

} // namespace PlasmaLyrics
