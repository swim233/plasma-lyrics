#include "matcher.h"

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
    for (const auto &rawArtist : artists) {
        for (const auto &part : rawArtist.split(separator, Qt::SkipEmptyParts)) {
            const auto normalized = normalizeSearchText(part);
            if (!normalized.isEmpty() && !result.contains(normalized)) {
                result.append(normalized);
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
    score.title = textSimilarity(normalizeSearchText(cleanTitle(query.title)),
                                 normalizeSearchText(cleanTitle(candidate.title)));
    score.artists = artistSimilarity(cleanArtists(query.artists), cleanArtists(candidate.artists));
    score.album = textSimilarity(normalizeSearchText(query.album), normalizeSearchText(candidate.album));
    score.durationDifferenceMs = query.lengthMs > 0 && candidate.lengthMs > 0
        ? qAbs(query.lengthMs - candidate.lengthMs)
        : 0;
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

QString explainMatch(const TrackQuery &query, const QList<Candidate> &candidates)
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
               << " deltaMs=" << item.score.durationDifferenceMs << '\n';
    }
    if (!ranked.isEmpty()) {
        stream << "selected: " << (isAcceptableMatch(ranked.first()) ? ranked.first().candidate.trackId
                                                                        : QStringLiteral("none"))
               << '\n';
    }
    return explanation;
}

} // namespace PlasmaLyrics

