// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Duration-tolerance scoring, DESIGN #12 / §6 test 3. waylyrics' rule: a
 * candidate within `length-toleration` (2s) of the playing track's length is
 * a best choice (weight zero on length); beyond that, distance decides.
 *
 * The real candidates are 魔法厨娘's three same-name versions (recorded in
 * providers/tests/fixtures/netease/search-mofachuniang.json):
 *   423776453  286066 ms   (official)
 *   1418713342 286289 ms   (cover)
 *   33497601   284328 ms   (album cut)
 * A track playing at 286100 ms must pick 423776453, not the cover that is
 * 189 ms further away.
 */

#include "match/Scorer.h"

#include <QTest>

using namespace Lyrics;

namespace
{

Candidate makeCandidate(const char *id, qint64 lengthMs)
{
    Candidate c;
    c.trackId = QString::fromLatin1(id);
    c.title = QStringLiteral("魔法厨娘");
    c.lengthMs = lengthMs;
    return c;
}

} // namespace

class ScorerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void realMagicChefCandidates();
    void withinToleranceBeatsCloserButOutside();
    void noCandidates();
    void allOutsideToleranceStillPicksClosest();
    void titleMatchBreaksLengthTie();
};

void ScorerTest::realMagicChefCandidates()
{
    const QList<Candidate> candidates{
        makeCandidate("423776453", 286066),
        makeCandidate("1418713342", 286289),
        makeCandidate("33497601", 284328),
    };
    TrackQuery q;
    q.title = QStringLiteral("魔法厨娘");
    q.lengthMs = 286100;

    const QString best = pickBest(candidates, q);
    QCOMPARE(best, QStringLiteral("423776453"));
}

void ScorerTest::withinToleranceBeatsCloserButOutside()
{
    // 500 ms away (inside 2s tolerance) beats 1900 ms away even if the
    // tolerance rule alone would allow both: inside-tolerance wins outright.
    const QList<Candidate> candidates{
        makeCandidate("a", 286066),  // 34 ms away
        makeCandidate("b", 288000),  // 1900 ms away
    };
    TrackQuery q;
    q.title = QStringLiteral("魔法厨娘");
    q.lengthMs = 286100;
    QCOMPARE(pickBest(candidates, q), QStringLiteral("a"));
}

void ScorerTest::noCandidates()
{
    TrackQuery q;
    q.title = QStringLiteral("魔法厨娘");
    QCOMPARE(pickBest({}, q), QString());
}

void ScorerTest::allOutsideToleranceStillPicksClosest()
{
    // Search should not fail just because nothing is within tolerance: the
    // closest candidate still wins (it is probably the right song, cut).
    const QList<Candidate> candidates{
        makeCandidate("short", 200000),
        makeCandidate("close", 300000),
    };
    TrackQuery q;
    q.title = QStringLiteral("魔法厨娘");
    q.lengthMs = 301000;
    QCOMPARE(pickBest(candidates, q), QStringLiteral("close"));
}

void ScorerTest::titleMatchBreaksLengthTie()
{
    QList<Candidate> candidates{
        makeCandidate("exact-title", 286066),
    };
    Candidate other = makeCandidate("other-title", 286066);
    other.title = QStringLiteral("别的歌");
    candidates.append(other);

    TrackQuery q;
    q.title = QStringLiteral("魔法厨娘");
    q.lengthMs = 286100;
    QCOMPARE(pickBest(candidates, q), QStringLiteral("exact-title"));
}

QTEST_MAIN(ScorerTest)
#include "tst_scorer.moc"
