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
        QRegularExpression pattern;
    };
    // The QRegularExpression objects themselves are now built once (magic
    // static, thread-safe per C++11 -- the same guarantee cleanTitle's own
    // static const QRegularExpression already relies on), not reconstructed
    // on every call: this function runs once per candidate title under
    // MatchPolicy::PreserveVersions (via classifyCandidateVersions) and,
    // since splitTrailingGloss started calling it too, once per query
    // under MatchPolicy::Default as well -- compiling 8 patterns here plus
    // 6 more below on every one of those calls measurably slowed both
    // paths. The pattern text itself is untouched, character for character.
    static const MarkerPattern patterns[]{
        {"live", QRegularExpression(QStringLiteral(R"(\blive\b|现场|演唱会|ライブ)"),
                                    QRegularExpression::CaseInsensitiveOption)},
        {"remix", QRegularExpression(QStringLiteral(R"(\bremix(?:ed)?\b|混音|リミックス)"),
                                     QRegularExpression::CaseInsensitiveOption)},
        {"cover", QRegularExpression(QStringLiteral(R"(\bcover\b|翻唱|カバー)"),
                                     QRegularExpression::CaseInsensitiveOption)},
        {"remaster", QRegularExpression(QStringLiteral(R"(\bremaster(?:ed)?\b|重制|リマスター)"),
                                        QRegularExpression::CaseInsensitiveOption)},
        {"instrumental", QRegularExpression(QStringLiteral(R"(\binstrumental\b|\boff[ -]?vocal\b|伴奏|纯音乐|純音樂|インスト)"),
                                            QRegularExpression::CaseInsensitiveOption)},
        {"acoustic", QRegularExpression(QStringLiteral(R"(\bacoustic\b|不插电|不插電|アコースティック)"),
                                        QRegularExpression::CaseInsensitiveOption)},
        {"edit", QRegularExpression(QStringLiteral(R"(\bradio edit\b|\bsingle edit\b|\bedit\b)"),
                                    QRegularExpression::CaseInsensitiveOption)},
        {"edition", QRegularExpression(QStringLiteral(R"(\bedition\b|\bdeluxe\b|\bexpanded\b|\blimited\b|\bcollector'?s?\b|\banniversary\b|特别版|特別版|豪华版|豪華版|扩展版|擴展版|限定版|珍藏版|周年(?:纪念|紀念|記念)?版|エディション)"),
                                       QRegularExpression::CaseInsensitiveOption)},
    };
    VersionEvidence result;
    for (const auto &item : patterns) {
        if (normalized.contains(item.pattern)) {
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
        QRegularExpression pattern;
    };
    static const EditionPattern editionPatterns[]{
        {"deluxe", QRegularExpression(QStringLiteral(R"(\bdeluxe(?: edition)?\b|豪华版|豪華版|デラックス(?:・)?エディション)"),
                                      QRegularExpression::CaseInsensitiveOption)},
        {"special", QRegularExpression(QStringLiteral(R"(\bspecial(?: edition)?\b|特别版|特別版|スペシャル(?:・)?エディション)"),
                                       QRegularExpression::CaseInsensitiveOption)},
        {"expanded", QRegularExpression(QStringLiteral(R"(\bexpanded(?: edition)?\b|扩展版|擴展版)"),
                                        QRegularExpression::CaseInsensitiveOption)},
        {"limited", QRegularExpression(QStringLiteral(R"(\blimited(?: edition)?\b|限定版|リミテッド(?:・)?エディション)"),
                                       QRegularExpression::CaseInsensitiveOption)},
        {"collector", QRegularExpression(QStringLiteral(R"(\bcollector'?s?(?: edition)?\b|珍藏版|コレクターズ(?:・)?エディション)"),
                                         QRegularExpression::CaseInsensitiveOption)},
        {"anniversary", QRegularExpression(QStringLiteral(R"(\b(?:\d+(?:st|nd|rd|th)\s+)?anniversary(?: edition)?\b|周年(?:纪念|紀念|記念)?版)"),
                                           QRegularExpression::CaseInsensitiveOption)},
    };
    for (const auto &item : editionPatterns) {
        if (normalized.contains(item.pattern)) {
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
// Named so nonStrippedMatchAloneIsAcceptable below can require exactly the
// same bar this gate does, rather than a second, silently-driftable 0.5.
constexpr double kAliasArtistThreshold = 0.5;

bool passesAliasArtistGate(const RankedCandidate &candidate)
{
    return !candidate.score.titleViaAlternate || candidate.score.artists >= kAliasArtistThreshold;
}

// isAcceptableMatch's own two bars, named here so passesGlossVariantGate's
// and passesArtistStripGate's B.3b escape hatch (nonStrippedMatchAloneIsAcceptable
// below) and candidateRejectionReason's classification can share the exact
// same numbers with isAcceptableMatch instead of each hard-coding 0.55/0.58
// a second and third time.
constexpr double kTitleAcceptanceThreshold = 0.55;
constexpr double kTotalAcceptanceThreshold = 0.58;

// score.total's own weights (see the end of scoreCandidateWithVariants),
// named here so nonStrippedMatchAloneIsAcceptable's "what would total have
// been" computation below can share them instead of a second, independently
// driftable copy of 0.5/0.2/0.1/0.2.
constexpr double kTitleWeight = 0.5;
constexpr double kArtistsWeight = 0.2;
constexpr double kAlbumWeight = 0.1;
constexpr double kDurationWeight = 0.2;

// The 2000ms window scoreCandidate's own duration curve already treats as
// "best tier" (decision 12; see the first branch of score.duration below)
// -- reused here, rather than a new number, so this gate and that curve
// agree on what "close enough" means. Shared, unchanged, by both
// passesGlossVariantGate and passesArtistStripGate below (DESIGN.md
// decisions 65/66): both gates guard a title win that only holds because a
// stripped variant of the query title was substituted in, so both need the
// same independent evidence and the same bar for "close enough". Renamed
// from kGlossVariantDurationWindowMs (B.3b, qa-b-1) once a second gate
// started reusing it under that name.
constexpr qint64 kStrippedVariantDurationWindowMs = 2000;

// B.3b (qa-b-1, 2026-09-12): passesGlossVariantGate/passesArtistStripGate
// must bind only when a *stripped* variant is the reason this candidate
// cleared the acceptance bars at all -- not merely the reason it scored
// highest. A candidate whose plain, non-stripped title match (see
// ScoreBreakdown::titleWithoutStrip) would *already*, on its own, clear
// both isAcceptableMatch bars did not need the strip to be accepted, so
// demanding duration corroboration for it is wrong: probed counterexample,
// both durations unknown -- query "Bohemian Rhapsody Queen"/["Queen"] vs
// candidate "Bohemian Rhapsody"/["Queen"]. The plain variant alone scores
// title=0.739 (textSimilarity's containment fast-path) and total=0.6696,
// clearing both bars unaided -- this candidate was correctly accepted
// before this feature existed and must stay accepted. But the artist-strip
// variant ("Bohemian Rhapsody", after stripping the trailing "Queen") beats
// it with an exact title=1.0, so titleViaArtistStrip ends up true and,
// without this escape hatch, an unknown duration would reject a candidate
// that used to be fine. The identical shape was found, live and
// unmodified, on the pre-existing gloss gate too via
// "Bohemian Rhapsody (Queen)" -- a real, shipped-in-v0.3.2 regression this
// fix closes at the same time (decision 65's "胜出" wording predates this
// correction; see decision 66 for the retroactive fix and decision 65 for
// the corrected wording).
//
// This is deliberately an escape hatch on *when the gate binds*, not a
// change to what it does once it does bind: a candidate that only ever
// scores 1.0 through the stripped variant, with no non-stripped path
// anywhere near the bars, still hits the exact hard rejection this gate
// was built for (glossVariantDurationGateRejectsATooShortSameArtistTrack /
// artistAppendedToTitleRejectedWhenDurationUnknown are both still exactly
// as strict as before -- their non-stripped title score is far below 0.55).
//
// It must also not resurrect a candidate passesAliasArtistGate (D-11) would
// have rejected: the best non-stripped match considered here can itself
// have come through an alternateTitle (ScoreBreakdown::titleWithoutStripViaAlternate),
// on a candidate whose real artists don't match the query at all -- main
// rejects that shape outright via the alias gate. Without this check, a
// strip on the *primary* title that happens to score even higher would
// flip the actual winner's titleViaAlternate to false (the alias gate only
// ever looks at the winner, not at every path that was tried), silently
// routing a wrong-artist candidate around the one gate built to catch it.
// So this hatch demands the same artist bar the alias gate does whenever
// the qualifying non-stripped path was itself alternate-sourced.
bool nonStrippedMatchAloneIsAcceptable(const ScoreBreakdown &score)
{
    if (score.titleWithoutStrip < kTitleAcceptanceThreshold) {
        return false;
    }
    if (score.titleWithoutStripViaAlternate && score.artists < kAliasArtistThreshold) {
        return false;
    }
    const double totalWithoutStrip = score.titleWithoutStrip * kTitleWeight + score.artists * kArtistsWeight
        + score.album * kAlbumWeight + score.duration * kDurationWeight;
    return totalWithoutStrip >= kTotalAcceptanceThreshold;
}

// B.3c (qa-b-2, 2026-09-12): the four distinct ways a stripped-variant win
// can relate to the duration gate, named so the gate functions below and
// explainMatch's durationGate= field (which must report this exact
// condition, not a re-derived approximation of it) share one definition
// and can never drift apart. B.3b's escape hatch (nonStrippedMatchAloneIsAcceptable)
// was corrected here, not removed: it fired whenever duration was simply
// unknown OR far apart, treating "corroboration unavailable" the same as
// "corroboration available and negative" -- those are not the same
// situation. qa-b-2's differential replay against 82 real resolver runs
// found the flip: query "ARC Raiders (II)" (170567ms) against two "ARC
// Raiders" candidates 27911ms/26754ms longer -- known, and decisively
// outside the window, which is affirmative evidence AGAINST the match, not
// merely missing evidence. B.3b's own counterexample (Bohemian Rhapsody)
// had both durations unknown, which is exactly when the hatch still
// applies; a known, far-apart duration must not be waved through by it.
enum class StrippedVariantDurationGateOutcome {
    WithinWindow,   // duration known, close enough -- gate passes on duration alone
    OutsideWindow,  // duration known, decisively far apart -- gate rejects regardless of the hatch
    Bypassed,       // duration unknown, but the non-stripped match alone already clears the bars
    Required,       // duration unknown, and the non-stripped match alone does not clear the bars
};

StrippedVariantDurationGateOutcome strippedVariantDurationGateOutcome(const ScoreBreakdown &score)
{
    if (score.durationComparable) {
        return score.durationDifferenceMs <= kStrippedVariantDurationWindowMs
            ? StrippedVariantDurationGateOutcome::WithinWindow
            : StrippedVariantDurationGateOutcome::OutsideWindow;
    }
    return nonStrippedMatchAloneIsAcceptable(score) ? StrippedVariantDurationGateOutcome::Bypassed
                                                     : StrippedVariantDurationGateOutcome::Required;
}

bool passesStrippedVariantDurationGate(const ScoreBreakdown &score)
{
    switch (strippedVariantDurationGateOutcome(score)) {
    case StrippedVariantDurationGateOutcome::WithinWindow:
    case StrippedVariantDurationGateOutcome::Bypassed:
        return true;
    case StrippedVariantDurationGateOutcome::OutsideWindow:
    case StrippedVariantDurationGateOutcome::Required:
        return false;
    }
    return false;
}

// A gloss-stripped query variant (see splitTrailingGloss) can make the
// candidate-side title match a *shortened* form of the query just as
// easily as it rescues a genuinely translated title: "Song (Intro)" and a
// same-artist "Intro" track (a different, much shorter recording) score
// identically well through the stripped variant "Song". Duration is the
// only independent evidence left to tell the two apart, so a title win
// that came through a gloss variant is only accepted within this window --
// *when the strip is what's actually responsible for the win at all*, see
// nonStrippedMatchAloneIsAcceptable above -- and the escape hatch that
// covers an unknown duration does not extend to a duration that is known
// and decisively far apart (B.3c, see strippedVariantDurationGateOutcome):
// that is affirmative evidence against the match, not merely unavailable
// evidence. Unlike passesAliasArtistGate's artist bar (D-11), an
// unknown/missing duration without the hatch firing cannot pass here:
// DESIGN.md decision 45's user preference is "no lyrics rather than wrong
// lyrics", and an Intro/Interlude/Outro/Skit collision is common enough
// (frequent album-track names) that erring toward rejection is the safer
// default.
bool passesGlossVariantGate(const RankedCandidate &candidate)
{
    return !candidate.score.titleViaGlossVariant
        || passesStrippedVariantDurationGate(candidate.score);
}

// B.2 (DESIGN.md decision 66): a query title with an artist's own name
// concatenated onto its tail can only be scored correctly once that tail
// is stripped -- but the stripped variant is exact-match-only, for the
// same containment-fast-path reason as the gloss variant above (decision
// 65), so a title win through it is exactly as blind to "same artist,
// different (much shorter/longer) song" as a gloss-variant win is. Same
// fix, same window, same constant, same escape hatch and same B.3c
// narrowing: independent, close duration evidence is required only when
// the strip is what actually got this candidate over the bars, and a
// known, far-apart duration rejects regardless of whether a non-stripped
// path would otherwise have qualified.
bool passesArtistStripGate(const RankedCandidate &candidate)
{
    return !candidate.score.titleViaArtistStrip
        || passesStrippedVariantDurationGate(candidate.score);
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
    // Set only by scoreCandidateWithVariants, never by queryTitleVariants()
    // itself -- this variant needs a specific candidate's artist list (B.2,
    // DESIGN.md decision 66), so unlike isGlossStripped it cannot be
    // determined from the query alone.
    bool isArtistStripped = false;
};

// The query side may carry a bracketed localized gloss that the provider
// candidate's title doesn't, so title matching also tries the query with
// that gloss removed and keeps whichever variant scores highest against a
// given candidate title -- but only counts a gloss-stripped variant on an
// *exact* match (see considerTitle in scoreCandidateWithVariants): a short
// stripped title is far likelier than the full query to land inside some unrelated
// candidate's title via textSimilarity's containment fast-path (min/max of
// sizes), which is exactly how qa-2-match's counterexample slipped through
// before this restriction ("心跳吧" scored 0.667 against the stripped
// "心跳" via containment, beating the correct 0.286 the un-stripped query
// gets via Levenshtein). Even with that restriction, a title win via this
// variant still flips on passesGlossVariantGate, a hard rejection a
// plain-title win never triggers -- but chooseMatch's MatchPolicy::Default
// branch now skips a disqualified top rank rather than stopping at it (see
// chooseMatch), so a gate rejection here no longer buries a legitimate
// second place the way it once could.
QList<QueryTitleVariant> queryTitleVariants(const QString &title, MatchPolicy policy)
{
    QList<QueryTitleVariant> variants{{titleForPolicy(title, policy), false}};
    QString main;
    if (splitTrailingGloss(title, &main) && !main.isEmpty()) {
        variants.append({titleForPolicy(main, policy), true});
    }
    return variants;
}

// queryTitleVariants (and, through splitTrailingGloss, versionEvidence)
// depends only on query.title and policy, never on a specific candidate --
// so this must be computed once per query, not once per candidate. It used
// to live inside scoreCandidate itself, which made it re-run per candidate
// from rankCandidates' loop; for a title with a trailing bracket, that
// pulled versionEvidence's per-call QRegularExpression construction into
// every single candidate comparison, including under MatchPolicy::Default,
// which previously never called versionEvidence at all (measured 4.5x
// slower end to end on a 3275-entry local index).
QList<QueryTitleVariant> normalizedQueryTitleVariants(const QString &title, MatchPolicy policy)
{
    QList<QueryTitleVariant> normalized;
    for (const auto &variant : queryTitleVariants(title, policy)) {
        normalized.append({normalizeSearchText(variant.text), variant.isGlossStripped});
    }
    return normalized;
}

// The token separators for B.2's candidate-relative title variant --
// deliberately whitespace/"/"/"-" only, not cleanArtists' own separator set
// (which also splits on "、", ";", "&", "feat."/"ft." but not on "-" at
// all): B.2 §2 fixes this exact set for this variant, independently of how
// artist fields get tokenized elsewhere.
QStringList titleStripTokens(const QString &title)
{
    static const QRegularExpression separator(QStringLiteral(R"([\s/\-]+)"));
    return title.split(separator, Qt::SkipEmptyParts);
}

// B.2 (DESIGN.md decision 66): every way to split `title`'s tokens into a
// leading remainder plus a trailing run that, in full, exactly names one
// of `cleanedCandidateArtists` (already cleanArtists()-ed, i.e.
// normalizeSearchText'd, by the caller) -- not partial or containing, or
// "浴火" would match an artist "浴火者乐队". Deliberately not just the
// longest trailing run: a shorter, later remainder can still be the one
// that actually matches the candidate's own title (query "A B C" against
// candidate title "A B" with artists ["C", "B C"] -- the 1-token tail "C"
// is the correct strip, but it would never be tried if this stopped at the
// first, longer-tail match "B C"). Returning every match rather than just
// the first is free: every returned remainder only ever counts on an exact
// *title* match against the candidate anyway (see scoreCandidateWithVariants),
// so an extra, wrong-length remainder that doesn't happen to equal the
// candidate's own title just scores low and is ignored like any other
// losing variant. A remainder is never empty -- splitAt starts at 1, so at
// least the first token always stays -- which is how "the whole title is
// the artist name" (B.2 §4) is discarded rather than special-cased.
QStringList artistStrippedTitleVariants(const QString &title, const QStringList &cleanedCandidateArtists)
{
    QStringList remainders;
    if (cleanedCandidateArtists.isEmpty()) {
        return remainders;
    }
    const QStringList tokens = titleStripTokens(title);
    for (qsizetype splitAt = 1; splitAt < tokens.size(); ++splitAt) {
        const QString normalizedTail = normalizeSearchText(tokens.mid(splitAt).join(QLatin1Char(' ')));
        if (cleanedCandidateArtists.contains(normalizedTail)) {
            remainders.append(tokens.mid(0, splitAt).join(QLatin1Char(' ')));
        }
    }
    return remainders;
}

// The actual per-candidate scoring body, taking the query-side variants
// already normalized by the caller (see normalizedQueryTitleVariants)
// instead of recomputing them -- rankCandidates computes them exactly
// once and passes the same list to every candidate. scoreCandidate (the
// exported, single-candidate convenience entry point used directly by
// tests and one-off callers) computes them itself and forwards here.
ScoreBreakdown scoreCandidateWithVariants(const TrackQuery &query, const Candidate &candidate, MatchPolicy policy,
                                         const QList<QueryTitleVariant> &normalizedQueryVariants)
{
    ScoreBreakdown score;
    score.versionPolicyApplied = policy == MatchPolicy::PreserveVersions;
    QStringList candidateTitles{candidate.title};
    candidateTitles.append(candidate.alternateTitles);
    score.versionTier = policy == MatchPolicy::PreserveVersions
        ? classifyCandidateVersions(query.title, candidateTitles)
        : VersionTier::Normal;
    // Computed once here and reused for score.artists below, rather than
    // calling cleanArtists(candidate.artists) twice -- decision 65 already
    // measured what an extra regex pass per candidate costs on a
    // real-sized (~3000 entry) index.
    const QStringList cleanedCandidateArtists = cleanArtists(candidate.artists);
    // B.2's third query-title variant (DESIGN.md decision 66): unlike the
    // other two (see normalizedQueryVariants, computed once for the whole
    // pool), this one needs the candidate's own artist list to know what to
    // strip, so it cannot live in queryTitleVariants()/rankCandidates'
    // shared list and must be computed here, once per candidate.
    QList<QueryTitleVariant> variants = normalizedQueryVariants;
    for (const auto &remainder :
         artistStrippedTitleVariants(titleForPolicy(query.title, policy), cleanedCandidateArtists)) {
        variants.append({normalizeSearchText(remainder), false, true});
    }
    // B.3b: the best title score reachable using only non-stripped query
    // variants, tracked alongside (not instead of) the overall best below,
    // together with whether that best non-stripped score came via an
    // alternateTitle -- see ScoreBreakdown::titleWithoutStrip/
    // titleWithoutStripViaAlternate and nonStrippedMatchAloneIsAcceptable.
    double titleWithoutStrip = 0;
    bool titleWithoutStripViaAlternate = false;
    auto considerTitle = [&](const QString &title, bool alternate) {
        const QString normalizedCandidateTitle = normalizeSearchText(titleForPolicy(title, policy));
        for (const auto &queryVariant : variants) {
            const double titleScore = textSimilarity(queryVariant.text, normalizedCandidateTitle);
            const bool queryVariantIsStripped = queryVariant.isGlossStripped || queryVariant.isArtistStripped;
            if (!queryVariantIsStripped && titleScore > titleWithoutStrip) {
                titleWithoutStrip = titleScore;
                titleWithoutStripViaAlternate = alternate;
            }
            // A gloss-stripped or artist-stripped variant only
            // participates on an exact match -- see the note on
            // queryTitleVariants (gloss) and artistStrippedTitleVariants
            // (artist strip) for why a partial match here is unsafe to
            // let through at all, not just unsafe to rank first.
            if (queryVariantIsStripped && titleScore < 1.0) {
                continue;
            }
            // On a tie, prefer evidence that doesn't depend on either
            // strip: both passesGlossVariantGate's and
            // passesArtistStripGate's premise is "this match only holds
            // because a variant got substituted in", which stops being
            // true the moment an equally-good, plain-title path exists (a
            // primary title matching a stripped variant, tied by an
            // alternateTitle matching the query's own unmodified title).
            // Without this, a gate could fire on a candidate that also
            // carries the strongest possible plain evidence, simply
            // because considerTitle visits candidate.title before
            // alternateTitles and only overwrites on a strict ">".
            const bool currentWinnerIsStripped = score.titleViaGlossVariant || score.titleViaArtistStrip;
            const bool prefersPlainOnTie = titleScore == score.title
                && currentWinnerIsStripped && !queryVariantIsStripped;
            if (titleScore > score.title || prefersPlainOnTie) {
                score.title = titleScore;
                score.titleViaAlternate = alternate;
                score.titleViaGlossVariant = queryVariant.isGlossStripped;
                score.titleViaArtistStrip = queryVariant.isArtistStripped;
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
    score.titleWithoutStrip = titleWithoutStrip;
    score.titleWithoutStripViaAlternate = titleWithoutStripViaAlternate;
    score.artists = artistSimilarity(cleanArtists(query.artists), cleanedCandidateArtists);
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
    score.total = score.title * kTitleWeight + score.artists * kArtistsWeight
        + score.album * kAlbumWeight + score.duration * kDurationWeight;
    if (score.versionTier == VersionTier::Conflict) {
        score.rejectionReason = QStringLiteral("version-conflict");
    }
    return score;
}

} // namespace

QString normalizeSearchText(QString text)
{
    // static const, not reconstructed on every call (B.3c, qa-b-2): this is
    // one of the hottest functions in the whole matcher -- called for the
    // candidate title, each artist, and the album on every candidate, so a
    // real ~3275-entry AMLL index was compiling roughly 12000 of these per
    // lookup even on main, before this task's strip variants (title/artist
    // tokenizing and re-scoring) roughly doubled that call count. Same
    // treatment decision 65 already gave versionEvidence()'s 16 patterns
    // for the identical reason; this one was simply missed. qa-b-2
    // measured 659ms/198ms on the real user index; re-measured
    // independently on a synthetic ~3275-entry pool (see DESIGN.md decision
    // 65's performance note) at ~207ms/~150ms per PreserveVersions
    // rankCandidates call -- different dataset and hardware than qa-b-2's,
    // so the absolute numbers differ, but the direction and magnitude
    // (roughly a quarter to a third faster) reproduce independently.
    static const QRegularExpression nonWordRun(QStringLiteral(R"([^\p{L}\p{N}]+)"));
    text = text.normalized(QString::NormalizationForm_KC).toCaseFolded();
    text.replace(nonWordRun, QStringLiteral(" "));
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
    return scoreCandidateWithVariants(query, candidate, policy,
                                      normalizedQueryTitleVariants(query.title, policy));
}

QList<RankedCandidate> rankCandidates(const TrackQuery &query, const QList<Candidate> &candidates,
                                      MatchPolicy policy)
{
    // Computed once for the whole candidate pool, not once per candidate
    // via scoreCandidate -- see normalizedQueryTitleVariants.
    const auto normalizedQueryVariants = normalizedQueryTitleVariants(query.title, policy);
    QList<RankedCandidate> ranked;
    ranked.reserve(candidates.size());
    for (const auto &candidate : candidates) {
        ranked.append({candidate, scoreCandidateWithVariants(query, candidate, policy, normalizedQueryVariants)});
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
        && candidate.score.title >= kTitleAcceptanceThreshold && candidate.score.total >= kTotalAcceptanceThreshold
        && passesGlossVariantGate(candidate) && passesArtistStripGate(candidate);
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
    } else {
        // A gate here (passesAliasArtistGate, passesGlossVariantGate,
        // passesArtistStripGate) disqualifies a candidate; it doesn't mean
        // "lower quality" --
        // skip past it and keep looking, the same as the
        // MatchPolicy::PreserveVersions branch above already does. A
        // failed *threshold* (isAcceptableMatch) is a different signal:
        // decision 8 has always applied it to the first gate-passing
        // candidate alone, and a failure there must still mean "the
        // primary path stops here" -- not "keep looking for a
        // lower-ranked candidate that happens to pass" (ranking is by
        // total, and score.title isn't part of that key, so a
        // first-gate-passing candidate can have the highest total while
        // still failing the title-threshold on its own; qa-2-match
        // measured a same-artist neighbor 20s off getting picked this
        // way, on an ordinary input with no gloss variant or alias
        // involved at all -- exactly the "wrong lyrics" shape decision 45
        // exists to prevent) -- but it must still fall through to the
        // allowLocalizedFallback path below exactly as it always did,
        // hence break rather than returning nullopt directly: an early
        // return here would skip that fallback path entirely, which is
        // its own, independently-gated way of finding a match (D-8) and
        // has nothing to do with this loop's gates.
        for (const auto &candidate : ranked) {
            if (!passesAliasArtistGate(candidate) || !passesGlossVariantGate(candidate)
                || !passesArtistStripGate(candidate)) {
                continue;
            }
            if (isAcceptableMatch(candidate)) {
                return candidate;
            }
            break;
        }
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
    if (candidate.score.title < kTitleAcceptanceThreshold) {
        return QStringLiteral("title-threshold");
    }
    if (candidate.score.total < kTotalAcceptanceThreshold) {
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
    if (!passesArtistStripGate(candidate)) {
        // Same two-reason split as the gloss gate above, and for the same
        // reason (B.2, DESIGN.md decision 66): a known duration outside
        // the window is very likely a different, unrelated recording (no
        // action fixes that); an unknown duration is something the user
        // could resolve by adding a length, so it gets its own, more
        // hopeful reason string.
        return candidate.score.durationComparable
            ? QStringLiteral("artist-strip-duration-threshold")
            : QStringLiteral("artist-strip-duration-unknown");
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
    // out (DESIGN.md decision 46). This deliberately does NOT include B.2's
    // artist-stripped variant (decision 66): that one is candidate-relative
    // (it needs a specific candidate's artist list), so it cannot be
    // listed once for the whole query the way these two can -- it shows up
    // per-candidate instead, as titleVia=artist-strip on the ranked line
    // below.
    QStringList titleVariantTexts;
    for (const auto &variant : queryTitleVariants(query.title, policy)) {
        titleVariantTexts.append(variant.text);
    }
    stream << "raw title: " << query.title << '\n'
           << "raw artists: " << query.artists.join(QStringLiteral(" / ")) << '\n'
           << "query length: " << (query.lengthMs > 0 ? QString::number(query.lengthMs) : QStringLiteral("unknown"))
           << '\n'
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
        if (score.titleViaAlternate && score.titleViaArtistStrip) {
            return QStringLiteral("alias+artist-strip");
        }
        if (score.titleViaGlossVariant) {
            return QStringLiteral("gloss");
        }
        if (score.titleViaArtistStrip) {
            return QStringLiteral("artist-strip");
        }
        if (score.titleViaAlternate) {
            return QStringLiteral("alias");
        }
        return QStringLiteral("title");
    };
    const auto ranked = rankCandidates(query, candidates, policy);
    bool anyStrippedVariantDurationUnknown = false;
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
               << " titleVia=" << titleViaLabel(item.score);
        // B.3b/B.3c legibility fix: titleVia=gloss/artist-strip (and their
        // alias+ combinations) is exactly the situation where
        // passesGlossVariantGate/passesArtistStripGate's duration
        // corroboration requirement (strippedVariantDurationGateOutcome)
        // decides whether this candidate needs, has, or lacks it --
        // without this, a candidate with titleVia=artist-strip and no
        // rejected= gives no visible reason why the gate did or didn't
        // block it, which is exactly the "diagnostic disagrees with the
        // real decision" shape decision 46 exists to rule out. This calls
        // the exact same function the gates call, not a re-derived
        // approximation of its condition, so it can never print a state
        // the gate itself didn't reach (B.3c: qa-b-2 flagged that printing
        // nonStrippedMatchAloneIsAcceptable() directly, post-B.3c, would
        // show "required" for a candidate the gate actually passed on a
        // known, in-window duration, and "bypassed" for one it rejected on
        // a known, far-apart duration). Only shown for these two titleVia
        // kinds -- every other candidate's line is unaffected.
        if (item.score.titleViaGlossVariant || item.score.titleViaArtistStrip) {
            const auto durationGateLabel = [](StrippedVariantDurationGateOutcome outcome) {
                switch (outcome) {
                case StrippedVariantDurationGateOutcome::WithinWindow:
                    return QStringLiteral("within-window");
                case StrippedVariantDurationGateOutcome::OutsideWindow:
                    return QStringLiteral("outside-window");
                case StrippedVariantDurationGateOutcome::Bypassed:
                    return QStringLiteral("bypassed");
                case StrippedVariantDurationGateOutcome::Required:
                    return QStringLiteral("required");
                }
                return QStringLiteral("required");
            };
            stream << " plainTitle=" << QString::number(item.score.titleWithoutStrip, 'f', 3)
                   << " durationGate=" << durationGateLabel(strippedVariantDurationGateOutcome(item.score));
        }
        stream << " versionTier="
               << (item.score.versionTier == VersionTier::Normal
                       ? QStringLiteral("normal")
                       : item.score.versionTier == VersionTier::OneSided
                           ? QStringLiteral("one-sided") : QStringLiteral("conflict"));
        // Single source of truth for the classification, shared with
        // Resolver's "search ... rejected=" log field -- keeps the two from
        // drifting apart (see candidateRejectionReason()).
        const QString rejectionReason = candidateRejectionReason(item);
        if (!rejectionReason.isEmpty()) {
            stream << " rejected=" << rejectionReason;
        }
        if (rejectionReason == QStringLiteral("gloss-duration-unknown")
            || rejectionReason == QStringLiteral("artist-strip-duration-unknown")) {
            anyStrippedVariantDurationUnknown = true;
        }
        stream << '\n';
    }
    // --explain has no MPRIS state to read a length from, unlike the
    // daemon itself, so "query length: unknown" above is common here and
    // not a sign of anything wrong -- but it does mean any
    // gloss-duration-unknown/artist-strip-duration-unknown rejection right
    // below it may not reflect what the daemon would actually decide at
    // runtime, so say so.
    if (anyStrippedVariantDurationUnknown && query.lengthMs <= 0) {
        stream << "note: no query length was provided (see \"query length: unknown\" above); "
                  "pass --length-ms to check whether a known length would change a "
                  "gloss-duration-unknown or artist-strip-duration-unknown result above. "
                  "The daemon itself always has one, read from MPRIS." << '\n';
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
