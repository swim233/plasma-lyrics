#include "resolver.h"

#include "core/lyric/lrcparser.h"
#include "core/lyric/timeline.h"
#include "core/match/matcher.h"
#include "core/store/lyricstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QPointer>
#include <QSet>
#include <QUrl>

namespace PlasmaLyrics {

struct Resolver::Request {
    quint64 generation = 0;
    MprisState state;
    TrackQuery query;
    QList<Provider *> providers;
    qsizetype providerIndex = 0;
    bool networkFailed = false;
    bool sawEmptyLyric = false;
    bool keepExisting = false;
    std::optional<ResolvedLyric> existing;
    bool force = false;
    QString manualPreference;
    QString effectivePreference;
    QSet<QString> attemptedProviderVersions;
};

namespace {

constexpr qint64 networkMissTtlSeconds = 5 * 60;
constexpr qint64 noCandidateMissTtlSeconds = 7 * 24 * 60 * 60;

QString localMediaSource(const QString &mediaSrc, const QString &url)
{
    const auto existingLocalFile = [](const QString &value) {
        const QUrl candidate(value);
        return candidate.isValid() && candidate.isLocalFile()
            && QFileInfo(candidate.toLocalFile()).isFile();
    };
    if (existingLocalFile(mediaSrc)) return mediaSrc;
    if (existingLocalFile(url)) return url;
    return {};
}

} // namespace

Resolver::Resolver(LyricStore &store, QList<Provider *> providers, bool filterCredits,
                   QObject *parent, QString overrideDirectory)
    : QObject(parent)
    , m_store(store)
    , m_overrides(std::move(overrideDirectory))
    , m_providers(std::move(providers))
    , m_filterCredits(filterCredits)
{
}

QStringList Resolver::availableProviders() const
{
    QStringList result;
    for (auto *provider : m_providers) {
        if (provider && provider->isConfigured() && provider->supportsSearch()
            && !result.contains(provider->id())) {
            result.append(provider->id());
        }
    }
    return result;
}

QList<Provider *> Resolver::searchChain(const QString &preferred) const
{
    QList<Provider *> result;
    auto appendOnce = [&](Provider *provider) {
        if (provider && provider->isConfigured() && provider->supportsSearch()
            && !result.contains(provider)) {
            result.append(provider);
        }
    };
    if (!preferred.isEmpty()) {
        for (auto *provider : m_providers) {
            if (provider && provider->id() == preferred) appendOnce(provider);
        }
    }
    for (auto *provider : m_providers) appendOnce(provider);
    return result;
}

std::optional<LyricDocument> Resolver::overridden(const TrackRef &ref) const
{
    if (auto document = m_overrides.lyric(ref)) {
        return forDisplay(std::move(*document), ref);
    }
    return std::nullopt;
}

LyricDocument Resolver::forDisplay(LyricDocument document, const TrackRef &ref) const
{
    if (m_filterCredits) {
        document.lines = filterLeadingCredits(document.lines);
    }
    document.offsetMs = m_store.offset(ref);
    return document;
}

ResolvedLyric Resolver::resolvedLyric(const std::shared_ptr<Request> &request,
                                      const QString &state,
                                      const std::optional<TrackRef> &ref,
                                      LyricDocument document) const
{
    const bool fallback = ref && !request->effectivePreference.isEmpty()
        && ref->provider != request->effectivePreference;
    return ResolvedLyric{
        .state = state,
        .ref = ref,
        .document = std::move(document),
        .preferredProvider = request->manualPreference,
        .effectivePreferredProvider = request->effectivePreference,
        .temporaryFallback = fallback,
        .availableProviders = availableProviders(),
        .globalOffsetEnabled = false,
        .switchingProvider = {},
    };
}

QStringList Resolver::legacyWaylyricsIds(const MprisState &state)
{
    auto rustString = [](QString value) {
        value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        value.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
        value.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
        return QString(QLatin1Char('"') + value + QLatin1Char('"'));
    };
    QStringList artists;
    for (const auto &artist : state.artists) {
        artists.append(rustString(artist));
    }
    const QString artistsDebug = QStringLiteral("Some([") + artists.join(QStringLiteral(", ")) + QStringLiteral("])");
    const QString albumDebug = state.album.isNull()
        ? QStringLiteral("None")
        : QStringLiteral("Some(") + rustString(state.album) + QLatin1Char(')');
    QString duration;
    if (state.lengthUs <= 0) {
        duration = QStringLiteral("None");
    } else {
        const qint64 seconds = state.lengthUs / 1000000;
        const qint64 micros = state.lengthUs % 1000000;
        if (micros == 0) {
            duration = QStringLiteral("Some(%1s)").arg(seconds);
        } else if (micros % 1000 == 0) {
            duration = QStringLiteral("Some(%1.%2s)").arg(seconds).arg(micros / 1000, 3, 10, QLatin1Char('0'));
        } else {
            duration = QStringLiteral("Some(%1.%2s)").arg(seconds).arg(micros, 6, 10, QLatin1Char('0'));
        }
    }
    const QString key = state.title + QLatin1Char('-') + artistsDebug + QLatin1Char('-')
        + albumDebug + QLatin1Char('-') + duration;
    QStringList keys{QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex())};

