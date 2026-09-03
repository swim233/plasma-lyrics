// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Duration-tolerance scoring (DESIGN #12). Rule (waylyrics §6): a candidate
 * inside `length-toleration` (2s) of the query length wins outright; beyond
 * that, distance decides, and an exact-title match breaks a distance tie.
 */

#include "Scorer.h"

namespace Lyrics
{

namespace
{

constexpr qint64 kLengthTolerationMs = 2000;

} // namespace

QString pickBest(const QList<Candidate> &candidates, const TrackQuery &query)
{
    const Candidate *best = nullptr;
    qint64 bestDistance = 0;
    bool bestInsideToleration = false;
    bool bestTitleMatch = false;

    for (const Candidate &candidate : candidates) {
        const qint64 distance = qAbs(candidate.lengthMs - query.lengthMs);
        const bool insideToleration = distance <= kLengthTolerationMs;
        const bool titleMatch = candidate.title == query.title;

        bool wins;
        if (best == nullptr) {
            wins = true;
        } else if (insideToleration != bestInsideToleration) {
            wins = insideToleration;
        } else if (distance != bestDistance) {
            wins = distance < bestDistance;
        } else if (titleMatch != bestTitleMatch) {
            wins = titleMatch;
        } else {
            wins = false;
        }

        if (wins) {
            best = &candidate;
            bestDistance = distance;
            bestInsideToleration = insideToleration;
            bestTitleMatch = titleMatch;
        }
    }

    return best == nullptr ? QString() : best->trackId;
}

} // namespace Lyrics
