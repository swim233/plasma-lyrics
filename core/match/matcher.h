#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace PlasmaLyrics {

struct TrackQuery {
    QString title;
    QStringList artists;
    QString album;
    qint64 lengthMs = 0;
    // Music-service ids only.  mpris:trackid must never be put here.
    QHash<QString, QStringList> platformIds;
    // Used only by local providers for an audio-file sidecar. Network
    // providers deliberately ignore it.
    QString mediaSrc;
};

struct Candidate {
    QString trackId;
    QString title;
    QStringList artists;
    QString album;
    qint64 lengthMs = 0;
    QStringList alternateTitles;   // this track's other known titles (netease transNames)
    // Provider-private content locator.  trackId remains the stable key used
    // for preferences, mappings, cached lyrics and offsets.
    QString contentId;
    QHash<QString, QStringList> platformIds;
    QStringList authors;
};

enum class MatchPolicy {
    Default,
    PreserveVersions,
};

enum class VersionTier {
    Normal,
    OneSided,
    Conflict,
};

struct ScoreBreakdown {
    double title = 0;
    double artists = 0;
    double album = 0;
    double duration = 0;
    double total = 0;
    qint64 durationDifferenceMs = 0;
    bool titleViaAlternate = false;   // true when an alternateTitles entry beat the primary title
    // true when the winning query-side title was a gloss-stripped variant
    // (see splitTrailingGloss), not the original query title. Unlike
    // titleViaAlternate, this is evidence the candidate-side title matches
    // a *shortened* form of the query, which a same-artist, differently
    // durationed track (an Intro/Interlude/Outro) can also do -- see
    // passesGlossVariantGate.
    bool titleViaGlossVariant = false;
    // true when the winning query-side title match came from stripping a
    // trailing run of tokens that exactly names one of the candidate's own
    // artists off the query title (B.2, DESIGN.md decision 66) -- e.g. a
    // query title of "Song Artist Name" against a candidate whose title is
    // "Song" and whose artists include "Artist Name". Distinct from
    // titleViaGlossVariant because this variant is candidate-relative (it
    // needs candidate.artists to know what to strip), not determined by
    // the query alone -- but it gets the same "only an exact match counts,
    // and only with independent duration evidence" treatment; see
    // passesArtistStripGate.
    bool titleViaArtistStrip = false;
    // The best title score achievable using only NON-stripped query
    // variants (the plain query title, matched against either candidate.title
    // or an alternateTitle) for this same candidate -- i.e. what score.title
    // would be if neither the gloss-stripped nor the artist-stripped variant
    // existed. Used by passesGlossVariantGate/passesArtistStripGate to tell
    // "this candidate only cleared the acceptance bars because a stripped
    // variant scored higher" (duration corroboration required) apart from
    // "a stripped variant merely won the tie-break, but a plain match would
    // have cleared the bars on its own anyway" (no corroboration needed) --
    // see DESIGN.md decision 65/66's B.3b correction. Independent of which
    // variant actually won: score.title/score.titleViaGlossVariant/
    // score.titleViaArtistStrip above still reflect the single best variant,
    // exactly as before.
    double titleWithoutStrip = 0;
    // Whether the best non-stripped score above (titleWithoutStrip) came
    // from matching an alternateTitle rather than the candidate's primary
    // title. Mirrors titleViaAlternate, but for the non-stripped shadow
    // score rather than the actual winner: needed because the actual
    // winner's titleViaAlternate can be false (a strip on the *primary*
    // title outscored the alternate match) even though the only qualifying
    // non-stripped path went through an alternateTitle -- without tracking
    // this separately, passesGlossVariantGate/passesArtistStripGate's
    // escape hatch would credit a path that passesAliasArtistGate (D-11)
    // was built to reject and let a wrong-artist candidate through.
    bool titleWithoutStripViaAlternate = false;
    // true when the winning title match came from stripping a trailing
    // localized-gloss bracket off the CANDIDATE's own title (or
    // alternateTitle) rather than the query's -- e.g. a candidate title of
    // "惑星ループ (行星循环)" against a query of "惑星ループ" (QQ appends a
    // Chinese translated title onto non-Chinese tracks; DESIGN.md decision
    // 70). Mirrors titleViaGlossVariant's mechanism (same splitTrailingGloss
    // predicate, same exact-match-only restriction) but on the other side
    // of the comparison, and independent of it: both can be true at once
    // (a bracket stripped from both the query and the candidate to reach
    // an exact match). Gets the same "only an exact match counts, and only
    // with independent duration evidence" treatment; see
    // passesCandidateGlossGate.
    bool titleViaCandidateGloss = false;
    bool durationComparable = false;  // true when both sides had a known length (query and candidate)
    VersionTier versionTier = VersionTier::Normal;
    bool versionPolicyApplied = false;
    QString rejectionReason;
};

