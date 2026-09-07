#include "matcher.h"

#include <QHash>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>

namespace PlasmaLyrics {
namespace {

double textSimilarity(const QString &left, const QString &right)
{
    if (left == right) {
        return left.isEmpty() ? 0.0 : 1.0;
    }
    if (left.isEmpty() || right.isEmpty()) {
        return 0.0;
    }
    if (left.contains(right) || right.contains(left)) {
        return static_cast<double>(std::min(left.size(), right.size()))
            / static_cast<double>(std::max(left.size(), right.size()));
    }
    QList<int> previous(right.size() + 1);
    QList<int> current(right.size() + 1);
    for (qsizetype column = 0; column <= right.size(); ++column) {
        previous[column] = static_cast<int>(column);
    }
    for (qsizetype row = 1; row <= left.size(); ++row) {
        current[0] = static_cast<int>(row);
        for (qsizetype column = 1; column <= right.size(); ++column) {
            const int substitution = previous[column - 1] + (left[row - 1] == right[column - 1] ? 0 : 1);
            current[column] = std::min({previous[column] + 1, current[column - 1] + 1, substitution});
        }
        previous.swap(current);
    }
    return 1.0 - static_cast<double>(previous.last()) / static_cast<double>(std::max(left.size(), right.size()));
}

// D-11: a candidate that only cleared score.title through an alternate title
// (netease transNames propagate to covers/remixes of the same song) must
// also clear a much lower artist bar than the D-8 fallback's 0.9 -- the
// title evidence here is strong, this bar only needs to reject "unrelated
// person", see DESIGN.md decision 45.
bool passesAliasArtistGate(const RankedCandidate &candidate)
{
    return !candidate.score.titleViaAlternate || candidate.score.artists >= 0.5;
}

double artistSimilarity(const QStringList &left, const QStringList &right)
{
    if (left.isEmpty() || right.isEmpty()) {
        return 0.0;
    }
    double sum = 0;
    for (const auto &artist : left) {
        double best = 0;
        for (const auto &candidate : right) {
            best = std::max(best, textSimilarity(artist, candidate));
        }
        sum += best;
    }
    return sum / static_cast<double>(left.size());
}

} // namespace

QString normalizeSearchText(QString text)
{
    text = text.normalized(QString::NormalizationForm_KC).toCaseFolded();
    text.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")), QStringLiteral(" "));
    return text.simplified();
}

QString cleanTitle(QString title)
{
    static const QRegularExpression versionSuffix(
        QStringLiteral(R"(\s*[\(（\[【].{0,30}(伴奏|纯音乐|live|版|ver\.?|cover|remix).{0,20}[\)）\]】]\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    title.remove(versionSuffix);
    return title.simplified();
}

QStringList cleanArtists(const QStringList &artists)
{
    QStringList result;
    static const QRegularExpression separator(QStringLiteral(R"(\s*(?:/|／|、|;|；|\||&|feat\.?|ft\.?)\s*)"),
                                              QRegularExpression::CaseInsensitiveOption);
    // netease commonly stores one performer as "primary name (native-script
    // alias)", e.g. "BTS (防弹少年团)" -- that trailing group names the same
    // person/group under a different script, not a second collaborator, but
    // normalizeSearchText would otherwise fold both scripts into one blob
    // that matches neither a bare "BTS" query nor a bare "防弹少年团" one as
    // well as either name alone would (see DESIGN.md decision 44).
    static const QRegularExpression trailingAlias(QStringLiteral(R"(^(.+?)[\(（]([^()（）]+)[\)）]\s*$)"));
    for (const auto &rawArtist : artists) {
        QStringList expanded;
        if (const auto match = trailingAlias.match(rawArtist); match.hasMatch()) {
            expanded.append(match.captured(1));
            expanded.append(match.captured(2));
        } else {
            expanded.append(rawArtist);
        }
        for (const auto &name : expanded) {
            for (const auto &part : name.split(separator, Qt::SkipEmptyParts)) {
                const auto normalized = normalizeSearchText(part);
                if (!normalized.isEmpty() && !result.contains(normalized)) {
                    result.append(normalized);
                }
            }
        }
    }
    return result;
}

QString searchKeywords(const TrackQuery &query)
{
    QStringList parts{cleanTitle(query.title)};
    parts.append(query.artists);
    if (!query.album.trimmed().isEmpty() && normalizeSearchText(query.album) != normalizeSearchText(query.title)) {
        parts.append(query.album);
    }
    return parts.join(QLatin1Char(' ')).simplified();
}

ScoreBreakdown scoreCandidate(const TrackQuery &query, const Candidate &candidate)
{
    ScoreBreakdown score;
    const QString normalizedQuery = normalizeSearchText(cleanTitle(query.title));
    score.title = textSimilarity(normalizedQuery, normalizeSearchText(cleanTitle(candidate.title)));
    // Alternate titles (e.g. netease transNames) are extra evidence, not a
    // lowered bar: they can only raise score.title, by the same textSimilarity
    // used for the primary title, and only the best of all of them counts.
    for (const auto &alternate : candidate.alternateTitles) {
        const double alternateScore = textSimilarity(normalizedQuery, normalizeSearchText(cleanTitle(alternate)));
        if (alternateScore > score.title) {
            score.title = alternateScore;
            score.titleViaAlternate = true;
        }
    }
    score.artists = artistSimilarity(cleanArtists(query.artists), cleanArtists(candidate.artists));
    score.album = textSimilarity(normalizeSearchText(query.album), normalizeSearchText(candidate.album));
    score.durationComparable = query.lengthMs > 0 && candidate.lengthMs > 0;
    score.durationDifferenceMs = score.durationComparable ? qAbs(query.lengthMs - candidate.lengthMs) : 0;
    if (query.lengthMs <= 0 || candidate.lengthMs <= 0) {
        score.duration = 0.5;
    } else if (score.durationDifferenceMs <= 2000) {
        score.duration = 1.0 - static_cast<double>(score.durationDifferenceMs) / 20000.0;
    } else {
        score.duration = std::max(0.0, 0.9 - static_cast<double>(score.durationDifferenceMs - 2000) / 15000.0);
    }
    score.total = score.title * 0.5 + score.artists * 0.2 + score.album * 0.1 + score.duration * 0.2;
    return score;
}

QList<RankedCandidate> rankCandidates(const TrackQuery &query, const QList<Candidate> &candidates)
{
    QList<RankedCandidate> ranked;
    ranked.reserve(candidates.size());
    for (const auto &candidate : candidates) {
        ranked.append({candidate, scoreCandidate(query, candidate)});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &left, const auto &right) {
        if (left.score.total != right.score.total) {
            return left.score.total > right.score.total;
        }
        return left.score.durationDifferenceMs < right.score.durationDifferenceMs;
    });
    return ranked;
}

bool isAcceptableMatch(const RankedCandidate &candidate)
{
    return candidate.score.title >= 0.55 && candidate.score.total >= 0.58;
}

std::optional<RankedCandidate> chooseMatch(const QList<RankedCandidate> &ranked, bool allowLocalizedFallback)
{
    if (ranked.isEmpty()) {
        return std::nullopt;
    }
    if (isAcceptableMatch(ranked.first()) && passesAliasArtistGate(ranked.first())) {
        return ranked.first();
    }
    if (!allowLocalizedFallback) {
        return std::nullopt;
    }
    // A pool of one or two candidates makes "unique" free -- exactly the case
    // where the evidence is weakest (D-8).
    if (ranked.size() < 3) {
        return std::nullopt;
    }
    QList<RankedCandidate> survivors;
    for (const auto &item : ranked) {
        // 250ms, not scoreCandidate's 2000ms "best tier" -- this is the
        // fallback's own uniqueness window, tightened separately (measured
        // collision rate, DESIGN.md decision 45): known genuine matches sit
        // at Delta<=1ms (Apple and netease releases agree to the
        // millisecond), while a same-artist neighbor lands inside a 2000ms
        // window often enough to get mistaken for the real song once the
        // real song itself has been filtered out by this same gate.
        if (item.score.artists >= 0.9 && item.score.durationComparable && item.score.durationDifferenceMs <= 250) {
            survivors.append(item);
        }
    }
    if (survivors.isEmpty()) {
        return std::nullopt;
    }
    // Netease routinely lists the same song under several track ids; dedupe
    // by (title, artists) before counting "unique" (D-9), otherwise a
    // genuine match gets rejected as ambiguous against itself.
    QHash<QString, QList<RankedCandidate>> groups;
    for (const auto &item : survivors) {
        const QString key = normalizeSearchText(cleanTitle(item.candidate.title)) + QLatin1Char('\x1f')
            + cleanArtists(item.candidate.artists).join(QLatin1Char('/'));
        groups[key].append(item);
    }
    if (groups.size() != 1) {
        return std::nullopt;
    }
    const auto &group = groups.constBegin().value();
    const auto best = std::min_element(group.begin(), group.end(), [](const auto &left, const auto &right) {
        return left.score.durationDifferenceMs < right.score.durationDifferenceMs;
    });
    // No passesAliasArtistGate call here: every survivor already cleared
    // artists >= 0.9 above, which is strictly stricter than the alias gate's
    // 0.5 -- calling it on *best would be dead code (qa-1).
    return *best;
}

QString explainMatch(const TrackQuery &query, const QList<Candidate> &candidates,
                     bool allowLocalizedFallback, bool platformKnown)
{
    QString explanation;
    QTextStream stream(&explanation);
    stream << "raw title: " << query.title << '\n'
           << "raw artists: " << query.artists.join(QStringLiteral(" / ")) << '\n'
           << "clean title: " << cleanTitle(query.title) << '\n'
           << "clean artists: " << cleanArtists(query.artists).join(QStringLiteral(" / ")) << '\n'
           << "keywords: " << searchKeywords(query) << '\n';
    const auto ranked = rankCandidates(query, candidates);
    for (qsizetype index = 0; index < ranked.size(); ++index) {
        const auto &item = ranked[index];
        stream << index + 1 << ". [" << item.candidate.trackId << "] " << item.candidate.title
               << " — " << item.candidate.artists.join(QStringLiteral(" / "))
               << " total=" << QString::number(item.score.total, 'f', 3)
               << " title=" << QString::number(item.score.title, 'f', 3)
               << " artists=" << QString::number(item.score.artists, 'f', 3)
               << " album=" << QString::number(item.score.album, 'f', 3)
               << " duration=" << QString::number(item.score.duration, 'f', 3)
               << " deltaMs=" << item.score.durationDifferenceMs
               << " titleVia=" << (item.score.titleViaAlternate ? QStringLiteral("alias") : QStringLiteral("title"))
               << '\n';
    }
    const auto fallbackChoice = chooseMatch(ranked, true);
    if (platformKnown) {
        const auto chosen = chooseMatch(ranked, allowLocalizedFallback);
        stream << "selected: " << (chosen ? chosen->candidate.trackId : QStringLiteral("none")) << '\n';
        const QString chosenId = chosen ? chosen->candidate.trackId : QString();
        const QString fallbackId = fallbackChoice ? fallbackChoice->candidate.trackId : QString();
        if (chosenId != fallbackId) {
            stream << "would-select-with-fallback: "
                   << (fallbackChoice ? fallbackChoice->candidate.trackId : QStringLiteral("none")) << '\n';
        }
    } else {
        stream << "would-select-with-fallback: "
               << (fallbackChoice ? fallbackChoice->candidate.trackId : QStringLiteral("none")) << '\n';
    }
    return explanation;
}

} // namespace PlasmaLyrics
