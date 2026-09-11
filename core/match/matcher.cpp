#include "matcher.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <algorithm>

namespace PlasmaLyrics {
namespace {

struct VersionEvidence {
    QSet<QString> markers;
    QSet<QString> years;
    QSet<QString> editions;

    bool isExplicit() const { return !markers.isEmpty(); }
};

VersionEvidence versionEvidence(const QString &title)
{
    const QString normalized = title.normalized(QString::NormalizationForm_KC).toCaseFolded();
    struct MarkerPattern {
        const char *marker;
        const char *pattern;
    };
    static const MarkerPattern patterns[]{
        {"live", R"(\blive\b|现场|演唱会|ライブ)"},
        {"remix", R"(\bremix(?:ed)?\b|混音|リミックス)"},
        {"cover", R"(\bcover\b|翻唱|カバー)"},
        {"remaster", R"(\bremaster(?:ed)?\b|重制|リマスター)"},
        {"instrumental", R"(\binstrumental\b|\boff[ -]?vocal\b|伴奏|纯音乐|純音樂|インスト)"},
        {"acoustic", R"(\bacoustic\b|不插电|不插電|アコースティック)"},
        {"edit", R"(\bradio edit\b|\bsingle edit\b|\bedit\b)"},
        {"edition", R"(\bedition\b|\bdeluxe\b|\bexpanded\b|\blimited\b|\bcollector'?s?\b|\banniversary\b|特别版|特別版|豪华版|豪華版|扩展版|擴展版|限定版|珍藏版|周年(?:纪念|紀念|記念)?版|エディション)"},
    };
    VersionEvidence result;
    for (const auto &item : patterns) {
        if (normalized.contains(QRegularExpression(QString::fromUtf8(item.pattern),
                                                   QRegularExpression::CaseInsensitiveOption))) {
            result.markers.insert(QString::fromLatin1(item.marker));
        }
    }
    if (!result.isExplicit()) {
        return result;
    }

    // Years and edition names qualify an explicit version marker. They are
    // deliberately not treated as versions on their own: a year in an
    // otherwise plain song title is commonly part of the title itself.
    static const QRegularExpression yearPattern(
        QStringLiteral(R"((?<!\d)((?:19|20)\d{2})(?!\d))"));
    auto years = yearPattern.globalMatch(normalized);
    while (years.hasNext()) {
        result.years.insert(years.next().captured(1));
    }
    struct EditionPattern {
        const char *edition;
        const char *pattern;
    };
    static const EditionPattern editionPatterns[]{
        {"deluxe", R"(\bdeluxe(?: edition)?\b|豪华版|豪華版|デラックス(?:・)?エディション)"},
        {"special", R"(\bspecial(?: edition)?\b|特别版|特別版|スペシャル(?:・)?エディション)"},
        {"expanded", R"(\bexpanded(?: edition)?\b|扩展版|擴展版)"},
        {"limited", R"(\blimited(?: edition)?\b|限定版|リミテッド(?:・)?エディション)"},
        {"collector", R"(\bcollector'?s?(?: edition)?\b|珍藏版|コレクターズ(?:・)?エディション)"},
        {"anniversary", R"(\b(?:\d+(?:st|nd|rd|th)\s+)?anniversary(?: edition)?\b|周年(?:纪念|紀念|記念)?版)"},
    };
    for (const auto &item : editionPatterns) {
        if (normalized.contains(QRegularExpression(QString::fromUtf8(item.pattern),
                                                   QRegularExpression::CaseInsensitiveOption))) {
            result.editions.insert(QString::fromLatin1(item.edition));
        }
    }
    static const QRegularExpression anniversaryOrdinalPattern(
        QString::fromUtf8(
            R"((?<!\d)(\d{1,3})(?:(?:st|nd|rd|th)\s+anniversary\b|\s*周年(?:纪念|紀念|記念)?(?:版|エディション)?))"),
        QRegularExpression::CaseInsensitiveOption);
    auto anniversaryOrdinals = anniversaryOrdinalPattern.globalMatch(normalized);
    while (anniversaryOrdinals.hasNext()) {
        const auto match = anniversaryOrdinals.next();
        result.editions.insert(QStringLiteral("anniversary:")
                               + QString::number(match.captured(1).toUInt()));
    }
    return result;
}

bool hasIntersection(const QSet<QString> &left, const QSet<QString> &right)
{
    for (const auto &value : left) {
        if (right.contains(value)) return true;
    }
    return false;
}

bool conflictingEvidence(const VersionEvidence &query, const VersionEvidence &candidate)
{
    if (query.markers != candidate.markers) return true;
    if (!query.years.isEmpty() && !candidate.years.isEmpty()
        && !hasIntersection(query.years, candidate.years)) {
        return true;
    }
    const auto anniversaryOrdinals = [](const QSet<QString> &editions) {
        QSet<QString> result;
        for (const auto &edition : editions) {
            if (edition.startsWith(QStringLiteral("anniversary:"))) {
                result.insert(edition);
            }
        }
        return result;
    };
    const auto queryAnniversaryOrdinals = anniversaryOrdinals(query.editions);
    const auto candidateAnniversaryOrdinals = anniversaryOrdinals(candidate.editions);
    if (!queryAnniversaryOrdinals.isEmpty() && !candidateAnniversaryOrdinals.isEmpty()
        && !hasIntersection(queryAnniversaryOrdinals, candidateAnniversaryOrdinals)) {
        return true;
    }
    return !query.editions.isEmpty() && !candidate.editions.isEmpty()
        && !hasIntersection(query.editions, candidate.editions);
}

VersionTier classifyCandidateVersions(const QString &queryTitle,
                                      const QStringList &candidateTitles)
{
    const auto query = versionEvidence(queryTitle);
    QList<VersionEvidence> explicitCandidates;
    for (const auto &title : candidateTitles) {
        auto evidence = versionEvidence(title);
        if (evidence.isExplicit()) explicitCandidates.append(std::move(evidence));
    }
    if (!query.isExplicit() && explicitCandidates.isEmpty()) {
        return VersionTier::Normal;
    }
    if (!query.isExplicit() || explicitCandidates.isEmpty()) {
        return VersionTier::OneSided;
    }
    // Every explicit title is evidence about the recording. An unversioned
    // localized alias may improve title similarity, but it cannot erase a
    // Live/Remix or 2020/2021 conflict present in another title.
    return std::any_of(explicitCandidates.cbegin(), explicitCandidates.cend(),
                       [&query](const auto &candidate) {
                           return conflictingEvidence(query, candidate);
                       })
        ? VersionTier::Conflict : VersionTier::Normal;
}

int tierOrder(VersionTier tier)
{
    switch (tier) {
    case VersionTier::Normal: return 0;
    case VersionTier::OneSided: return 1;
    case VersionTier::Conflict: return 2;
    }
    return 2;
}

QString titleForPolicy(const QString &title, MatchPolicy policy)
{
    return policy == MatchPolicy::PreserveVersions ? title.simplified() : cleanTitle(title);
}

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

// The 2000ms window scoreCandidate's own duration curve already treats as
// "best tier" (decision 12; see the first branch of score.duration below)
// -- reused here, rather than a new number, so this gate and that curve
// agree on what "close enough" means.
constexpr qint64 kGlossVariantDurationWindowMs = 2000;

// A gloss-stripped query variant (see splitTrailingGloss) can make the
// candidate-side title match a *shortened* form of the query just as
// easily as it rescues a genuinely translated title: "Song (Intro)" and a
// same-artist "Intro" track (a different, much shorter recording) score
// identically well through the stripped variant "Song". Duration is the
// only independent evidence left to tell the two apart, so a title win
// that came through a gloss variant is only accepted within this window.
// Unlike passesAliasArtistGate's artist bar (D-11), an unknown/missing
// duration cannot pass here: DESIGN.md decision 45's user preference is
// "no lyrics rather than wrong lyrics", and an Intro/Interlude/Outro/Skit
// collision is common enough (frequent album-track names) that erring
// toward rejection is the safer default.
bool passesGlossVariantGate(const RankedCandidate &candidate)
{
    return !candidate.score.titleViaGlossVariant
        || (candidate.score.durationComparable
            && candidate.score.durationDifferenceMs <= kGlossVariantDurationWindowMs);
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

// The marker words cleanTitle's own trailing-bracket regex looks for. A
// bare "版" or "ver"/"ver." is common enough on Chinese- and
// English-market titles that cleanTitle already treats it as a version
// marker, but versionEvidence()'s own, richer table does not carry either
// one on its own (only compound forms like "特别版", "周年...版"). Shared
// as a fragment, rather than copied, so cleanTitle and splitTrailingGloss's
// guard below can never quietly drift onto two different definitions of
// "version marker".
QString cleanTitleVersionMarkerPattern()
{
    return QStringLiteral(R"(伴奏|纯音乐|live|版|ver\.?|cover|remix)");
}

// Whether a query title variant is the original, un-stripped title or a
// gloss-stripped one travels with the string itself here, rather than
// being inferred from its position in a list: a caller that reordered or
// filtered that list would otherwise silently make passesGlossVariantGate
// misclassify a gloss-stripped match as the original title (and vice
// versa), quietly reopening the exact hole that gate exists to close.
struct QueryTitleVariant {
    QString text;
    bool isGlossStripped = false;
};

// The query side may carry a bracketed localized gloss that the provider
// candidate's title doesn't, so title matching also tries the query with
// that gloss removed and keeps whichever variant scores highest against a
// given candidate title. This can only raise score.title, never lower it
// -- same reasoning as alternateTitles on the candidate side (D-11) -- so
// no previously-accepted match is put at risk by adding this variant.
QList<QueryTitleVariant> queryTitleVariants(const QString &title, MatchPolicy policy)
{
    QList<QueryTitleVariant> variants{{titleForPolicy(title, policy), false}};
    QString main;
    if (splitTrailingGloss(title, &main) && !main.isEmpty()) {
        variants.append({titleForPolicy(main, policy), true});
    }
    return variants;
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
        QStringLiteral(R"(\s*[\(（\[【].{0,30}()") + cleanTitleVersionMarkerPattern()
            + QStringLiteral(R"().{0,20}[\)）\]】]\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    title.remove(versionSuffix);
    return title.simplified();
}

// A trailing parenthetical whose contents carry no known version marker is
// read as a same-song localized alias/gloss (e.g. the literal title
// "青さは止んだ (青春已逝)", where the parenthesized text is just the
// Mandarin storefront's translation of the Japanese title before it), not
// evidence of a different recording. A trailing parenthetical that DOES
// carry a version marker is left alone here: cleanTitle already strips
// those under MatchPolicy::Default (versions collapse together there by
// design), and MatchPolicy::PreserveVersions must keep them intact --
// decision 54's version distinction depends on it. "Version marker" here
// is deliberately the union of versionEvidence()'s table and cleanTitle's
// own ("版"/"ver" are not in versionEvidence()'s table -- see above): a
// word missing from one but not the other must never be misread as safe
// to strip. Erring toward not stripping only ever regresses to today's
// not-found; erring toward stripping can select the wrong version, so the
// guard is deliberately the more conservative of the two directions.
bool splitTrailingGloss(const QString &title, QString *main)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^(.*?)\s*[\(（\[【]([^()（）\[\]【】]{1,60})[\)）\]】]\s*$)"));
    static const QRegularExpression cleanTitleMarker(cleanTitleVersionMarkerPattern(),
                                                     QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(title);
    if (!match.hasMatch()) {
        return false;
    }
    const QString bracketContent = match.captured(2);
    if (versionEvidence(bracketContent).isExplicit() || bracketContent.contains(cleanTitleMarker)) {
        return false;
    }
    if (main) *main = match.captured(1).trimmed();
    return true;
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
    // A bracketed localized gloss (see splitTrailingGloss) is query-side
    // noise for a provider search: it isn't part of the recording's own
    // title, so leaving it in the "s=" search string can drag in unrelated
    // results. This is a secondary cleanup, not what rescues the bilingual
    // title match itself -- the provider already returns the right
    // candidate either way; scoreCandidate's own query variants are what
    // let it be recognized as such.
    QString main;
    QString title = query.title;
    if (splitTrailingGloss(query.title, &main) && !main.isEmpty()) {
        title = main;
    }
    const QString cleanedTitle = cleanTitle(title);
    QStringList parts{cleanedTitle};
    parts.append(query.artists);
    // Compare against cleanedTitle -- the title actually placed in parts
    // above -- not the raw query.title: an album equal to the *stripped*
    // title (as in the bug report's own "青さは止んだ" album tag) must not
    // be added a second time just because it differs from the raw,
    // gloss-still-attached query.title.
    if (!query.album.trimmed().isEmpty() && normalizeSearchText(query.album) != normalizeSearchText(cleanedTitle)) {
        parts.append(query.album);
    }
    return parts.join(QLatin1Char(' ')).simplified();
}

ScoreBreakdown scoreCandidate(const TrackQuery &query, const Candidate &candidate,
                              MatchPolicy policy)
{
    ScoreBreakdown score;
    score.versionPolicyApplied = policy == MatchPolicy::PreserveVersions;
    QList<QueryTitleVariant> normalizedQueryVariants;
    for (const auto &variant : queryTitleVariants(query.title, policy)) {
        normalizedQueryVariants.append({normalizeSearchText(variant.text), variant.isGlossStripped});
    }
    QStringList candidateTitles{candidate.title};
    candidateTitles.append(candidate.alternateTitles);
    score.versionTier = policy == MatchPolicy::PreserveVersions
        ? classifyCandidateVersions(query.title, candidateTitles)
        : VersionTier::Normal;
    auto considerTitle = [&](const QString &title, bool alternate) {
        const QString normalizedCandidateTitle = normalizeSearchText(titleForPolicy(title, policy));
        for (const auto &queryVariant : normalizedQueryVariants) {
            const double titleScore = textSimilarity(queryVariant.text, normalizedCandidateTitle);
            if (titleScore > score.title) {
                score.title = titleScore;
                score.titleViaAlternate = alternate;
                score.titleViaGlossVariant = queryVariant.isGlossStripped;
            }
        }
    };
    considerTitle(candidate.title, false);
    // Alternate titles (e.g. netease transNames) are extra evidence, not a
    // lowered bar: they can only raise score.title, by the same textSimilarity
    // used for the primary title, and only the best of all of them counts.
    for (const auto &alternate : candidate.alternateTitles) {
        considerTitle(alternate, true);
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
    if (score.versionTier == VersionTier::Conflict) {
        score.rejectionReason = QStringLiteral("version-conflict");
    }
    return score;
}

QList<RankedCandidate> rankCandidates(const TrackQuery &query, const QList<Candidate> &candidates,
                                      MatchPolicy policy)
{
    QList<RankedCandidate> ranked;
    ranked.reserve(candidates.size());
    for (const auto &candidate : candidates) {
        ranked.append({candidate, scoreCandidate(query, candidate, policy)});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &left, const auto &right) {
        if (left.score.versionTier != right.score.versionTier) {
            return tierOrder(left.score.versionTier) < tierOrder(right.score.versionTier);
        }
        if (left.score.total != right.score.total) {
            return left.score.total > right.score.total;
        }
        return left.score.durationDifferenceMs < right.score.durationDifferenceMs;
    });
    return ranked;
}

bool isAcceptableMatch(const RankedCandidate &candidate)
{
    return candidate.score.versionTier != VersionTier::Conflict
        && candidate.score.title >= 0.55 && candidate.score.total >= 0.58
        && passesGlossVariantGate(candidate);
}

std::optional<RankedCandidate> chooseMatch(const QList<RankedCandidate> &ranked, bool allowLocalizedFallback)
{
    if (ranked.isEmpty()) {
        return std::nullopt;
    }
    if (ranked.first().score.versionPolicyApplied) {
        for (const auto tier : {VersionTier::Normal, VersionTier::OneSided}) {
            for (const auto &candidate : ranked) {
                if (candidate.score.versionTier == tier
                    && isAcceptableMatch(candidate) && passesAliasArtistGate(candidate)) {
                    return candidate;
                }
            }
        }
    } else if (isAcceptableMatch(ranked.first()) && passesAliasArtistGate(ranked.first())) {
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
        if (item.score.versionPolicyApplied
            && item.score.versionTier == VersionTier::Conflict) {
            continue;
        }
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
    const auto groupIt = groups.constBegin();
    const auto &group = groupIt.value();
    const auto best = std::min_element(group.begin(), group.end(), [](const auto &left, const auto &right) {
        return left.score.durationDifferenceMs < right.score.durationDifferenceMs;
    });
    // No passesAliasArtistGate call here: every survivor already cleared
    // artists >= 0.9 above, which is strictly stricter than the alias gate's
    // 0.5 -- calling it on *best would be dead code (qa-1).
    return *best;
}

QString candidateRejectionReason(const RankedCandidate &candidate)
{
    if (!candidate.score.rejectionReason.isEmpty()) {
        return candidate.score.rejectionReason;
    }
    if (candidate.score.title < 0.55) {
        return QStringLiteral("title-threshold");
    }
    if (candidate.score.total < 0.58) {
        return QStringLiteral("total-threshold");
    }
    if (!passesAliasArtistGate(candidate)) {
        return QStringLiteral("alias-artist-threshold");
    }
    if (!passesGlossVariantGate(candidate)) {
        // Two different situations, two different reasons (DESIGN.md
        // decision 33: diagnostics must point at an actionable next step).
        // A known duration outside the window is very likely a different,
        // unrelated recording -- no action fixes that. An unknown duration
        // (a local .lrc with no [length:] tag, or any other provider that
        // can't supply one) is something the user could resolve by adding
        // one, so it gets its own, more hopeful reason string.
        return candidate.score.durationComparable
            ? QStringLiteral("gloss-duration-threshold")
            : QStringLiteral("gloss-duration-unknown");
    }
    return QString();
}

QString explainMatch(const TrackQuery &query, const QList<Candidate> &candidates,
                     bool allowLocalizedFallback, bool platformKnown, MatchPolicy policy)
{
    QString explanation;
    QTextStream stream(&explanation);
    // Every query-side title actually tried against candidates (see
    // queryTitleVariants) -- not just cleanTitle's output, which is only
    // one of them under MatchPolicy::Default and not even that under
    // PreserveVersions. A diagnostic that showed cleanTitle unconditionally
    // here would silently disagree with what scoreCandidate actually
    // scored, which is exactly the failure mode this line exists to rule
    // out (DESIGN.md decision 46).
    QStringList titleVariantTexts;
    for (const auto &variant : queryTitleVariants(query.title, policy)) {
        titleVariantTexts.append(variant.text);
    }
    stream << "raw title: " << query.title << '\n'
           << "raw artists: " << query.artists.join(QStringLiteral(" / ")) << '\n'
           << "title variants: " << titleVariantTexts.join(QStringLiteral(" | ")) << '\n'
           << "clean artists: " << cleanArtists(query.artists).join(QStringLiteral(" / ")) << '\n'
           << "keywords: " << searchKeywords(query) << '\n';
    // titleViaAlternate and titleViaGlossVariant are independent (one is
    // about which candidate-side title won, the other which query-side
    // variant won) and can both be true at once -- a gloss-stripped query
    // variant beating an alternateTitle -- so this must be able to show
    // either, neither, or both, unambiguously.
    auto titleViaLabel = [](const ScoreBreakdown &score) {
        if (score.titleViaAlternate && score.titleViaGlossVariant) {
            return QStringLiteral("alias+gloss");
        }
        if (score.titleViaGlossVariant) {
            return QStringLiteral("gloss");
        }
        if (score.titleViaAlternate) {
            return QStringLiteral("alias");
        }
        return QStringLiteral("title");
    };
    const auto ranked = rankCandidates(query, candidates, policy);
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
               << " titleVia=" << titleViaLabel(item.score)
               << " versionTier="
               << (item.score.versionTier == VersionTier::Normal
                       ? QStringLiteral("normal")
                       : item.score.versionTier == VersionTier::OneSided
                           ? QStringLiteral("one-sided") : QStringLiteral("conflict"));
        // Single source of truth for the classification, shared with
        // Resolver's "search ... rejected=" log field -- keeps the two from
        // drifting apart (see candidateRejectionReason()).
        if (const QString rejectionReason = candidateRejectionReason(item); !rejectionReason.isEmpty()) {
            stream << " rejected=" << rejectionReason;
        }
        stream << '\n';
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
