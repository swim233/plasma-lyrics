#pragma once

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
};

struct Candidate {
    QString trackId;
    QString title;
    QStringList artists;
    QString album;
    qint64 lengthMs = 0;
    QStringList alternateTitles;   // this track's other known titles (netease transNames)
};

struct ScoreBreakdown {
    double title = 0;
    double artists = 0;
    double album = 0;
    double duration = 0;
    double total = 0;
    qint64 durationDifferenceMs = 0;
    bool titleViaAlternate = false;   // true when an alternateTitles entry beat the primary title
    bool durationComparable = false;  // true when both sides had a known length (query and candidate)
};

struct RankedCandidate {
    Candidate candidate;
    ScoreBreakdown score;
};

QString normalizeSearchText(QString text);
QString cleanTitle(QString title);
QStringList cleanArtists(const QStringList &artists);
QString searchKeywords(const TrackQuery &query);
ScoreBreakdown scoreCandidate(const TrackQuery &query, const Candidate &candidate);
QList<RankedCandidate> rankCandidates(const TrackQuery &query, const QList<Candidate> &candidates);
bool isAcceptableMatch(const RankedCandidate &candidate);
// Picks a usable match out of already-ranked candidates. When
// allowLocalizedFallback is true, a second acceptance path is allowed for
// "title unreadable but artists and duration both line up" cases.
std::optional<RankedCandidate> chooseMatch(const QList<RankedCandidate> &ranked, bool allowLocalizedFallback);
QString explainMatch(const TrackQuery &query, const QList<Candidate> &candidates,
                     bool allowLocalizedFallback, bool platformKnown = true);

} // namespace PlasmaLyrics
