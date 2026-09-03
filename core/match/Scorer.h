// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QStringList>

namespace Lyrics
{

// A search candidate handed to scoring (DESIGN #12). pbi records the real
// payload in providers/tests/fixtures/netease/search-mofachuniang.json.
struct Candidate
{
    QString trackId;
    QString title;
    qint64 lengthMs = 0;
};

// The playing track's query. `length-toleration` is measured from pbi's
// recorded 3-candidate payload (§6 test 3).
struct TrackQuery
{
    QString title;
    qint64 lengthMs = 0;
};

// Returns the best candidate's trackId, or a null QString when the list is
// empty. Rule (waylyrics §6): a candidate inside `length-toleration` (2s) of
// the query length wins outright; beyond that, distance decides, and an
// exact-title match breaks a distance tie.
QString pickBest(const QList<Candidate> &candidates, const TrackQuery &query);

} // namespace Lyrics
