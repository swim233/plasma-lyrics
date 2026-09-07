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

    void romanizedTitleRescuedByLocalizedFallback()
    {
        // us storefront romanizes 群青 as "Gunjou"; netease never romanizes
        // Japanese titles, so transNames is None here and the primary title
        // path can't see it -- only chooseMatch's fallback path (D-7/D-8)
        // can. Pool is the measured search response verbatim (SPEC-commit3 §6).
        const TrackQuery query{QStringLiteral("Gunjou"), {QStringLiteral("YOASOBI")}, QString(), 248444};
        const QList<Candidate> candidates{
            {QStringLiteral("1875383422"), QStringLiteral("Gunjou (Yoasobi)"), {QStringLiteral("Vangakuz")}, QString(), 243941},
            {QStringLiteral("2042876595"), QStringLiteral("Gunjou (8-Bit YOASOBI Emulation)"), {QStringLiteral("8-Bit Arcade")}, QString(), 246061},
            {QStringLiteral("1472480890"), QStringLiteral("群青"), {QStringLiteral("YOASOBI")}, QString(), 248444},
            {QStringLiteral("3389005906"), QStringLiteral("オリオン"), {QStringLiteral("YOASOBI")}, QString(), 206586},
            {QStringLiteral("1459343252"), QStringLiteral("たぶん"), {QStringLiteral("YOASOBI")}, QString(), 258666,
             {QStringLiteral("大概")}},
            {QStringLiteral("2042879549"), QStringLiteral("Gunjou (Originally Performed by YOASOBI)"),
             {QStringLiteral("Backing Business")}, QString(), 249324},
            {QStringLiteral("3323596738"), QStringLiteral("GUNJOU (Cover)"), {QStringLiteral("Omnixor")}, QString(), 262153},
            {QStringLiteral("2083182016"), QStringLiteral("勇者"), {QStringLiteral("YOASOBI")}, QString(), 194164},
            {QStringLiteral("2034742057"), QStringLiteral("アイドル"), {QStringLiteral("YOASOBI")}, QString(), 213233,
             {QStringLiteral("偶像")}},
            {QStringLiteral("1803908863"), QStringLiteral("怪物"), {QStringLiteral("YOASOBI")}, QString(), 206000}};
        const auto ranked = rankCandidates(query, candidates);
        // The literal title match ("GUNJOU (Cover)" / Omnixor) must not win --
        // wrong artist, real duration delta of 13709 ms sinks its total below
        // 0.58, which is why isAcceptableMatch(ranked.first()) fails and the
        // fallback path has to run at all.
        QVERIFY(!isAcceptableMatch(ranked.first()));
        const auto chosen = chooseMatch(ranked, true);
        QVERIFY(chosen.has_value());
        QCOMPARE(chosen->candidate.trackId, QStringLiteral("1472480890"));
    }

    void localizedTitleStillCaughtByPrimaryPath()
    {
        // Same 偶像/アイドル pair as the transNames tests above, but this time
        // through chooseMatch end-to-end with the full measured candidate
        // pool: the primary path (transNames already lifted score.title to
        // 1.0) must win outright, without needing the fallback at all.
        const TrackQuery query{QStringLiteral("偶像"), {QStringLiteral("YOASOBI")}, QString(), 213234};
        const QList<Candidate> candidates{
            {QStringLiteral("2034742057"), QStringLiteral("アイドル"), {QStringLiteral("YOASOBI")}, QString(), 213233,
             {QStringLiteral("偶像")}},
            {QStringLiteral("2083870325"), QStringLiteral("アイドル"), {QStringLiteral("YOASOBI")}, QString(), 211733,
             {QStringLiteral("偶像")}},
            {QStringLiteral("2599476826"), QStringLiteral("アイドル Yoasobi"), {QStringLiteral("Get A Better Beat")},
             QString(), 170666, {QStringLiteral("偶像 Yoasobi")}},
            {QStringLiteral("1472480890"), QStringLiteral("群青"), {QStringLiteral("YOASOBI")}, QString(), 248444},
            {QStringLiteral("2049587772"), QStringLiteral("アイドル (偶像)"), {QStringLiteral("Cherry酱")}, QString(), 213233},
            {QStringLiteral("2048049634"), QStringLiteral("YOASOBI - アイドル（YunFuCola remix）"),
             {QStringLiteral("YunFu")}, QString(), 213241, {QStringLiteral("偶像")}},
            {QStringLiteral("2041800151"), QStringLiteral("偶像(アイドル)【Cvoer：YOASOBI】"), {QStringLiteral("兰音Reine")},
             QString(), 215421},
            {QStringLiteral("2042621628"), QStringLiteral("偶像"), {QStringLiteral("カンザキイオリ")}, QString(), 292633},
            {QStringLiteral("3389005906"), QStringLiteral("オリオン"), {QStringLiteral("YOASOBI")}, QString(), 206586},
            {QStringLiteral("1459343252"), QStringLiteral("たぶん"), {QStringLiteral("YOASOBI")}, QString(), 258666,
             {QStringLiteral("大概")}}};
        const auto ranked = rankCandidates(query, candidates);
        QVERIFY(isAcceptableMatch(ranked.first()));
        QCOMPARE(ranked.first().candidate.trackId, QStringLiteral("2034742057"));
        // Prove it's the primary path, not the fallback, that wins: disabling
        // the fallback must not change the outcome.
        const auto chosen = chooseMatch(ranked, false);
        QVERIFY(chosen.has_value());
        QCOMPARE(chosen->candidate.trackId, QStringLiteral("2034742057"));
    }

    void dedupeGuardTreatsSameSongUnderDifferentIdsAsOne()
    {
        // netease commonly lists the same song under several track ids
        // (D-9); the fallback's uniqueness check must dedupe by
        // (title, artists) before counting, or a genuine match gets rejected
        // as ambiguous against itself.
        const TrackQuery query{QStringLiteral("Gunjou"), {QStringLiteral("YOASOBI")}, QString(), 248444};
        const QList<Candidate> candidates{
            {QStringLiteral("1472480890"), QStringLiteral("群青"), {QStringLiteral("YOASOBI")}, QString(), 248444},
            {QStringLiteral("1472480891"), QStringLiteral("群青"), {QStringLiteral("YOASOBI")}, QString(), 248444},
            {QStringLiteral("2042879549"), QStringLiteral("Gunjou (Originally Performed by YOASOBI)"),
             {QStringLiteral("Backing Business")}, QString(), 249324}};
        const auto ranked = rankCandidates(query, candidates);
        QVERIFY(!isAcceptableMatch(ranked.first()));
        const auto chosen = chooseMatch(ranked, true);
        QVERIFY(chosen.has_value());
        QVERIFY(chosen->candidate.trackId == QStringLiteral("1472480890")
                || chosen->candidate.trackId == QStringLiteral("1472480891"));
    }

    void genuineAmbiguityIsRejected()
    {
        // Same artist, two genuinely different songs both inside the
        // fallback's 250ms duration window (DESIGN.md decision 45) -- this
        // must stay rejected, not just deduped away.
        const TrackQuery query{QStringLiteral("Gunjou"), {QStringLiteral("YOASOBI")}, QString(), 248444};
        const QList<Candidate> candidates{
            {QStringLiteral("A1"), QStringLiteral("群青"), {QStringLiteral("YOASOBI")}, QString(), 248444},
            {QStringLiteral("B1"), QStringLiteral("怪物"), {QStringLiteral("YOASOBI")}, QString(), 248500},
            {QStringLiteral("C1"), QStringLiteral("Filler"), {QStringLiteral("Nobody")}, QString(), 100000}};
        const auto ranked = rankCandidates(query, candidates);
        QVERIFY(!isAcceptableMatch(ranked.first()));
        QVERIFY(!chooseMatch(ranked, true).has_value());
    }

    void unfilteredNeighborCannotImpersonateAFilteredOutRealSong()
    {
        // qa-2's counterexample: if the real song's duration falls outside
        // the fallback's duration window (source/netease length disagree,
        // or netease indexed a different edit) while a same-artist but
        // genuinely different song happens to land inside the window
        // *alone*, all four gates (artists, duration, pool size, dedupe
        // uniqueness) can pass for the wrong song once the real one has
        // already been filtered out by this same duration gate -- "unique"
        // among survivors doesn't mean "unique among candidates".
        // Tightening the window to 250ms (DESIGN.md decision 45) closes
        // this specific instance: the wrong song's deltaMs=1000 no longer
        // survives either, so neither song does and chooseMatch must
        // return nullopt rather than confidently picking the wrong one.
        const TrackQuery query{QStringLiteral("Gunjou"), {QStringLiteral("YOASOBI")}, QString(), 200000};
        const QList<Candidate> candidates{
            {QStringLiteral("real"), QStringLiteral("群青"), {QStringLiteral("YOASOBI")}, QString(), 210500},
            {QStringLiteral("wrong"), QStringLiteral("アイドル"), {QStringLiteral("YOASOBI")}, QString(), 201000},
            {QStringLiteral("filler"), QStringLiteral("Filler"), {QStringLiteral("Nobody")}, QString(), 100000}};
        const auto ranked = rankCandidates(query, candidates);
        QVERIFY(!isAcceptableMatch(ranked.first()));
        QVERIFY(!chooseMatch(ranked, true).has_value());
    }

    void dedupeKeyUsesNormalizedTitleAndArtists()
    {
        // D-9's dedupe key must be built from normalizeSearchText(cleanTitle(...))
        // and cleanArtists(...), not the raw fields -- two survivor rows for
        // the same song that merely differ in case or incidental whitespace
        // (both realistic netease noise) must still merge into one group
        // and be accepted, not rejected as a false ambiguity.
        const TrackQuery query{QStringLiteral("Gunjou"), {QStringLiteral("YOASOBI")}, QString(), 248444};
        const QList<Candidate> candidates{
            {QStringLiteral("1"), QStringLiteral("群青"), {QStringLiteral("YOASOBI")}, QString(), 248444},
            {QStringLiteral("2"), QStringLiteral(" 群青 "), {QStringLiteral("yoasobi")}, QString(), 248444},
            {QStringLiteral("3"), QStringLiteral("Filler"), {QStringLiteral("Nobody")}, QString(), 100000}};
        const auto ranked = rankCandidates(query, candidates);
        QVERIFY(!isAcceptableMatch(ranked.first()));
        const auto chosen = chooseMatch(ranked, true);
        QVERIFY(chosen.has_value());
        QVERIFY(chosen->candidate.trackId == QStringLiteral("1") || chosen->candidate.trackId == QStringLiteral("2"));
    }

    void fallbackRequiresPlatformFlag()
    {
        // Same romanized-title input as romanizedTitleRescuedByLocalizedFallback,
        // but with allowLocalizedFallback=false (D-7: fallback is gated to the
        // apple platform in Resolver) -- must come back empty.
        const TrackQuery query{QStringLiteral("Gunjou"), {QStringLiteral("YOASOBI")}, QString(), 248444};
        const QList<Candidate> candidates{
            {QStringLiteral("1875383422"), QStringLiteral("Gunjou (Yoasobi)"), {QStringLiteral("Vangakuz")}, QString(), 243941},
            {QStringLiteral("1472480890"), QStringLiteral("群青"), {QStringLiteral("YOASOBI")}, QString(), 248444},
            {QStringLiteral("3323596738"), QStringLiteral("GUNJOU (Cover)"), {QStringLiteral("Omnixor")}, QString(), 262153}};
        const auto ranked = rankCandidates(query, candidates);
        QVERIFY(!chooseMatch(ranked, false).has_value());
    }

    void diagnosticReflectsActualNonAppleDecision()
    {
        const TrackQuery query{QStringLiteral("Gunjou"), {QStringLiteral("YOASOBI")}, QString(), 248444};
        const QList<Candidate> candidates{
            {QStringLiteral("1875383422"), QStringLiteral("Gunjou (Yoasobi)"), {QStringLiteral("Vangakuz")}, QString(), 243941},
            {QStringLiteral("1472480890"), QStringLiteral("群青"), {QStringLiteral("YOASOBI")}, QString(), 248444},
            {QStringLiteral("3323596738"), QStringLiteral("GUNJOU (Cover)"), {QStringLiteral("Omnixor")}, QString(), 262153}};

        const QString actual = explainMatch(query, candidates, false, true);
        QVERIFY(actual.contains(QStringLiteral("selected: none\n")));
        QVERIFY(actual.contains(QStringLiteral("would-select-with-fallback: 1472480890\n")));

        const QString platformUnknown = explainMatch(query, candidates, true, false);
        QVERIFY(!platformUnknown.contains(QStringLiteral("\nselected:")));
        QVERIFY(platformUnknown.contains(QStringLiteral("would-select-with-fallback: 1472480890\n")));
    }

    void poolGateRejectsTooFewCandidates()
    {
        // Only two candidates in the pool -- "unique" would be free even
        // though both otherwise clear the survivor bar (D-8's pool >= 3 gate).
        const TrackQuery query{QStringLiteral("Gunjou"), {QStringLiteral("YOASOBI")}, QString(), 248444};
        const QList<Candidate> candidates{
            {QStringLiteral("1472480890"), QStringLiteral("群青"), {QStringLiteral("YOASOBI")}, QString(), 248444},
            {QStringLiteral("1803908863"), QStringLiteral("怪物"), {QStringLiteral("YOASOBI")}, QString(), 206000}};
        const auto ranked = rankCandidates(query, candidates);
        QVERIFY(!isAcceptableMatch(ranked.first()));
        QVERIFY(!chooseMatch(ranked, true).has_value());
    }

    void missingDurationCannotPassTheDurationGate()
    {
        // scoreCandidate sets durationDifferenceMs to 0 whenever either side
        // is missing a length; without checking durationComparable a
        // candidate that has no known length at all would sail through the
        // fallback's duration gate for free.
        const TrackQuery query{QStringLiteral("Gunjou"), {QStringLiteral("YOASOBI")}, QString(), 248444};
        const QList<Candidate> candidates{
            {QStringLiteral("Z1"), QStringLiteral("怪物"), {QStringLiteral("YOASOBI")}, QString(), 0},
            {QStringLiteral("Z2"), QStringLiteral("Filler"), {QStringLiteral("Nobody")}, QString(), 100000},
            {QStringLiteral("Z3"), QStringLiteral("Filler2"), {QStringLiteral("Nobody2")}, QString(), 100000}};
        const auto ranked = rankCandidates(query, candidates);
        QVERIFY(!isAcceptableMatch(ranked.first()));
        QVERIFY(!chooseMatch(ranked, true).has_value());
    }

    void aliasArtistGateBlocksWrongArtistCover()
    {
        // D-11: a cover with the wrong artist can reach score.title=1.0 via
        // an alternate title that netease propagates onto covers/remixes,
        // and total~=0.73 clears both isAcceptableMatch thresholds. Measured
        // data: YunFuCola's remix of アイドル carries transNames=['偶像']
        // just like the original, but artists is 'YunFu', not 'YOASOBI'.
        const TrackQuery query{QStringLiteral("偶像"), {QStringLiteral("YOASOBI")}, QString(), 213234};
        const Candidate candidate{QStringLiteral("2048049634"), QStringLiteral("YOASOBI - アイドル（YunFuCola remix）"),
                                  {QStringLiteral("YunFu")}, QString(), 213241, {QStringLiteral("偶像")}};
        const auto score = scoreCandidate(query, candidate);
        QCOMPARE(score.title, 1.0);
        QVERIFY(score.titleViaAlternate);
        QVERIFY(score.artists < 0.5);
        QVERIFY(isAcceptableMatch({candidate, score}));
        // isAcceptableMatch alone would accept this; chooseMatch's D-11 gate
        // on its return points must reject it regardless of the fallback flag.
        const QList<RankedCandidate> ranked{{candidate, score}};
        QVERIFY(!chooseMatch(ranked, false).has_value());
    }

    void aliasArtistGateDoesNotRejectTheRealSong()
    {
        // Same alternate-title mechanism, but the real song: artists=1.0
        // must still be accepted through the D-11 gate.
        const TrackQuery query{QStringLiteral("偶像"), {QStringLiteral("YOASOBI")}, QString(), 213234};
        const Candidate candidate{QStringLiteral("2034742057"), QStringLiteral("アイドル"),
                                  {QStringLiteral("YOASOBI")}, QString(), 213233, {QStringLiteral("偶像")}};
        const auto score = scoreCandidate(query, candidate);
        QVERIFY(score.titleViaAlternate);
        QCOMPARE(score.artists, 1.0);
        const QList<RankedCandidate> ranked{{candidate, score}};
        const auto chosen = chooseMatch(ranked, false);
        QVERIFY(chosen.has_value());
        QCOMPARE(chosen->candidate.trackId, QStringLiteral("2034742057"));
    }
};

QTEST_GUILESS_MAIN(MatcherTest)
#include "tst_matcher.moc"
