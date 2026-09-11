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
// one (e.g. version-conflict), otherwise the first threshold it misses
// (title-threshold, total-threshold, alias-artist-threshold). Empty means
// the candidate would actually be accepted. Callers that only need "why
// didn't the top-ranked candidate win" (e.g. a resolver log line) can call
// this on ranked.first() instead of formatting/parsing explainMatch's text.
QString candidateRejectionReason(const RankedCandidate &candidate);

} // namespace PlasmaLyrics