    // QVariant conversion loses absent-vs-empty information. Trying the None
    // spellings makes imports work for players that omitted these properties.
    if (state.artists.isEmpty()) {
        const QString alternative = state.title + QStringLiteral("-None-") + albumDebug + QLatin1Char('-') + duration;
        keys.append(QString::fromLatin1(QCryptographicHash::hash(alternative.toUtf8(), QCryptographicHash::Md5).toHex()));
    }
    return keys;
}

void Resolver::finish(const std::shared_ptr<Request> &request, ResolvedLyric lyric)
{
    if (request->generation == m_generation) {
        Q_EMIT resolved(request->state.fingerprint, lyric);
    }
}

void Resolver::cancel()
{
    ++m_generation;
}

void Resolver::resolve(const MprisState &state)
{
    resolve(state, ResolveOptions{});
}

void Resolver::resolve(const MprisState &state, ResolveOptions options)
{
    const auto request = std::make_shared<Request>();
    request->generation = ++m_generation;
    request->state = state;
    request->force = options.force;
    request->keepExisting = options.existing
        && options.existing->state == QStringLiteral("ok")
        && options.existing->ref.has_value()
        && !options.existing->document.lines.isEmpty();
    if (request->keepExisting) request->existing = std::move(options.existing);
    request->manualPreference = m_store.preferredProvider(state.fingerprint).value_or(QString());
    request->providers = searchChain(request->manualPreference);
    if (!request->providers.isEmpty()) {
        request->effectivePreference = request->providers.first()->id();
    }
    qInfo().noquote() << QStringLiteral("resolve: fingerprint=%1 platform=%2 music=%3")
                             .arg(state.fingerprint,
                                  state.platform.isEmpty() ? QStringLiteral("unknown") : state.platform,
                                  state.music ? QStringLiteral("true") : QStringLiteral("false"));
    if (!state.music) {
        qInfo() << "state=filtered";
        finish(request, resolvedLyric(request, QStringLiteral("filtered"), std::nullopt));
        return;
    }
    if (!request->force) {
        const auto preferredMapped = !request->effectivePreference.isEmpty()
            ? m_store.refForProvider(state.fingerprint, request->effectivePreference)
            : std::nullopt;
        if (preferredMapped) {
            if (const auto override = overridden(*preferredMapped)) {
                if (!override->lines.isEmpty()) {
                    mapFingerprint(state.fingerprint, *preferredMapped);
                    finish(request, resolvedLyric(request, QStringLiteral("ok"),
                                                  *preferredMapped, *override));
                    return;
                }
            }
            if (const auto cached = m_store.lyric(*preferredMapped)) {
                auto display = forDisplay(*cached, *preferredMapped);
                if (!display.lines.isEmpty()) {
                    mapFingerprint(state.fingerprint, *preferredMapped);
                    finish(request, resolvedLyric(request, QStringLiteral("ok"),
                                                  *preferredMapped, std::move(display)));
                    return;
                }
            }
        }
    }
    if (!request->force) {
        if (const auto mapped = m_store.refForFingerprint(state.fingerprint)) {
            bool publishedExisting = false;
            if (const auto override = overridden(*mapped)) {
                const QString resultState = override->lines.isEmpty()
                    ? QStringLiteral("no-lyric") : QStringLiteral("ok");
                qInfo().noquote() << QStringLiteral("override hit: %1/%2 lines=%3")
                                         .arg(mapped->provider, mapped->trackId)
                                         .arg(override->lines.size());
                qInfo().noquote() << QStringLiteral("state=") + resultState;
                if (!override->lines.isEmpty()) {
                    finish(request, resolvedLyric(request, resultState, *mapped, *override));
                    if (mapped->provider == request->effectivePreference) return;
                    request->keepExisting = true;
                    request->providers = searchChain(request->effectivePreference).mid(0, 1);
                    publishedExisting = true;
                }
            }
            if (!publishedExisting) {
                if (const auto cached = m_store.lyric(*mapped)) {
                    auto display = forDisplay(*cached, *mapped);
                    const QString resultState = display.lines.isEmpty()
                        ? QStringLiteral("no-lyric") : QStringLiteral("ok");
                    qInfo().noquote() << QStringLiteral("cache hit: %1/%2 lines=%3")
                                             .arg(mapped->provider, mapped->trackId)
                                             .arg(display.lines.size());
                    qInfo().noquote() << QStringLiteral("state=") + resultState;
                    if (!display.lines.isEmpty()) {
                        finish(request, resolvedLyric(request, resultState, *mapped, display));
                        if (mapped->provider == request->effectivePreference) return;
                        request->keepExisting = true;
                        request->providers = searchChain(request->effectivePreference).mid(0, 1);
                        publishedExisting = true;
                    }
                }
            }
            if (!publishedExisting) {
                qInfo().noquote() << QStringLiteral("cache lyric missing: %1/%2")
                                         .arg(mapped->provider, mapped->trackId);
            }
        } else {
            qInfo().noquote() << QStringLiteral("cache mapping missing: ") + state.fingerprint;
        }
    }

    if (!request->force && !request->keepExisting) {
        for (const auto &legacyId : legacyWaylyricsIds(state)) {
            const TrackRef legacy{QStringLiteral("waylyrics"), legacyId, 1.0};
            if (const auto imported = m_store.lyric(legacy)) {
                mapFingerprint(state.fingerprint, legacy);
                auto display = forDisplay(*imported, legacy);
                const QString resultState = display.lines.isEmpty()
                    ? QStringLiteral("no-lyric") : QStringLiteral("ok");
                qInfo().noquote() << QStringLiteral("cache hit: %1/%2 lines=%3")
                                         .arg(legacy.provider, legacy.trackId)
                                         .arg(display.lines.size());
                qInfo().noquote() << QStringLiteral("state=") + resultState;
                if (!display.lines.isEmpty()) {
                    finish(request, resolvedLyric(request, resultState, legacy, display));
                    request->keepExisting = true;
                    request->providers = searchChain(request->effectivePreference).mid(0, 1);
                }
                break;
            }
        }
    }
    qInfo() << "cache miss";

    // kde:mediaSrc is more specific when present, but it is a KDE extension.
    // Standard MPRIS players normally expose the same local file only through
    // xesam:url. This affects sidecar discovery only; track identity continues
    // to use the untouched MprisState and MprisPolicy fingerprint.
    request->query = {state.title, state.artists, state.album, state.lengthUs / 1000,
                      {}, localMediaSource(state.mediaSrc, state.url)};
    continueWithProvider(request);
}

