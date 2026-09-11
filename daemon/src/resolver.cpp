#include "resolver.h"

#include "core/lyric/lrcparser.h"
#include "core/lyric/timeline.h"
#include "core/log/logformat.h"
#include "core/match/matcher.h"
#include "core/store/lyricstore.h"
#include "logging.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
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
    QString trigger;
    QElapsedTimer timer;
    // Provider ids whose search() this request actually invoked, in order.
    // Populated only at the searchPrepared() call site -- a provider skipped
    // via the fresh-miss cooldown or an already-mapped cache hit never runs
    // its search and so never appears here.
    QStringList tried;
    // Set whenever this request starts showing already-known content while
    // it (re)tries a provider in the background (see logRetainedStart()).
    // Used only to build the exhaustion trailer for the paths that publish
    // that content via a direct finish() rather than through `existing`
    // (which is reserved for the forceResolve keepExisting contract, where
    // the retry's final outcome -- success or exhaustion -- is the request's
    // only publish).
    std::optional<TrackRef> retainedRef;
    int retainedLines = 0;
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

QString joinOrDash(const QStringList &values)
{
    return values.isEmpty() ? QStringLiteral("-") : values.join(QLatin1Char(','));
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

std::optional<LyricDocument> Resolver::overridden(quint64 generation, const TrackRef &ref) const
{
    int droppedLines = 0;
    if (auto document = m_overrides.lyric(ref, &droppedLines)) {
        if (droppedLines > 0) {
            qCDebug(lcResolver).noquote() << QStringLiteral(
                "#%1 override bilingual pairing: %2/%3 dropped=%4")
                .arg(generation).arg(ref.provider, ref.trackId).arg(droppedLines);
        }
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
    if (request->generation != m_generation) {
        qCDebug(lcResolver).noquote() << QStringLiteral("#%1 superseded").arg(request->generation);
        return;
    }
    Q_EMIT resolved(request->state.fingerprint, lyric);
}

QString Resolver::terminalLine(const std::shared_ptr<Request> &request, const QString &state,
                               const std::optional<TrackRef> &ref, int lines,
                               const QString &from) const
{
    QString line = QStringLiteral("#%1 state=%2").arg(request->generation).arg(state);
    if (state == QStringLiteral("filtered")) {
        return line + QStringLiteral(" elapsed=%1ms").arg(request->timer.elapsed());
    }
    if (!from.isEmpty()) {
        line += QStringLiteral(" from=%1").arg(from);
        if (ref) {
            line += QStringLiteral(" source=%1/%2").arg(ref->provider, ref->trackId);
        }
        line += QStringLiteral(" lines=%1").arg(lines);
    }
    // The provider that actually produced a successful source is excluded
    // from tried -- it was searched too, but "tried" here means "searched
    // and did not end up as the source". Every other outcome (cache/
    // override/legacy/retained/failure) shows the full invoked list as-is:
    // none of those providers contributed the result.
    QStringList tried = request->tried;
    if (from == QStringLiteral("provider") && ref) {
        tried.removeAll(ref->provider);
    }
    const bool alwaysShowTried = state == QStringLiteral("not-found")
        || state == QStringLiteral("network-error") || state == QStringLiteral("no-lyric");
    if (!tried.isEmpty()) {
        line += QStringLiteral(" tried=%1").arg(tried.join(QLatin1Char(',')));
    } else if (alwaysShowTried) {
        line += QStringLiteral(" tried=-");
    }
    line += QStringLiteral(" elapsed=%1ms").arg(request->timer.elapsed());
    return line;
}

void Resolver::finishTerminal(const std::shared_ptr<Request> &request, ResolvedLyric lyric,
                              const QString &from)
{
    qCInfo(lcResolver).noquote() << terminalLine(request, lyric.state, lyric.ref,
                                                 lyric.document.lines.size(), from);
    finish(request, std::move(lyric));
}

void Resolver::logRetainedStart(const std::shared_ptr<Request> &request, const TrackRef &ref, int lines)
{
    QStringList retrying;
    for (auto *provider : request->providers) {
        if (provider) retrying.append(provider->id());
    }
    qCInfo(lcResolver).noquote() << QStringLiteral("#%1 retained: source=%2/%3 lines=%4 retrying=%5")
        .arg(request->generation).arg(ref.provider, ref.trackId).arg(lines).arg(joinOrDash(retrying));
}

void Resolver::cancel()
{
    ++m_generation;
}

void Resolver::resolve(const MprisState &state, QString trigger)
{
    resolve(state, ResolveOptions{.force = false, .existing = std::nullopt, .trigger = std::move(trigger)});
}

void Resolver::resolve(const MprisState &state, ResolveOptions options)
{
    const auto request = std::make_shared<Request>();
    request->timer.start();
    request->generation = ++m_generation;
    request->state = state;
    request->force = options.force;
    request->trigger = options.trigger;
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
    const QString identity = state.identity.isEmpty() ? QStringLiteral("unknown") : state.identity;
    const QString platform = state.platform.isEmpty() ? QStringLiteral("unknown") : state.platform;
    QString headerLine = QStringLiteral(
        "#%1 resolve: trigger=%2 identity=%3 service=%4 fingerprint=%5 platform=%6 music=%7")
            .arg(request->generation)
            .arg(request->trigger, quoted(identity), state.service, quoted(state.fingerprint),
                 platform, state.music ? QStringLiteral("true") : QStringLiteral("false"));
    if (state.music) {
        // A non-music source (e.g. a browser tab playing video) must not put
        // its title into the journal at info level by default -- only the
        // music path names the track here. Browser integrations commonly
        // report a single empty-string artist ([""]) rather than an empty
        // list; MprisPolicy::isMusic treats that the same as "no artist".
        const QString firstArtist = state.artists.value(0);
        const QString artist = firstArtist.isEmpty() ? QStringLiteral("-") : firstArtist;
        headerLine += QStringLiteral(" title=%1 artist=%2").arg(quoted(state.title), quoted(artist));
    }
    qCInfo(lcResolver).noquote() << headerLine;
    if (!state.music) {
        finishTerminal(request, resolvedLyric(request, QStringLiteral("filtered"), std::nullopt),
                      QString());
        return;
    }
    if (!request->force) {
        const auto preferredMapped = !request->effectivePreference.isEmpty()
            ? m_store.refForProvider(state.fingerprint, request->effectivePreference)
            : std::nullopt;
        if (preferredMapped) {
            if (const auto override = overridden(request->generation, *preferredMapped)) {
                qCDebug(lcResolver).noquote() << QStringLiteral("#%1 override hit: %2/%3 lines=%4")
                    .arg(request->generation).arg(preferredMapped->provider, preferredMapped->trackId)
                    .arg(override->lines.size());
                if (!override->lines.isEmpty()) {
                    mapFingerprint(request->generation, state.fingerprint, *preferredMapped);
                    finishTerminal(request, resolvedLyric(request, QStringLiteral("ok"),
                                                          *preferredMapped, *override),
                                  QStringLiteral("override"));
                    return;
                }
            }
            if (const auto cached = m_store.lyric(*preferredMapped)) {
                auto display = forDisplay(*cached, *preferredMapped);
                qCDebug(lcResolver).noquote() << QStringLiteral("#%1 cache hit: %2/%3 lines=%4")
                    .arg(request->generation).arg(preferredMapped->provider, preferredMapped->trackId)
                    .arg(display.lines.size());
                if (!display.lines.isEmpty()) {
                    mapFingerprint(request->generation, state.fingerprint, *preferredMapped);
                    finishTerminal(request, resolvedLyric(request, QStringLiteral("ok"),
                                                          *preferredMapped, std::move(display)),
                                  QStringLiteral("cache"));
                    return;
                }
            }
        }
    }
    if (!request->force) {
        if (const auto mapped = m_store.refForFingerprint(state.fingerprint)) {
            bool publishedExisting = false;
            if (const auto override = overridden(request->generation, *mapped)) {
                const QString resultState = override->lines.isEmpty()
                    ? QStringLiteral("no-lyric") : QStringLiteral("ok");
                qCDebug(lcResolver).noquote() << QStringLiteral("#%1 override hit: %2/%3 lines=%4")
                    .arg(request->generation).arg(mapped->provider, mapped->trackId)
                    .arg(override->lines.size());
                if (!override->lines.isEmpty()) {
                    if (mapped->provider == request->effectivePreference) {
                        finishTerminal(request, resolvedLyric(request, resultState, *mapped, *override),
                                      QStringLiteral("override"));
                        return;
                    }
                    request->keepExisting = true;
                    request->providers = searchChain(request->effectivePreference).mid(0, 1);
                    request->retainedRef = *mapped;
                    request->retainedLines = override->lines.size();
                    logRetainedStart(request, *mapped, request->retainedLines);
                    finish(request, resolvedLyric(request, resultState, *mapped, *override));
                    publishedExisting = true;
                }
            }
            if (!publishedExisting) {
                if (const auto cached = m_store.lyric(*mapped)) {
                    auto display = forDisplay(*cached, *mapped);
                    const QString resultState = display.lines.isEmpty()
                        ? QStringLiteral("no-lyric") : QStringLiteral("ok");
                    qCDebug(lcResolver).noquote() << QStringLiteral("#%1 cache hit: %2/%3 lines=%4")
                        .arg(request->generation).arg(mapped->provider, mapped->trackId)
                        .arg(display.lines.size());
                    if (!display.lines.isEmpty()) {
                        if (mapped->provider == request->effectivePreference) {
                            finishTerminal(request, resolvedLyric(request, resultState, *mapped, display),
                                          QStringLiteral("cache"));
                            return;
                        }
                        request->keepExisting = true;
                        request->providers = searchChain(request->effectivePreference).mid(0, 1);
                        request->retainedRef = *mapped;
                        request->retainedLines = display.lines.size();
                        logRetainedStart(request, *mapped, request->retainedLines);
                        finish(request, resolvedLyric(request, resultState, *mapped, display));
                        publishedExisting = true;
                    }
                }
            }
            if (!publishedExisting) {
                qCDebug(lcResolver).noquote() << QStringLiteral("#%1 cache lyric missing: %2/%3")
                    .arg(request->generation).arg(mapped->provider, mapped->trackId);
            }
        } else {
            qCDebug(lcResolver).noquote() << QStringLiteral("#%1 cache mapping missing: fingerprint=%2")
                .arg(request->generation).arg(quoted(state.fingerprint));
        }
    }

    if (!request->force && !request->keepExisting) {
        for (const auto &legacyId : legacyWaylyricsIds(state)) {
            const TrackRef legacy{QStringLiteral("waylyrics"), legacyId, 1.0};
            if (const auto imported = m_store.lyric(legacy)) {
                mapFingerprint(request->generation, state.fingerprint, legacy);
                auto display = forDisplay(*imported, legacy);
                const QString resultState = display.lines.isEmpty()
                    ? QStringLiteral("no-lyric") : QStringLiteral("ok");
                qCDebug(lcResolver).noquote() << QStringLiteral("#%1 cache hit: %2/%3 lines=%4")
                    .arg(request->generation).arg(legacy.provider, legacy.trackId)
                    .arg(display.lines.size());
                if (!display.lines.isEmpty()) {
                    request->keepExisting = true;
                    request->providers = searchChain(request->effectivePreference).mid(0, 1);
                    request->retainedRef = legacy;
                    request->retainedLines = display.lines.size();
                    logRetainedStart(request, legacy, request->retainedLines);
                    finish(request, resolvedLyric(request, resultState, legacy, display));
                }
                break;
            }
        }
    }

    // A forced request that is keeping an already-displayed lyric (a manual
    // "research"/preference change) never runs the cache/override lookups
    // above, so it announces its own retained content here instead.
    if (request->force && request->keepExisting && request->existing && request->existing->ref) {
        request->retainedRef = request->existing->ref;
        request->retainedLines = request->existing->document.lines.size();
        logRetainedStart(request, *request->retainedRef, request->retainedLines);
    }

    if (!request->keepExisting) {
        qCInfo(lcResolver).noquote() << QStringLiteral("#%1 cache miss").arg(request->generation);
    }

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
        qCWarning(lcResolver).noquote() << QStringLiteral(
            "#%1 provider miss record failed: fingerprint=%2 provider=%3 reason=%4")
                .arg(request->generation).arg(quoted(request->state.fingerprint), provider->id(), reason);
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
    qCDebug(lcResolver).noquote() << QStringLiteral(
        "#%1 provider index changed during resolve: provider=%2 old=%3 new=%4; retrying")
            .arg(request->generation).arg(provider->id(), attemptedCacheVersion, currentCacheVersion);
    request->providers.insert(request->providerIndex, provider);
    continueWithProvider(request);
    return true;
}

void Resolver::mapFingerprint(quint64 generation, const QString &fingerprint, const TrackRef &ref)
{
    if (!m_store.mapFingerprint(fingerprint, ref)) {
        qCWarning(lcResolver).noquote() << QStringLiteral("#%1 cache map failed: fingerprint=%2 ref=%3/%4")
                                    .arg(generation).arg(quoted(fingerprint), ref.provider, ref.trackId);
    }
}

void Resolver::clearProviderMiss(quint64 generation, const QString &fingerprint, const QString &provider)
{
    if (!m_store.clearProviderMiss(fingerprint, provider)) {
        qCWarning(lcResolver).noquote() << QStringLiteral("#%1 provider miss clear failed: fingerprint=%2 provider=%3")
                                    .arg(generation).arg(quoted(fingerprint), provider);
    }
}

void Resolver::continueWithProvider(const std::shared_ptr<Request> &request)
{
    if (request->generation != m_generation) {
        qCDebug(lcResolver).noquote() << QStringLiteral("#%1 superseded").arg(request->generation);
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
                finishTerminal(request, std::move(retained), QStringLiteral("retained"));
            } else if (request->retainedRef) {
                // Already published via the mid-flight finish() above (or by
                // main.cpp for the forced case): log the trailer only, do not
                // re-emit identical content.
                qCInfo(lcResolver).noquote() << terminalLine(request, QStringLiteral("ok"),
                                                              request->retainedRef,
                                                              request->retainedLines,
                                                              QStringLiteral("retained"));
            }
            return;
        }
        const QString resultState = request->networkFailed
            ? QStringLiteral("network-error")
            : request->sawEmptyLyric ? QStringLiteral("no-lyric")
                                     : QStringLiteral("not-found");
        finishTerminal(request, resolvedLyric(request, resultState, std::nullopt), QString());
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
                qCInfo(lcResolver).noquote() << QStringLiteral(
                    "#%1 fresh provider miss: provider=%2 reason=%3")
                        .arg(request->generation).arg(provider->id(), miss->reason);
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
                    mapFingerprint(request->generation, request->state.fingerprint, *mapped);
                    qCDebug(lcResolver).noquote() << QStringLiteral("#%1 cache hit: %2/%3 lines=%4")
                        .arg(request->generation).arg(mapped->provider, mapped->trackId)
                        .arg(display.lines.size());
                    finishTerminal(request, resolvedLyric(request, QStringLiteral("ok"), *mapped,
                                                          std::move(display)), QStringLiteral("cache"));
                    return;
                }
            }
        }
    }

    const QPointer<Resolver> self(this);
    const QString searchCacheVersion = providerCacheVersion;
    request->tried.append(provider->id());
    QElapsedTimer searchTimer;
    searchTimer.start();
    provider->searchPrepared(request->query, searchCacheVersion,
                     [self, request, provider, searchCacheVersion, searchTimer](ProviderSearchResult result) mutable {
        if (!self || request->generation != self->m_generation) {
            qCDebug(lcResolver).noquote() << QStringLiteral("#%1 superseded").arg(request->generation);
            return;
        }
        const qint64 searchElapsed = searchTimer.elapsed();
        const QString resultCacheVersion = result.cacheVersion.isEmpty()
            ? searchCacheVersion : result.cacheVersion;
        if (!result.error.isEmpty()) {
            qCInfo(lcResolver).noquote() << QStringLiteral(
                "#%1 search failed: provider=%2 elapsed=%3ms error=%4")
                    .arg(request->generation).arg(provider->id())
                    .arg(searchElapsed).arg(quoted(result.error));
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
        if (lcResolver().isDebugEnabled()) {
            const QString explanation = explainMatch(request->query, result.candidates,
                                               request->state.platform == QStringLiteral("apple"), true,
                                               provider->matchPolicy());
            const auto explanationLines = explanation.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const auto &explanationLine : explanationLines) {
                qCDebug(lcResolver).noquote() << QStringLiteral("#%1 %2")
                    .arg(request->generation).arg(explanationLine);
            }
        }
        const auto ranked = rankCandidates(request->query, result.candidates,
                                           provider->matchPolicy());
        const auto chosen = chooseMatch(ranked,
                                        request->state.platform == QStringLiteral("apple"));
        if (!chosen) {
            QString searchLine = QStringLiteral("#%1 search provider=%2 candidates=%3 selected=none")
                .arg(request->generation).arg(provider->id()).arg(result.candidates.size());
            if (!ranked.isEmpty()) {
                const QString reason = candidateRejectionReason(ranked.first());
                searchLine += QStringLiteral(" best=%1 rejected=%2")
                    .arg(ranked.first().candidate.trackId,
                         reason.isEmpty() ? QStringLiteral("unknown") : reason);
            }
            searchLine += QStringLiteral(" elapsed=%1ms").arg(searchElapsed);
            qCInfo(lcResolver).noquote() << searchLine;
            if (result.cacheableMiss) {
                self->recordProviderFailure(request, provider, QStringLiteral("no-candidate"),
                                            resultCacheVersion);
            }
            if (self->retryProviderIfIndexChanged(request, provider, searchCacheVersion,
                                                  resultCacheVersion)) return;
            self->continueWithProvider(request);
            return;
        }
        qCInfo(lcResolver).noquote() << QStringLiteral(
            "#%1 search provider=%2 candidates=%3 selected=%4 score=%5 elapsed=%6ms")
                .arg(request->generation).arg(provider->id()).arg(result.candidates.size())
                .arg(chosen->candidate.trackId, QString::number(chosen->score.total, 'f', 3))
                .arg(searchElapsed);
        const TrackRef ref{provider->id(), chosen->candidate.trackId, chosen->score.total};
        const QString contentId = chosen->candidate.contentId.isEmpty()
            ? chosen->candidate.trackId : chosen->candidate.contentId;
        const bool cacheableMiss = result.cacheableMiss;
        QElapsedTimer fetchTimer;
        fetchTimer.start();
        provider->fetch(contentId,
                        [self, request, ref, provider,
                         resultCacheVersion, cacheableMiss, fetchTimer](ProviderFetchResult result) mutable {
            if (!self || request->generation != self->m_generation) {
                qCDebug(lcResolver).noquote() << QStringLiteral("#%1 superseded").arg(request->generation);
                return;
            }
            const qint64 fetchElapsed = fetchTimer.elapsed();
            if (!result.document) {
                qCInfo(lcResolver).noquote() << QStringLiteral(
                    "#%1 fetch failed: source=%2/%3 elapsed=%4ms error=%5")
                        .arg(request->generation).arg(ref.provider, ref.trackId)
                        .arg(fetchElapsed)
                        .arg(quoted(result.error.isEmpty()
                                        ? QStringLiteral("unknown error") : result.error));
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
            qCInfo(lcResolver).noquote() << QStringLiteral(
                "#%1 fetched: %2/%3 lines=%4 hasWords=%5 elapsed=%6ms")
                    .arg(request->generation).arg(ref.provider, ref.trackId)
                    .arg(result.document->lines.size())
                    .arg(result.document->hasWords
                             ? QStringLiteral("true") : QStringLiteral("false"))
                    .arg(fetchElapsed);
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
                qCWarning(lcResolver).noquote() << QStringLiteral("#%1 cache put failed: %2/%3")
                                            .arg(request->generation).arg(ref.provider, ref.trackId);
            }
            self->mapFingerprint(request->generation, request->state.fingerprint, ref);
            self->clearProviderMiss(request->generation, request->state.fingerprint, ref.provider);
            qCDebug(lcResolver).noquote() << QStringLiteral("#%1 after filterLeadingCredits: lines=%2")
                                     .arg(request->generation).arg(finalDocument.lines.size());
            self->finishTerminal(request, self->resolvedLyric(request, QStringLiteral("ok"), ref,
                                                              std::move(finalDocument)),
                                QStringLiteral("provider"));
        });
    });
}

} // namespace PlasmaLyrics
