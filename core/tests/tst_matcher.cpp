#include "core/match/matcher.h"

#include <QTest>

using namespace PlasmaLyrics;

class MatcherTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void splitsBrowserArtist()
    {
        QCOMPARE(cleanArtists({QStringLiteral("闹闹丶/FFF君/欧Ωhm/洛天依Official")}).size(), 4);
    }

    void splitsTrailingNativeScriptAlias()
    {
        // netease stores some performers as "primary name (native-script
        // alias)", e.g. the literal string "BTS (防弹少年团)" -- both halves
        // name the same group, so this must become two entries, not one
        // merged blob that matches neither name well (DESIGN.md decision 44).
        const auto artists = cleanArtists({QStringLiteral("BTS (防弹少年团)")});
        QCOMPARE(artists.size(), 2);
        QVERIFY(artists.contains(QStringLiteral("bts")));
        QVERIFY(artists.contains(normalizeSearchText(QStringLiteral("防弹少年团"))));
    }

    void doesNotSplitParensThatAreNotTrailing()
    {
        // Only a *trailing* parenthetical is treated as a native-script alias
        // -- a collaborator whose own name happens to contain parentheses
        // that aren't the very last thing in the string must not be torn
        // apart. Two shapes, both must stay a single, untouched entry:
        // parens at the very start ("(Sandy) Alex G"), and parens in the
        // middle with more text after them ("Alex (Sandy) G") -- the second
        // shape is the one that actually distinguishes "anchored to the end"
        // from "anchored only at the open paren": an unanchored match would
        // still match up through the close-paren and silently drop the " G"
        // that follows, since nothing captures text after the matched group.
        const auto atStart = cleanArtists({QStringLiteral("(Sandy) Alex G")});
        QCOMPARE(atStart.size(), 1);
        QCOMPARE(atStart.first(), normalizeSearchText(QStringLiteral("(Sandy) Alex G")));

        const auto inMiddle = cleanArtists({QStringLiteral("Alex (Sandy) G")});
        QCOMPARE(inMiddle.size(), 1);
        QCOMPARE(inMiddle.first(), normalizeSearchText(QStringLiteral("Alex (Sandy) G")));
    }

    void compoundCandidateNameNoLongerUndercountsArtistSimilarity()
    {
        // This is NOT a general monotonicity guarantee -- there isn't one
        // (see DESIGN.md decision 44; qa-2 has a counterexample where the
        // query side is fixed and the candidate-side split still lowers the
        // score, because cleanArtists replaces a merged token rather than
        // only adding to it, on whichever side gets split). This test pins
        // one concrete instance that matches Resolver's real query shape:
        // a bare, unpunctuated query artist ("BTS") against a candidate
        // whose field carries the same name plus a trailing native-script
        // alias ("BTS (防弹少年团)") -- for this specific pair, splitting the
        // candidate side must not undercount the artist match relative to
        // the un-split candidate.
        const TrackQuery query{QStringLiteral("Spring Day"), {QStringLiteral("BTS")}, QString(), 0};
        const Candidate plain{QStringLiteral("1"), QStringLiteral("Spring Day"), {QStringLiteral("BTS")}, QString(), 0};
        const Candidate withAlias{QStringLiteral("2"), QStringLiteral("Spring Day"),
                                  {QStringLiteral("BTS (防弹少年团)")}, QString(), 0};
        QVERIFY(scoreCandidate(query, withAlias).artists >= scoreCandidate(query, plain).artists);
    }

    void compoundArtistAliasNoLongerUndercountsArtistSimilarity()
    {
        // Regression for the bug qa-1 found in commit 3's first attempt:
        // before this fix, "BTS" vs "BTS (防弹少年团)" scored as a single
        // merged token via the substring-ratio branch of textSimilarity
        // (measured artists=0.333 via `--explain 春日 BTS`), which sits
        // below the D-11 alias-artist-gate threshold of 0.5 and would wrongly
        // reject a genuine match. It must now score as an exact match.
        const TrackQuery query{QStringLiteral("Spring Day"), {QStringLiteral("BTS")}, QString(), 0};
        const Candidate candidate{QStringLiteral("2626124904"), QStringLiteral("Spring Day"),
                                  {QStringLiteral("BTS (防弹少年团)")}, QString(), 0};
        const auto score = scoreCandidate(query, candidate);
        QCOMPARE(score.artists, 1.0);
    }

    void choosesCorrectDurationVersion()
    {
        const TrackQuery query{QStringLiteral("魔法厨娘"), {QStringLiteral("洛天依")}, QString(), 286100};
        const QList<Candidate> candidates{
            {QStringLiteral("33497601"), QStringLiteral("魔法厨娘"), {QStringLiteral("洛天依")}, QString(), 284328},
            {QStringLiteral("1418713342"), QStringLiteral("魔法厨娘"), {QStringLiteral("洛天依")}, QString(), 286289},
            {QStringLiteral("423776453"), QStringLiteral("魔法厨娘"), {QStringLiteral("洛天依")}, QString(), 286066}};
        const auto ranked = rankCandidates(query, candidates);
        QCOMPARE(ranked.first().candidate.trackId, QStringLiteral("423776453"));
        QVERIFY(isAcceptableMatch(ranked.first()));
    }

    void alternateTitleRescuesLocalizedTitle()
    {
        // 偶像 is the Mandarin storefront's localized title for YOASOBI's
        // アイドル; without transNames as evidence this is a not-found today.
        const TrackQuery query{QStringLiteral("偶像"), {QStringLiteral("YOASOBI")}, QString(), 213234};
        const Candidate candidate{QStringLiteral("2034742057"), QStringLiteral("アイドル"),
                                  {QStringLiteral("YOASOBI")}, QString(), 213233,
                                  {QStringLiteral("偶像")}};
        const auto score = scoreCandidate(query, candidate);
        QCOMPARE(score.title, 1.0);
        QVERIFY(score.titleViaAlternate);
        const QList<RankedCandidate> ranked{{candidate, score}};
        QVERIFY(isAcceptableMatch(ranked.first()));
    }

    void emptyAlternateTitlesScoreIdenticallyToBeforeTheFeature()
    {
        // Regression guard for D-5 (§4): a candidate with no alternateTitles
        // must score exactly as it did before this feature existed.
        const TrackQuery query{QStringLiteral("魔法厨娘"), {QStringLiteral("洛天依")}, QString(), 286100};
        const Candidate candidate{QStringLiteral("423776453"), QStringLiteral("魔法厨娘"),
                                  {QStringLiteral("洛天依")}, QString(), 286066};
        QVERIFY(candidate.alternateTitles.isEmpty());
        const auto score = scoreCandidate(query, candidate);
        QCOMPARE(score.title, 1.0);
        QVERIFY(!score.titleViaAlternate);
        // Weights (0.5/0.2/0.1/0.2) must stay exactly what they were --
        // score.title's own algorithm is the only thing this feature touches.
        QCOMPARE(score.total, score.title * 0.5 + score.artists * 0.2 + score.album * 0.1 + score.duration * 0.2);
    }

    void unrelatedAlternateTitleDoesNotInflateScore()
    {
        // An alternate title that doesn't match the query must not add any
        // score, and must not pull an unrelated candidate over the threshold.
        const TrackQuery query{QStringLiteral("偶像"), {QStringLiteral("YOASOBI")}, QString(), 213234};
        const Candidate candidate{QStringLiteral("9999999"), QStringLiteral("完全不相关的歌名"),
                                  {QStringLiteral("某人")}, QString(), 213233,
                                  {QStringLiteral("另一个不相关的别名")}};
        const auto score = scoreCandidate(query, candidate);
        QCOMPARE(score.title, 0.0);
        QVERIFY(!score.titleViaAlternate);
        const QList<RankedCandidate> ranked{{candidate, score}};
        QVERIFY(!isAcceptableMatch(ranked.first()));
    }
};

QTEST_GUILESS_MAIN(MatcherTest)
#include "tst_matcher.moc"