void Resolver::recordProviderFailure(const std::shared_ptr<Request> &request,
                                     Provider *provider, const QString &reason,
                                     const QString &cacheVersion)
{
    if (!m_store.recordProviderMiss(request->state.fingerprint, provider->id(), reason,
                                    cacheVersion)) {
        qWarning().noquote() << QStringLiteral("provider miss record failed: fingerprint=%1 provider=%2 reason=%3")
                                    .arg(request->state.fingerprint, provider->id(), reason);
    }
}

bool Resolver::retryProviderIfIndexChanged(const std::shared_ptr<Request> &request,
                                           Provider *provider,
                                           const QString &attemptedCacheVersion,
                                           const std::optional<QString> &observedCacheVersion)
{
    const QString currentCacheVersion = observedCacheVersion
        ? *observedCacheVersion : provider->knownCacheVersion();
    const QString currentAttempt = provider->id() + QLatin1Char('\x1f') + currentCacheVersion;
    if (attemptedCacheVersion.isEmpty() || currentCacheVersion == attemptedCacheVersion
        || request->attemptedProviderVersions.contains(currentAttempt)) {
        return false;
    }
    qInfo().noquote() << QStringLiteral("provider index changed during resolve: provider=%1 old=%2 new=%3; retrying")
                             .arg(provider->id(), attemptedCacheVersion, currentCacheVersion);
    request->providers.insert(request->providerIndex, provider);
    continueWithProvider(request);
    return true;
}

