#include "resolver.h"

#include "core/lyric/lrcparser.h"
#include "core/lyric/timeline.h"
#include "core/match/matcher.h"
#include "core/store/lyricstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QPointer>

namespace PlasmaLyrics {

struct Resolver::Request {
    quint64 generation = 0;
    MprisState state;
    TrackQuery query;
    qsizetype providerIndex = 0;
    bool networkFailed = false;
};

namespace {

constexpr qint64 networkMissTtlSeconds = 5 * 60;
constexpr qint64 noCandidateMissTtlSeconds = 7 * 24 * 60 * 60;

} // namespace

Resolver::Resolver(LyricStore &store, QList<Provider *> providers, bool filterCredits,
                   QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_providers(std::move(providers))
    , m_filterCredits(filterCredits)
{
}

std::optional<LyricDocument> Resolver::overridden(const TrackRef &ref) const
{
    for (auto *provider : m_providers) {
        if (!provider || !provider->isConfigured()) {
            continue;
        }
        if (auto document = provider->overrideFor(ref.provider, ref.trackId)) {
            return forDisplay(std::move(*document), ref);
        }
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
    const auto request = std::make_shared<Request>();
    request->generation = ++m_generation;
    request->state = state;
    qInfo().noquote() << QStringLiteral("resolve: fingerprint=%1 platform=%2 music=%3")
                             .arg(state.fingerprint,
                                  state.platform.isEmpty() ? QStringLiteral("unknown") : state.platform,
                                  state.music ? QStringLiteral("true") : QStringLiteral("false"));
    if (!state.music) {
        qInfo() << "state=filtered";
        finish(request, {QStringLiteral("filtered"), std::nullopt, {}});
        return;
    }
    if (const auto mapped = m_store.refForFingerprint(state.fingerprint)) {
        if (const auto override = overridden(*mapped)) {
            const QString resultState = override->lines.isEmpty()
                ? QStringLiteral("no-lyric") : QStringLiteral("ok");
            qInfo().noquote() << QStringLiteral("override hit: %1/%2 lines=%3")
                                     .arg(mapped->provider, mapped->trackId)
                                     .arg(override->lines.size());
            qInfo().noquote() << QStringLiteral("state=") + resultState;
            finish(request, {resultState, *mapped, *override});
            return;
        }
        if (const auto cached = m_store.lyric(*mapped)) {
            auto display = forDisplay(*cached, *mapped);
            const QString resultState = display.lines.isEmpty()
                ? QStringLiteral("no-lyric") : QStringLiteral("ok");
            qInfo().noquote() << QStringLiteral("cache hit: %1/%2 lines=%3")
                                     .arg(mapped->provider, mapped->trackId)
                                     .arg(display.lines.size());
            qInfo().noquote() << QStringLiteral("state=") + resultState;
            finish(request, {resultState, *mapped, display});
            return;
        }
        qInfo().noquote() << QStringLiteral("cache lyric missing: %1/%2")
                                 .arg(mapped->provider, mapped->trackId);
    } else {
        qInfo().noquote() << QStringLiteral("cache mapping missing: ") + state.fingerprint;
    }

    for (const auto &legacyId : legacyWaylyricsIds(state)) {
        const TrackRef legacy{QStringLiteral("waylyrics"), legacyId, 1.0};
        if (const auto imported = m_store.lyric(legacy)) {
            if (!m_store.mapFingerprint(state.fingerprint, legacy)) {
                qWarning().noquote() << QStringLiteral("cache map failed: fingerprint=%1 ref=%2/%3")
                                            .arg(state.fingerprint, legacy.provider, legacy.trackId);
            }
            auto display = forDisplay(*imported, legacy);
            const QString resultState = display.lines.isEmpty()
                ? QStringLiteral("no-lyric") : QStringLiteral("ok");
            qInfo().noquote() << QStringLiteral("cache hit: %1/%2 lines=%3")
                                     .arg(legacy.provider, legacy.trackId)
                                     .arg(display.lines.size());
            qInfo().noquote() << QStringLiteral("state=") + resultState;
            finish(request, {resultState, legacy, display});
            return;
        }
    }
    qInfo() << "cache miss";

    // A miss only suppresses another network lookup. Local overrides, normal
    // cache entries, and a cache imported after the miss must remain usable.
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (const auto miss = m_store.freshMiss(state.fingerprint, now,
                                            noCandidateMissTtlSeconds)) {
        const bool networkMiss = miss->reason == QStringLiteral("network");
        const qint64 ttlSeconds = networkMiss
            ? networkMissTtlSeconds : noCandidateMissTtlSeconds;
        const qint64 ageSeconds = now - miss->triedAt;
        if (ageSeconds < ttlSeconds) {
            qInfo().noquote() << QStringLiteral("fresh miss: reason=%1 age=%2s ttl=%3")
                                     .arg(miss->reason)
                                     .arg(ageSeconds)
                                     .arg(networkMiss ? QStringLiteral("300s")
                                                      : QStringLiteral("7d"));
            const QString resultState = networkMiss
                ? QStringLiteral("network-error") : QStringLiteral("not-found");
            qInfo().noquote() << QStringLiteral("state=") + resultState;
            finish(request, {resultState, std::nullopt, {}});
            return;
        }
    }

    request->query = {state.title, state.artists, state.album, state.lengthUs / 1000};
    continueWithProvider(request);
}

void Resolver::continueWithProvider(const std::shared_ptr<Request> &request)
{
    if (request->generation != m_generation) {
        return;
    }
    Provider *provider = nullptr;
    while (request->providerIndex < m_providers.size()) {
        auto *candidate = m_providers[request->providerIndex++];
        if (!candidate || !candidate->isConfigured() || !candidate->supportsSearch()) {
            continue;
        }
        provider = candidate;
        break;
    }
    if (!provider) {
        const QString missReason = request->networkFailed
            ? QStringLiteral("network") : QStringLiteral("no-candidate");
        if (m_store.recordMiss(request->state.fingerprint, missReason)) {
            qInfo().noquote() << QStringLiteral("record miss: reason=") + missReason;
        } else {
            qWarning().noquote() << QStringLiteral("cache miss record failed: fingerprint=%1 reason=%2")
                                        .arg(request->state.fingerprint, missReason);
        }
        const QString resultState = request->networkFailed
            ? QStringLiteral("network-error") : QStringLiteral("not-found");
        qInfo().noquote() << QStringLiteral("state=") + resultState;
        finish(request, {resultState, std::nullopt, {}});
        return;
    }

    const QPointer<Resolver> self(this);
    provider->search(request->query,
                     [self, request, provider](ProviderSearchResult result) mutable {
        if (!self || request->generation != self->m_generation) {
            return;
        }
        if (!result.error.isEmpty()) {
            qInfo().noquote() << QStringLiteral("search failed: %1: %2")
                                     .arg(provider->id(), result.error);
            request->networkFailed |= result.transportFailed;
            self->continueWithProvider(request);
            return;
        }
        qInfo().noquote() << explainMatch(request->query, result.candidates,
                                           request->state.platform == QStringLiteral("apple"));
        const auto ranked = rankCandidates(request->query, result.candidates);
        const auto chosen = chooseMatch(ranked,
                                        request->state.platform == QStringLiteral("apple"));
        if (!chosen) {
            self->continueWithProvider(request);
            return;
        }
        const TrackRef ref{provider->id(), chosen->candidate.trackId, chosen->score.total};
        provider->fetch(ref.trackId,
                        [self, request, ref](ProviderFetchResult result) mutable {
            if (!self || request->generation != self->m_generation) {
                return;
            }
            if (!result.document) {
                qInfo().noquote() << QStringLiteral("fetch failed: %1/%2: %3")
                                         .arg(ref.provider, ref.trackId,
                                              result.error.isEmpty()
                                                  ? QStringLiteral("unknown error") : result.error);
                request->networkFailed |= result.transportFailed;
                self->continueWithProvider(request);
                return;
            }
            qInfo().noquote() << QStringLiteral("fetched: lines=%1 hasWords=%2")
                                     .arg(result.document->lines.size())
                                     .arg(result.document->hasWords
                                              ? QStringLiteral("true") : QStringLiteral("false"));
            if (!self->m_store.putLyric(ref, *result.document)) {
                qWarning().noquote() << QStringLiteral("cache put failed: %1/%2")
                                            .arg(ref.provider, ref.trackId);
            }
            if (!self->m_store.mapFingerprint(request->state.fingerprint, ref)) {
                qWarning().noquote() << QStringLiteral("cache map failed: fingerprint=%1 ref=%2/%3")
                                            .arg(request->state.fingerprint, ref.provider, ref.trackId);
            }
            auto finalDocument = self->forDisplay(*result.document, ref);
            qInfo().noquote() << QStringLiteral("after filterLeadingCredits: lines=%1")
                                     .arg(finalDocument.lines.size());
            const QString resultState = finalDocument.lines.isEmpty()
                ? QStringLiteral("no-lyric") : QStringLiteral("ok");
            qInfo().noquote() << QStringLiteral("state=") + resultState;
            self->finish(request, {resultState, ref, finalDocument});
        });
    });
}

} // namespace PlasmaLyrics
