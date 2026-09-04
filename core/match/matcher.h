#pragma once

#include <QList>
#include <QString>
#include <QStringList>

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
};

struct ScoreBreakdown {
    double title = 0;
    double artists = 0;
    double album = 0;
    double duration = 0;
    double total = 0;
    qint64 durationDifferenceMs = 0;
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
QString explainMatch(const TrackQuery &query, const QList<Candidate> &candidates);

} // namespace PlasmaLyrics