void Resolver::mapFingerprint(const QString &fingerprint, const TrackRef &ref)
{
    if (!m_store.mapFingerprint(fingerprint, ref)) {
        qWarning().noquote() << QStringLiteral("cache map failed: fingerprint=%1 ref=%2/%3")
                                    .arg(fingerprint, ref.provider, ref.trackId);
    }
}

void Resolver::clearProviderMiss(const QString &fingerprint, const QString &provider)
{
    if (!m_store.clearProviderMiss(fingerprint, provider)) {
        qWarning().noquote() << QStringLiteral("provider miss clear failed: fingerprint=%1 provider=%2")
                                    .arg(fingerprint, provider);
    }
}

void Resolver::continueWithProvider(const std::shared_ptr<Request> &request)
{
    if (request->generation != m_generation) {
        return;
    }
    Provider *provider = nullptr;
    QString providerCacheVersion;
    while (request->providerIndex < request->providers.size()) {
        auto *candidate = request->providers[request->providerIndex++];
        if (!candidate || !candidate->isConfigured() || !candidate->supportsSearch()) {
            continue;
        }
        const QString cacheVersion = candidate->cacheVersion();
        const QString attempt = candidate->id() + QLatin1Char('\x1f') + cacheVersion;
        if (request->attemptedProviderVersions.contains(attempt)) {
            continue;
        }
        request->attemptedProviderVersions.insert(attempt);
        provider = candidate;
        providerCacheVersion = cacheVersion;
        break;
    }
    if (!provider) {
        if (request->keepExisting) {
            if (request->existing) {
                auto retained = std::move(*request->existing);
                retained.preferredProvider = request->manualPreference;
                retained.effectivePreferredProvider = request->effectivePreference;
                retained.temporaryFallback = retained.ref
                    && !request->effectivePreference.isEmpty()
                    && retained.ref->provider != request->effectivePreference;
                retained.availableProviders = availableProviders();
                retained.switchingProvider.clear();
                finish(request, std::move(retained));
            }
            return;
        }
        const QString resultState = request->networkFailed
            ? QStringLiteral("network-error")
            : request->sawEmptyLyric ? QStringLiteral("no-lyric")
                                     : QStringLiteral("not-found");
        qInfo().noquote() << QStringLiteral("state=") + resultState;
        finish(request, resolvedLyric(request, resultState, std::nullopt));
        return;
    }


    if (!request->force) {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        // Read once with the longest TTL, then apply the reason-specific
        // baseline.  A cache/index version mismatch makes the method return
        // empty and immediately re-enables this provider.
        if (const auto miss = m_store.freshProviderMiss(
                request->state.fingerprint, provider->id(), providerCacheVersion, now,
                noCandidateMissTtlSeconds)) {
            const qint64 ttl = miss->reason == QStringLiteral("network")
                ? networkMissTtlSeconds : noCandidateMissTtlSeconds;
            if (now - miss->triedAt < ttl) {
                qInfo().noquote() << QStringLiteral("fresh provider miss: provider=%1 reason=%2")
                                         .arg(provider->id(), miss->reason);
                request->networkFailed |= miss->reason == QStringLiteral("network");
                request->sawEmptyLyric |= miss->reason == QStringLiteral("empty");
                continueWithProvider(request);
                return;
            }
        }
        if (const auto mapped = m_store.refForProvider(request->state.fingerprint, provider->id())) {
            if (const auto cached = m_store.lyric(*mapped)) {
                auto display = forDisplay(*cached, *mapped);
                if (!display.lines.isEmpty()) {
                    mapFingerprint(request->state.fingerprint, *mapped);
                    finish(request, resolvedLyric(request, QStringLiteral("ok"), *mapped,
                                                  std::move(display)));
                    return;
                }
            }
        }
    }

    const QPointer<Resolver> self(this);
    const QString searchCacheVersion = providerCacheVersion;
    provider->searchPrepared(request->query, searchCacheVersion,
                     [self, request, provider, searchCacheVersion](ProviderSearchResult result) mutable {
        if (!self || request->generation != self->m_generation) {
            return;
        }
        const QString resultCacheVersion = result.cacheVersion.isEmpty()
            ? searchCacheVersion : result.cacheVersion;
        if (!result.error.isEmpty()) {
            qInfo().noquote() << QStringLiteral("search failed: %1: %2")
                                     .arg(provider->id(), result.error);
            request->networkFailed |= result.transportFailed;
            if (result.cacheableMiss) {
                self->recordProviderFailure(request, provider,
                                            result.transportFailed ? QStringLiteral("network")
                                                                   : QStringLiteral("search-error"),
                                            resultCacheVersion);
            }
            if (self->retryProviderIfIndexChanged(request, provider, searchCacheVersion,
                                                  resultCacheVersion)) return;
            self->continueWithProvider(request);
            return;
        }
        qInfo().noquote() << explainMatch(request->query, result.candidates,
                                           request->state.platform == QStringLiteral("apple"), true,
                                           provider->matchPolicy());
        const auto ranked = rankCandidates(request->query, result.candidates,
                                           provider->matchPolicy());
        const auto chosen = chooseMatch(ranked,
                                        request->state.platform == QStringLiteral("apple"));
        if (!chosen) {
            if (result.cacheableMiss) {
                self->recordProviderFailure(request, provider, QStringLiteral("no-candidate"),
                                            resultCacheVersion);
            }
            if (self->retryProviderIfIndexChanged(request, provider, searchCacheVersion,
                                                  resultCacheVersion)) return;
            self->continueWithProvider(request);
            return;
        }
        const TrackRef ref{provider->id(), chosen->candidate.trackId, chosen->score.total};
        const QString contentId = chosen->candidate.contentId.isEmpty()
            ? chosen->candidate.trackId : chosen->candidate.contentId;
        const bool cacheableMiss = result.cacheableMiss;
        provider->fetch(contentId,
                        [self, request, ref, provider,
                         resultCacheVersion, cacheableMiss](ProviderFetchResult result) mutable {
            if (!self || request->generation != self->m_generation) {
                return;
            }
            if (!result.document) {
                qInfo().noquote() << QStringLiteral("fetch failed: %1/%2: %3")
                                         .arg(ref.provider, ref.trackId,
                                              result.error.isEmpty()
                                                  ? QStringLiteral("unknown error") : result.error);
                request->networkFailed |= result.transportFailed;
                if (cacheableMiss) {
                    self->recordProviderFailure(request, provider,
                                                result.transportFailed ? QStringLiteral("network")
                                                                       : QStringLiteral("fetch-error"),
                                                resultCacheVersion);
                }
                if (self->retryProviderIfIndexChanged(request, provider,
                                                      resultCacheVersion)) return;
                self->continueWithProvider(request);
                return;
            }
            qInfo().noquote() << QStringLiteral("fetched: lines=%1 hasWords=%2")
                                     .arg(result.document->lines.size())
                                     .arg(result.document->hasWords
                                              ? QStringLiteral("true") : QStringLiteral("false"));
            if (result.document->lines.isEmpty()) {
                request->sawEmptyLyric = true;
                if (cacheableMiss) {
                    self->recordProviderFailure(request, provider, QStringLiteral("empty"),
                                                resultCacheVersion);
                }
                if (self->retryProviderIfIndexChanged(request, provider,
                                                      resultCacheVersion)) return;
                self->continueWithProvider(request);
                return;
            }
            auto finalDocument = self->forDisplay(*result.document, ref);
            if (finalDocument.lines.isEmpty()) {
                request->sawEmptyLyric = true;
                if (cacheableMiss) {
                    self->recordProviderFailure(request, provider, QStringLiteral("empty"),
                                                resultCacheVersion);
                }
                if (self->retryProviderIfIndexChanged(request, provider,
                                                      resultCacheVersion)) return;
                self->continueWithProvider(request);
                return;
            }
            if (!self->m_store.putLyric(ref, *result.document)) {
                qWarning().noquote() << QStringLiteral("cache put failed: %1/%2")
                                            .arg(ref.provider, ref.trackId);
            }
            self->mapFingerprint(request->state.fingerprint, ref);
            self->clearProviderMiss(request->state.fingerprint, ref.provider);
            qInfo().noquote() << QStringLiteral("after filterLeadingCredits: lines=%1")
                                     .arg(finalDocument.lines.size());
            const QString resultState = QStringLiteral("ok");
            qInfo().noquote() << QStringLiteral("state=") + resultState;
            self->finish(request, self->resolvedLyric(request, resultState, ref,
                                                      std::move(finalDocument)));
        });
    });
}

} // namespace PlasmaLyrics