struct RankedCandidate {
    Candidate candidate;
    ScoreBreakdown score;
};

QString normalizeSearchText(QString text);
QString cleanTitle(QString title);
// Exposed for direct unit testing of the version-marker guard (see
// matcher.cpp) -- not a general-purpose title-parsing utility for callers
// outside this module. Returns true and sets *main to title with a
// trailing, no-version-marker bracketed gloss removed (e.g. a localized
// alias like "Song (歌名译名)" -> "Song"); returns false when there is no
// trailing bracket, or its contents carry a version marker and must not be
// treated as a translation/alias.
bool splitTrailingGloss(const QString &title, QString *main);
QStringList cleanArtists(const QStringList &artists);
QString searchKeywords(const TrackQuery &query);
ScoreBreakdown scoreCandidate(const TrackQuery &query, const Candidate &candidate,
                              MatchPolicy policy = MatchPolicy::Default);
QList<RankedCandidate> rankCandidates(const TrackQuery &query, const QList<Candidate> &candidates,
                                      MatchPolicy policy = MatchPolicy::Default);
bool isAcceptableMatch(const RankedCandidate &candidate);
// Picks a usable match out of already-ranked candidates. When
// allowLocalizedFallback is true, a second acceptance path is allowed for
// "title unreadable but artists and duration both line up" cases.
std::optional<RankedCandidate> chooseMatch(const QList<RankedCandidate> &ranked, bool allowLocalizedFallback);
QString explainMatch(const TrackQuery &query, const QList<Candidate> &candidates,
                     bool allowLocalizedFallback, bool platformKnown = true,
                     MatchPolicy policy = MatchPolicy::Default);
// Same rejection classification explainMatch uses for one candidate's
// listing line: the candidate's own rejectionReason if scoreCandidate set
// one (version-conflict), otherwise the first gate or threshold it misses.
// The -threshold suffix alone does NOT say which of the two kinds a reason
// is, and the difference is the one decision 65 argued is the line between
// "no lyrics" and "the wrong lyrics" -- so the full set is grouped here:
//
//   Gate rejections -- chooseMatch's loop does `continue`, skipping this
//   candidate and trying the next one (matcher.cpp, all four gates share
//   the one `continue` branch):
//     alias-artist-threshold                  (passesAliasArtistGate)
//     gloss-duration-threshold / -unknown     (passesGlossVariantGate)
//     artist-strip-duration-threshold / -unknown
//                                             (passesArtistStripGate)
//     candidate-gloss-artist-threshold,
//     candidate-gloss-duration-threshold / -unknown
//                                             (passesCandidateGlossGate)
//
//   Acceptance-threshold failures -- chooseMatch's loop does `break`: the
//   ranking is by total, so once the best remaining candidate misses one of
//   these, no lower-ranked one can be a better answer. `break`, not
//   `return`, because the localized fallback (D-8) is judged separately.
//     title-threshold, total-threshold
//
// The -unknown reasons mean "a duration was not available", which is the
// common case on AMLL and on .lrc files without a [length:] tag, not an
// exotic one; the paired -threshold reason means the durations were known
// and decisively far apart. Empty means
// the candidate would actually be accepted. Callers that only need "why
// didn't the top-ranked candidate win" (e.g. a resolver log line) can call
// this on ranked.first() instead of formatting/parsing explainMatch's text.
QString candidateRejectionReason(const RankedCandidate &candidate);

} // namespace PlasmaLyrics
