#include "resolver.h"

#include "core/lyric/lrcparser.h"
#include "core/lyric/timeline.h"
#include "core/match/matcher.h"
#include "core/store/lyricstore.h"

#include <QCryptographicHash>

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
    if (!state.music) {
        return {QStringLiteral("filtered"), std::nullopt, {}};
    }
    if (const auto mapped = m_store.refForFingerprint(state.fingerprint)) {
        if (const auto override = overridden(*mapped)) {
            return {override->lines.isEmpty() ? QStringLiteral("no-lyric") : QStringLiteral("ok"), *mapped, *override};
        }
        if (const auto cached = m_store.lyric(*mapped)) {
            auto display = forDisplay(*cached, *mapped);
            return {display.lines.isEmpty() ? QStringLiteral("no-lyric") : QStringLiteral("ok"), *mapped, display};
        }
    }

    for (const auto &legacyId : legacyWaylyricsIds(state)) {
        const TrackRef legacy{QStringLiteral("waylyrics"), legacyId, 1.0};
        if (const auto imported = m_store.lyric(legacy)) {
            m_store.mapFingerprint(state.fingerprint, legacy);
            auto display = forDisplay(*imported, legacy);
            return {display.lines.isEmpty() ? QStringLiteral("no-lyric") : QStringLiteral("ok"), legacy, display};
        }
    }

    // A miss only suppresses another network lookup. Local overrides, normal
    // cache entries, and a cache imported after the miss must remain usable.
    if (m_store.hasFreshMiss(state.fingerprint)) {
        return {QStringLiteral("not-found"), std::nullopt, {}};
    }

    const TrackQuery query{state.title, state.artists, state.album, state.lengthUs / 1000};
    bool networkFailed = false;
    for (auto *provider : m_providers) {
        if (!provider || !provider->isConfigured() || !provider->supportsSearch()) {
            continue;
        }
        const auto candidates = provider->search(query);
        qInfo().noquote() << explainMatch(query, candidates);
        if (candidates.isEmpty() && !provider->lastError().isEmpty()) {
            networkFailed = true;
            continue;
        }
        const auto ranked = rankCandidates(query, candidates);
        if (ranked.isEmpty() || !isAcceptableMatch(ranked.first())) {
            continue;
        }
        const TrackRef ref{provider->id(), ranked.first().candidate.trackId, ranked.first().score.total};
        const auto document = provider->fetch(ref.trackId);
        if (!document) {
            networkFailed = true;
            continue;
        }
        m_store.putLyric(ref, *document);
        m_store.mapFingerprint(state.fingerprint, ref);
        auto finalDocument = forDisplay(*document, ref);
        return {finalDocument.lines.isEmpty() ? QStringLiteral("no-lyric") : QStringLiteral("ok"), ref, finalDocument};
    }
    m_store.recordMiss(state.fingerprint, networkFailed
                           ? QStringLiteral("network")
                           : QStringLiteral("no-candidate"));
    return {QStringLiteral("not-found"), std::nullopt, {}};
}

} // namespace PlasmaLyrics
