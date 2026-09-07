#include "resolver.h"

#include "core/lyric/lrcparser.h"
#include "core/lyric/timeline.h"
#include "core/match/matcher.h"
#include "core/store/lyricstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>

namespace PlasmaLyrics {

Resolver::Resolver(LyricStore &store, QList<Provider *> providers, bool filterCredits)
    : m_store(store)
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

ResolvedLyric Resolver::resolve(const MprisState &state)
{
    qInfo().noquote() << QStringLiteral("resolve: fingerprint=%1 platform=%2 music=%3")
                             .arg(state.fingerprint,
                                  state.platform.isEmpty() ? QStringLiteral("unknown") : state.platform,
                                  state.music ? QStringLiteral("true") : QStringLiteral("false"));
    if (!state.music) {
        qInfo() << "state=filtered";
        return {QStringLiteral("filtered"), std::nullopt, {}};
    }
    if (const auto mapped = m_store.refForFingerprint(state.fingerprint)) {
        if (const auto override = overridden(*mapped)) {
            const QString resultState = override->lines.isEmpty()
                ? QStringLiteral("no-lyric") : QStringLiteral("ok");
            qInfo().noquote() << QStringLiteral("override hit: %1/%2 lines=%3")
                                     .arg(mapped->provider, mapped->trackId)
                                     .arg(override->lines.size());
            qInfo().noquote() << QStringLiteral("state=") + resultState;
            return {resultState, *mapped, *override};
        }
        if (const auto cached = m_store.lyric(*mapped)) {
            auto display = forDisplay(*cached, *mapped);
            const QString resultState = display.lines.isEmpty()
                ? QStringLiteral("no-lyric") : QStringLiteral("ok");
            qInfo().noquote() << QStringLiteral("cache hit: %1/%2 lines=%3")
                                     .arg(mapped->provider, mapped->trackId)
                                     .arg(display.lines.size());
            qInfo().noquote() << QStringLiteral("state=") + resultState;
            return {resultState, *mapped, display};
        }
    }

    for (const auto &legacyId : legacyWaylyricsIds(state)) {
        const TrackRef legacy{QStringLiteral("waylyrics"), legacyId, 1.0};
        if (const auto imported = m_store.lyric(legacy)) {
            m_store.mapFingerprint(state.fingerprint, legacy);
            auto display = forDisplay(*imported, legacy);
            const QString resultState = display.lines.isEmpty()
                ? QStringLiteral("no-lyric") : QStringLiteral("ok");
            qInfo().noquote() << QStringLiteral("cache hit: %1/%2 lines=%3")
                                     .arg(legacy.provider, legacy.trackId)
                                     .arg(display.lines.size());
            qInfo().noquote() << QStringLiteral("state=") + resultState;
            return {resultState, legacy, display};
        }
    }
    qInfo() << "cache miss";

    // A miss only suppresses another network lookup. Local overrides, normal
    // cache entries, and a cache imported after the miss must remain usable.
    constexpr qint64 missTtlSeconds = 7 * 24 * 60 * 60;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (const auto miss = m_store.freshMiss(state.fingerprint, now, missTtlSeconds)) {
        qInfo().noquote() << QStringLiteral("fresh miss: reason=%1 age=%2s ttl=7d")
                                 .arg(miss->reason)
                                 .arg(now - miss->triedAt);
        qInfo() << "state=not-found";
        return {QStringLiteral("not-found"), std::nullopt, {}};
    }

    const TrackQuery query{state.title, state.artists, state.album, state.lengthUs / 1000};
    bool networkFailed = false;
    for (auto *provider : m_providers) {
        if (!provider || !provider->isConfigured() || !provider->supportsSearch()) {
            continue;
        }
        const auto candidates = provider->search(query);
        qInfo().noquote() << explainMatch(query, candidates,
                                           state.platform == QStringLiteral("apple"));
        if (candidates.isEmpty() && !provider->lastError().isEmpty()) {
            networkFailed = true;
            continue;
        }
        const auto ranked = rankCandidates(query, candidates);
        const auto chosen = chooseMatch(ranked, state.platform == QStringLiteral("apple"));
        if (!chosen) {
            continue;
        }
        const TrackRef ref{provider->id(), chosen->candidate.trackId, chosen->score.total};
        const auto document = provider->fetch(ref.trackId);
        if (!document) {
            qInfo().noquote() << QStringLiteral("fetch failed: %1/%2: %3")
                                     .arg(ref.provider, ref.trackId, provider->lastError());
            networkFailed = true;
            continue;
        }
        qInfo().noquote() << QStringLiteral("fetched: lines=%1 hasWords=%2")
                                 .arg(document->lines.size())
                                 .arg(document->hasWords ? QStringLiteral("true") : QStringLiteral("false"));
        m_store.putLyric(ref, *document);
        m_store.mapFingerprint(state.fingerprint, ref);
        auto finalDocument = forDisplay(*document, ref);
        qInfo().noquote() << QStringLiteral("after filterLeadingCredits: lines=%1")
                                 .arg(finalDocument.lines.size());
        const QString resultState = finalDocument.lines.isEmpty()
            ? QStringLiteral("no-lyric") : QStringLiteral("ok");
        qInfo().noquote() << QStringLiteral("state=") + resultState;
        return {resultState, ref, finalDocument};
    }
    const QString missReason = networkFailed
        ? QStringLiteral("network") : QStringLiteral("no-candidate");
    qInfo().noquote() << QStringLiteral("record miss: reason=") + missReason;
    m_store.recordMiss(state.fingerprint, missReason);
    qInfo() << "state=not-found";
    return {QStringLiteral("not-found"), std::nullopt, {}};
}

} // namespace PlasmaLyrics
