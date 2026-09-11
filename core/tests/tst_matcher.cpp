#include "core/match/matcher.h"

#include <QTest>
#include <algorithm>

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

    void bilingualGlossParentheticalRescuesCorrectCandidate()
    {
        // Regression for the actual bug report. The fixture is a
        // constructed scenario with matching lengthMs/album on both sides
        // -- not the real API response verbatim (qa-2-match measured
        // candidate 1's real dt as 198000ms, not this fixture's 213233;
        // the fixture's own two sides just need to agree with each other,
        // which they do, giving deltaMs=0 either way) -- built to isolate
        // score.title as the only thing that differs between the two
        // candidates, matching the user's original log:
        //   1. [3363002263] 青さは止んだ — ナナツカゼ total=0.773 title=0.545
        //      artists=1.000 album=1.000 duration=1.000 deltaMs=0
        //   2. [3363001374] 青さは止んだ (Instrumental) — ナナツカゼ total=0.684
        //      title=0.368 artists=1.000 album=1.000 duration=1.000 deltaMs=0
        // Before this fix, candidate 1's score.title landed at 6/11 = 0.545
        // -- just under the 0.55 title-threshold -- so it was rejected
        // purely on the title gate despite everything else lining up. The
        // query-side variant that strips the untranslated bracket now
        // scores candidate 1 an exact title match (verified against the
        // pre-fix code: this fixture reproduces 0.545/0.773 exactly).
        // Candidate 2 (Instrumental) must stay rejected at exactly its old
        // score. Verified by instrumenting textSimilarity's two branches
        // (not by derivation -- the two candidates' normalized titles are
        // "青さは止んだ 青春已逝" (11, original variant) and "青さは止んだ"
        // (6, stripped variant) against candidate 2's normalized
        // "青さは止んだ instrumental" (19): the *stripped* variant is a
        // prefix of candidate 2's title and takes the containment
        // fast-path, scoring only 6/19 = 0.316; the *original,
        // un-stripped* variant shares no such containment and takes the
        // full Levenshtein path, scoring 7/19 = 0.368 (matching the old,
        // pre-fix score exactly, since that path is untouched by this fix).
        // The max of the two stays at the Levenshtein score, unchanged.
        const TrackQuery query{QStringLiteral("青さは止んだ (青春已逝)"),
                               {QStringLiteral("ナナツカゼ")}, QStringLiteral("青さは止んだ"), 213233};
        const QList<Candidate> candidates{
            {QStringLiteral("3363002263"), QStringLiteral("青さは止んだ"),
             {QStringLiteral("ナナツカゼ")}, QStringLiteral("青さは止んだ"), 213233},
            {QStringLiteral("3363001374"), QStringLiteral("青さは止んだ (Instrumental)"),
             {QStringLiteral("ナナツカゼ")}, QStringLiteral("青さは止んだ"), 213233}};

        const auto ranked = rankCandidates(query, candidates);

        QCOMPARE(ranked.first().candidate.trackId, QStringLiteral("3363002263"));
        QCOMPARE(ranked.first().score.title, 1.0);
        QCOMPARE(ranked.first().score.artists, 1.0);
        QCOMPARE(ranked.first().score.album, 1.0);
        QCOMPARE(ranked.first().score.duration, 1.0);
        QCOMPARE(ranked.first().score.total, 1.0);
        // Candidate 1's winning title comparison came through the
        // gloss-stripped query variant (see the mechanism note above), so
        // this is exactly the case passesGlossVariantGate must let
        // through: it does, because deltaMs=0 here, same as the real bug
        // report -- glossVariantDurationGateRejectsATooShortSameArtistTrack
        // is the same shape with a large deltaMs instead, and must reject.
        QVERIFY(ranked.first().score.titleViaGlossVariant);
        QVERIFY(isAcceptableMatch(ranked.first()));

        const auto instrumental = std::find_if(ranked.cbegin(), ranked.cend(), [](const auto &item) {
            return item.candidate.trackId == QStringLiteral("3363001374");
        });
        QVERIFY(instrumental != ranked.cend());
        QCOMPARE(instrumental->score.title, 7.0 / 19.0);
        QCOMPARE(instrumental->score.total, 0.5 * (7.0 / 19.0) + 0.2 + 0.1 + 0.2);
        // Pin the mechanism, not just the score: considerTitle only takes a
        // new leader on a strict ">", so the gloss-stripped variant's 6/19
        // = 0.316 can never beat the original variant's 7/19 = 0.368 for
        // this candidate -- titleViaGlossVariant must stay false, and with
        // qa-2-match's real-API total (0.584) also already >= 0.58, the
        // only thing still blocking this candidate, in the fixture and for
        // real, is the 0.55 title-threshold.
        QVERIFY(!instrumental->score.titleViaGlossVariant);
        QVERIFY(!isAcceptableMatch(*instrumental));
        QCOMPARE(candidateRejectionReason(*instrumental), QStringLiteral("title-threshold"));
    }

    void preserveVersionsGlossStrippingNeverAppliesToVersionMarkers()
    {
        // Counter-example a: a trailing bracket that carries a version
        // marker ("Live") must never be treated as a strippable gloss --
        // otherwise "Example Song (Live)" would gain a bare "Example Song"
        // query variant that scores an *exact* title match (1.0) against
        // the studio candidate, letting an unrelated recording get chosen
        // as if it were the requested live performance (DESIGN.md decision
        // 54). Left unstripped, the two titles' own similarity is a fixed
        // 12/17 = 0.706 via their shared "Example Song" prefix -- this
        // exact value must be identical before and after this fix; any
        // change (up to and including hitting 1.0) means the bracket
        // started getting stripped.
        const TrackQuery query{QStringLiteral("Example Song (Live)"),
                               {QStringLiteral("Singer")}, QString(), 0};
        const Candidate candidate{QStringLiteral("studio"), QStringLiteral("Example Song"),
                                  {QStringLiteral("Singer")}, QString(), 0};

        const auto score = scoreCandidate(query, candidate, MatchPolicy::PreserveVersions);

        QCOMPARE(score.versionTier, VersionTier::OneSided);
        QCOMPARE(score.title, 12.0 / 17.0);
    }

    void preserveVersionsGlossStrippingNeverAppliesToCleanTitleOnlyMarkers()
    {
        // Counter-example c: regression for the union-guard rework. "版"
        // and "ver" are markers cleanTitle's own regex recognizes but
        // versionEvidence()'s table does not carry on their own (only
        // compound forms like 特别版/周年...版) -- before the guard became
        // the union of both tables, "歌名 (2024版)" and "Song (Ver. 2)"
        // would have been read as harmless localized aliases and
        // stripped, letting a query for one version exact-match a
        // candidate of a different, unversioned recording.
        // Both values pinned exactly (same rigor as counter-example a's
        // 12/17): "歌名 (2024版)" normalizes to "歌名 2024版" (8 chars)
        // which contains the unstripped candidate "歌名" (2 chars) ->
        // 2/8 = 0.25. "Song (Ver. 2)" normalizes to "song ver 2" (10
        // chars) containing "song" (4 chars) -> 4/10 = 0.4. Neither is
        // 1.0, so the bracket was not stripped.
        const TrackQuery queryVer{QStringLiteral("歌名 (2024版)"), {QStringLiteral("Singer")}, QString(), 0};
        const Candidate candidateVer{QStringLiteral("plain-ver"), QStringLiteral("歌名"),
                                     {QStringLiteral("Singer")}, QString(), 0};
        const auto scoreVer = scoreCandidate(queryVer, candidateVer, MatchPolicy::PreserveVersions);
        QCOMPARE(scoreVer.title, 2.0 / 8.0);

        const TrackQuery queryEn{QStringLiteral("Song (Ver. 2)"), {QStringLiteral("Singer")}, QString(), 0};
        const Candidate candidateEn{QStringLiteral("plain-en"), QStringLiteral("Song"),
                                    {QStringLiteral("Singer")}, QString(), 0};
        const auto scoreEn = scoreCandidate(queryEn, candidateEn, MatchPolicy::PreserveVersions);
        QCOMPARE(scoreEn.title, 4.0 / 10.0);
    }

    void splitTrailingGlossStripsAHarmlessLocalizedAlias()
    {
        QString main;
        QVERIFY(splitTrailingGloss(QStringLiteral("青さは止んだ (青春已逝)"), &main));
        QCOMPARE(main, QStringLiteral("青さは止んだ"));
    }

    void splitTrailingGlossNeverStripsVersionEvidenceMarkers()
    {
        QString main;
        QVERIFY(!splitTrailingGloss(QStringLiteral("Example Song (Live)"), &main));
        QVERIFY(!splitTrailingGloss(QStringLiteral("青さは止んだ (Instrumental)"), &main));
    }

    void splitTrailingGlossNeverStripsCleanTitleOnlyMarkers()
    {
        // "版" and "ver" are markers cleanTitle's own regex recognizes but
        // versionEvidence()'s table does not carry on their own. The guard
        // must be the union of both tables, not just versionEvidence's.
        QString main;
        QVERIFY(!splitTrailingGloss(QStringLiteral("歌名 (2024版)"), &main));
        QVERIFY(!splitTrailingGloss(QStringLiteral("Song (Ver. 2)"), &main));
    }

    void splitTrailingGlossHasNothingToStripWithoutATrailingBracket()
    {
        QString main;
        QVERIFY(!splitTrailingGloss(QStringLiteral("Plain Title"), &main));
    }

    void unrelatedBracketedAsideCannotDragAWrongSongOverThreshold()
    {
        // Counter-example b: splitTrailingGloss only requires the bracket
        // to carry no version marker before stripping it, so a completely
        // unrelated aside in parentheses also produces an extra query
        // variant (the title with it removed). That variant shares no
        // more than incidental punctuation (e.g. the word-separating
        // space) with an unrelated candidate, so it may not drag its
        // score anywhere near the 0.55 acceptance threshold.
        const TrackQuery query{QStringLiteral("真正的歌 (完全无关的备注文字)"),
                               {QStringLiteral("对的人")}, QString(), 0};
        const Candidate candidate{QStringLiteral("wrong"), QStringLiteral("Unrelated English Title"),
                                  {QStringLiteral("Nobody")}, QString(), 0};

        const auto score = scoreCandidate(query, candidate);

        QVERIFY(score.title < 0.55);
        QVERIFY(!isAcceptableMatch({candidate, score}));
    }

    void glossVariantDurationGateRejectsATooShortSameArtistTrack()
    {
        // qa-match's counterexample: this is the entire reason
        // passesGlossVariantGate exists. "心跳 (跳动的心)" strips to "心跳",
        // which exact-matches a same-artist candidate that is actually a
        // different, much shorter recording (200s query vs. 15s
        // candidate) -- not a translation at all, just an album track
        // that happens to share the stripped title. Before this gate,
        // artists=1.0 and title=1.0 alone cleared both thresholds
        // (total=0.7) despite score.duration having already collapsed to
        // 0.0 -- duration's 0.2 weight isn't enough on its own to block
        // an exact title+artist match (DESIGN.md decision 45: no lyrics
        // rather than wrong lyrics).
        const TrackQuery query{QStringLiteral("心跳 (跳动的心)"), {QStringLiteral("歌手A")}, QString(), 200000};
        const Candidate candidate{QStringLiteral("wrong"), QStringLiteral("心跳"),
                                  {QStringLiteral("歌手A")}, QString(), 15000};

        const auto score = scoreCandidate(query, candidate);

        QCOMPARE(score.title, 1.0);
        QCOMPARE(score.artists, 1.0);
        QCOMPARE(score.duration, 0.0);
        QCOMPARE(score.total, 0.7);
        QVERIFY(score.titleViaGlossVariant);
        QVERIFY(!isAcceptableMatch({candidate, score}));
        QCOMPARE(candidateRejectionReason({candidate, score}), QStringLiteral("gloss-duration-threshold"));
    }

    void glossVariantMustMatchExactlyToCount()
    {
        // qa-2-match's second counterexample, distinct from the one above:
        // "心跳" (the stripped variant of "心跳 (跳动的心)") is a strict
        // prefix of "心跳吧", a same-artist but genuinely different song,
        // so it would score 2/3 = 0.667 via textSimilarity's containment
        // fast-path -- comfortably over the 0.55 title-threshold, and
        // higher than the 0.286 the un-stripped query correctly gets via
        // Levenshtein (baseline behavior, unaffected by this fix). A
        // gloss-stripped variant now only participates in scoreCandidate
        // on an exact match, so this candidate is scored on the
        // un-stripped variant alone and stays rejected, matching the
        // pre-fix (baseline) outcome exactly.
        const TrackQuery query{QStringLiteral("心跳 (跳动的心)"), {QStringLiteral("歌手A")}, QString(), 0};
        const Candidate candidate{QStringLiteral("wrong"), QStringLiteral("心跳吧"),
                                  {QStringLiteral("歌手A")}, QString(), 0};

        const auto score = scoreCandidate(query, candidate);

        QVERIFY(!score.titleViaGlossVariant);
        QCOMPARE(score.title, 2.0 / 7.0);
        QVERIFY(!isAcceptableMatch({candidate, score}));
        QCOMPARE(candidateRejectionReason({candidate, score}), QStringLiteral("title-threshold"));
    }

    void glossVariantDurationGateBoundaryIsInclusiveOf2000ms()
    {
        // The gate's window matches scoreCandidate's own duration curve
        // breakpoint (2000ms, decision 12) exactly, including at the
        // boundary: <=2000ms passes, one millisecond more does not.
        const TrackQuery query{QStringLiteral("Song (Alt Title)"),
                               {QStringLiteral("Artist")}, QString(), 200000};
        const Candidate atBoundary{QStringLiteral("at-boundary"), QStringLiteral("Song"),
                                  {QStringLiteral("Artist")}, QString(), 202000};
        const Candidate overBoundary{QStringLiteral("over-boundary"), QStringLiteral("Song"),
                                     {QStringLiteral("Artist")}, QString(), 202001};

        const auto atScore = scoreCandidate(query, atBoundary);
        QVERIFY(atScore.titleViaGlossVariant);
        QCOMPARE(atScore.durationDifferenceMs, qint64(2000));
        QVERIFY(isAcceptableMatch({atBoundary, atScore}));

        const auto overScore = scoreCandidate(query, overBoundary);
        QVERIFY(overScore.titleViaGlossVariant);
        QCOMPARE(overScore.durationDifferenceMs, qint64(2001));
        QVERIFY(!isAcceptableMatch({overBoundary, overScore}));
        QCOMPARE(candidateRejectionReason({overBoundary, overScore}), QStringLiteral("gloss-duration-threshold"));
    }

    void glossVariantDurationGateRejectsWhenEitherSideDurationIsUnknown()
    {
        // Conservative direction (DESIGN.md decision 45): an unknown
        // duration cannot pass this gate, unlike passesAliasArtistGate's
        // artist bar, which only asks for a similarity score, known or
        // not. Erring toward rejection here only ever regresses to
        // today's not-found, never to a wrong match. But this is a
        // *different* reason than a known duration outside the window
        // (see glossVariantDurationGateRejectsATooShortSameArtistTrack):
        // there's an actionable fix here (add a length), so it gets its
        // own rejection reason (qa-match: a local directory search
        // candidate from a .lrc with no [length:] tag is exactly this
        // shape -- candidate.lengthMs=0 even though the query has a real
        // duration). The reverse -- the query side missing a duration --
        // hits the same path and isn't local-source-specific.
        const TrackQuery queryWithDuration{QStringLiteral("Song (Alt Title)"),
                                           {QStringLiteral("Artist")}, QString(), 200000};
        const Candidate candidateNoDuration{QStringLiteral("no-duration"), QStringLiteral("Song"),
                                            {QStringLiteral("Artist")}, QString(), 0};
        const auto candidateSideScore = scoreCandidate(queryWithDuration, candidateNoDuration);
        QVERIFY(candidateSideScore.titleViaGlossVariant);
        QVERIFY(!candidateSideScore.durationComparable);
        QVERIFY(!isAcceptableMatch({candidateNoDuration, candidateSideScore}));
        QCOMPARE(candidateRejectionReason({candidateNoDuration, candidateSideScore}),
                 QStringLiteral("gloss-duration-unknown"));

        const TrackQuery queryNoDuration{QStringLiteral("Song (Alt Title)"),
                                         {QStringLiteral("Artist")}, QString(), 0};
        const Candidate candidateWithDuration{QStringLiteral("with-duration"), QStringLiteral("Song"),
                                              {QStringLiteral("Artist")}, QString(), 200000};
        const auto querySideScore = scoreCandidate(queryNoDuration, candidateWithDuration);
        QVERIFY(querySideScore.titleViaGlossVariant);
        QVERIFY(!querySideScore.durationComparable);
        QVERIFY(!isAcceptableMatch({candidateWithDuration, querySideScore}));
        QCOMPARE(candidateRejectionReason({candidateWithDuration, querySideScore}),
                 QStringLiteral("gloss-duration-unknown"));
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

    void defaultPolicyChooseMatchSkipsADisqualifiedTopRankToTheNextAcceptable()
    {
        // A gate disqualifies a candidate; it does not mean "lower
        // quality" -- so a candidate a gate disqualifies must not go on
        // blocking an acceptable one further down, under MatchPolicy::
        // Default just as much as under PreserveVersions (which already
        // looped over every candidate in a tier, not just the first).
        // "top" here alias-gate-fails (wrong artist via an alternate
        // title) but still ranks first by total (0.8, from an exact
        // album/duration match); "second" is the real song, correctly
        // artist-matched, but ranks below "top" on total (0.773, from a
        // looser duration match) alone.
        const TrackQuery query{QStringLiteral("Song"), {QStringLiteral("Real Artist")},
                               QStringLiteral("Album"), 200000};
        const Candidate top{QStringLiteral("wrong-artist-cover"), QStringLiteral("Other Title"),
                            {QStringLiteral("Nobody")}, QStringLiteral("Album"), 200000,
                            {QStringLiteral("Song")}};
        const Candidate second{QStringLiteral("real-song"), QStringLiteral("Song"),
                               {QStringLiteral("Real Artist")}, QString(), 210000};

        const auto ranked = rankCandidates(query, {top, second});

        QCOMPARE(ranked.first().candidate.trackId, QStringLiteral("wrong-artist-cover"));
        QVERIFY(isAcceptableMatch(ranked.first()));
        // passesAliasArtistGate's own condition (D-11), spelled out since
        // the gate itself isn't exported: titleViaAlternate is true and
        // artists is well under 0.5, so it fails.
        QVERIFY(ranked.first().score.titleViaAlternate);
        QVERIFY(ranked.first().score.artists < 0.5);
        QVERIFY(isAcceptableMatch(ranked.last()));

        const auto chosen = chooseMatch(ranked, false);
        QVERIFY(chosen.has_value());
        QCOMPARE(chosen->candidate.trackId, QStringLiteral("real-song"));
    }

    void preserveVersionsRanksEveryNormalCandidateBeforeOneSidedCandidates()
    {
        const TrackQuery query{QStringLiteral("Example Song (Live)"),
                               {QStringLiteral("Singer")}, QStringLiteral("Album"), 0};
        const QList<Candidate> candidates{
            {QStringLiteral("studio"), QStringLiteral("Example Song"),
             {QStringLiteral("Singer")}, QStringLiteral("Album"), 0},
            {QStringLiteral("live"), QStringLiteral("Example Song - Live"),
             {QStringLiteral("Singer")}, QStringLiteral("Other Album"), 0}};

        const auto ranked = rankCandidates(query, candidates,
                                           MatchPolicy::PreserveVersions);

        QCOMPARE(ranked.first().candidate.trackId, QStringLiteral("live"));
        QCOMPARE(ranked.first().score.versionTier, VersionTier::Normal);
        QCOMPARE(ranked.last().score.versionTier, VersionTier::OneSided);
        const auto chosen = chooseMatch(ranked, false);
        QVERIFY(chosen.has_value());
        QCOMPARE(chosen->candidate.trackId, QStringLiteral("live"));
    }

    void preserveVersionsAllowsOneSidedOnlyWhenNoNormalMatchExists()
    {
        const TrackQuery query{QStringLiteral("Example Song"),
                               {QStringLiteral("Singer")}, QString(), 0};
        const QList<Candidate> candidates{
            {QStringLiteral("live"), QStringLiteral("Example Song (Live)"),
             {QStringLiteral("Singer")}, QString(), 0}};

        const auto ranked = rankCandidates(query, candidates,
                                           MatchPolicy::PreserveVersions);

        QCOMPARE(ranked.first().score.versionTier, VersionTier::OneSided);
        QVERIFY(isAcceptableMatch(ranked.first()));
        const auto chosen = chooseMatch(ranked, false);
        QVERIFY(chosen.has_value());
        QCOMPARE(chosen->candidate.trackId, QStringLiteral("live"));
    }

    void preserveVersionsRejectsConflictingVersionMarkers()
    {
        const TrackQuery query{QStringLiteral("Example Song (Live)"),
                               {QStringLiteral("Singer")}, QString(), 0};
        const Candidate candidate{QStringLiteral("remix"),
                                  QStringLiteral("Example Song (Remix)"),
                                  {QStringLiteral("Singer")}, QString(), 0};

        const auto score = scoreCandidate(query, candidate,
                                          MatchPolicy::PreserveVersions);

        QCOMPARE(score.versionTier, VersionTier::Conflict);
        QCOMPARE(score.rejectionReason, QStringLiteral("version-conflict"));
        QVERIFY(!isAcceptableMatch({candidate, score}));
        QVERIFY(!chooseMatch({{candidate, score}}, false).has_value());
        const auto explanation = explainMatch(query, {candidate}, false, true,
                                              MatchPolicy::PreserveVersions);
        QVERIFY(explanation.contains(QStringLiteral("versionTier=conflict")));
        QVERIFY(explanation.contains(QStringLiteral("rejected=version-conflict")));
        QVERIFY(explanation.contains(QStringLiteral("selected: none")));
    }

    void localizedFallbackCannotSelectAConflictingVersion()
    {
        const TrackQuery query{QStringLiteral("Gunjou (Live)"),
                               {QStringLiteral("YOASOBI")}, QString(), 248444};
        const QList<Candidate> candidates{
            {QStringLiteral("conflict"), QStringLiteral("群青 (Remix)"),
             {QStringLiteral("YOASOBI")}, QString(), 248444},
            {QStringLiteral("filler-1"), QStringLiteral("Unrelated One"),
             {QStringLiteral("Nobody")}, QString(), 100000},
            {QStringLiteral("filler-2"), QStringLiteral("Unrelated Two"),
             {QStringLiteral("Somebody")}, QString(), 300000}};

        const auto ranked = rankCandidates(query, candidates,
                                           MatchPolicy::PreserveVersions);
        const auto conflict = std::find_if(ranked.cbegin(), ranked.cend(), [](const auto &item) {
            return item.candidate.trackId == QStringLiteral("conflict");
        });
        QVERIFY(conflict != ranked.cend());
        QCOMPARE(conflict->score.versionTier, VersionTier::Conflict);
        QVERIFY(!chooseMatch(ranked, true).has_value());
    }

    void unversionedAliasCannotWashOutExplicitVersionConflict()
    {
        const TrackQuery query{QStringLiteral("Example Song (Remix)"),
                               {QStringLiteral("Singer")}, QString(), 0};
        const Candidate candidate{QStringLiteral("live"),
                                  QStringLiteral("Example Song (Live)"),
                                  {QStringLiteral("Singer")}, QString(), 0,
                                  {QStringLiteral("Example Song")}};

        const auto score = scoreCandidate(query, candidate,
                                          MatchPolicy::PreserveVersions);

        QCOMPARE(score.versionTier, VersionTier::Conflict);
        QCOMPARE(score.rejectionReason, QStringLiteral("version-conflict"));
        QVERIFY(!chooseMatch({{candidate, score}}, false).has_value());
    }

    void differentRemasterYearsAreConflictingVersions()
    {
        const TrackQuery query{QStringLiteral("Example Song (2020 Remaster)"),
                               {QStringLiteral("Singer")}, QString(), 0};
        const Candidate candidate{QStringLiteral("2021"),
                                  QStringLiteral("Example Song (2021 Remastered)"),
                                  {QStringLiteral("Singer")}, QString(), 0};

        const auto score = scoreCandidate(query, candidate,
                                          MatchPolicy::PreserveVersions);

        QCOMPARE(score.versionTier, VersionTier::Conflict);
        QVERIFY(!isAcceptableMatch({candidate, score}));
    }

    void differentNamedEditionsAreConflictingVersions()
    {
        const TrackQuery query{QStringLiteral("Example Song (Deluxe Edition)"),
                               {QStringLiteral("Singer")}, QString(), 0};
        const Candidate candidate{QStringLiteral("special"),
                                  QStringLiteral("Example Song (Special Edition)"),
                                  {QStringLiteral("Singer")}, QString(), 0};

        const auto score = scoreCandidate(query, candidate,
                                          MatchPolicy::PreserveVersions);

        QCOMPARE(score.versionTier, VersionTier::Conflict);
        QVERIFY(!chooseMatch({{candidate, score}}, false).has_value());
    }

    void differentAnniversaryOrdinalsAreConflictingVersions()
    {
        const TrackQuery query{QStringLiteral("Example Song (20th Anniversary Edition)"),
                               {QStringLiteral("Singer")}, QString(), 0};
        const Candidate candidate{QStringLiteral("25th"),
                                  QStringLiteral("Example Song (25th Anniversary Edition)"),
                                  {QStringLiteral("Singer")}, QString(), 0};

        const auto score = scoreCandidate(query, candidate,
                                          MatchPolicy::PreserveVersions);

        QCOMPARE(score.versionTier, VersionTier::Conflict);
        QVERIFY(!chooseMatch({{candidate, score}}, true).has_value());
    }

    void multilingualTitlesForTheSameAnniversaryOrdinalRemainCompatible()
    {
        const TrackQuery query{QStringLiteral("Example Song (20th Anniversary Edition)"),
                               {QStringLiteral("Singer")}, QString(), 0};
        const Candidate candidate{QStringLiteral("20th-localized"),
                                  QStringLiteral("Example Song (20周年記念版)"),
                                  {QStringLiteral("Singer")}, QString(), 0};

        const auto score = scoreCandidate(query, candidate,
                                          MatchPolicy::PreserveVersions);

        QCOMPARE(score.versionTier, VersionTier::Normal);
    }

    void multilingualTitlesForTheSameVersionRemainCompatible()
    {
        const TrackQuery query{QStringLiteral("Example Song (2020 Remaster)"),
                               {QStringLiteral("Singer")}, QString(), 0};
        const Candidate candidate{QStringLiteral("localized"),
                                  QStringLiteral("別名 (2020年重制)"),
                                  {QStringLiteral("Singer")}, QString(), 0,
                                  {QStringLiteral("Example Song (2020年リマスター)")}};

        const auto score = scoreCandidate(query, candidate,
                                          MatchPolicy::PreserveVersions);

        QCOMPARE(score.versionTier, VersionTier::Normal);
        QVERIFY(score.titleViaAlternate);
        QVERIFY(isAcceptableMatch({candidate, score}));
    }

    void explainIncludesThresholdRejectionReason()
    {
        const TrackQuery query{QStringLiteral("Expected Song"),
                               {QStringLiteral("Expected Artist")}, {}, 0};
        const Candidate candidate{QStringLiteral("other"),
                                  QStringLiteral("Unrelated"),
                                  {QStringLiteral("Someone Else")}, {}, 0};

        const QString explanation = explainMatch(query, {candidate}, false, true);

        QVERIFY(explanation.contains(QStringLiteral("versionTier=normal")));
        QVERIFY(explanation.contains(QStringLiteral("rejected=title-threshold")));
        QVERIFY(explanation.contains(QStringLiteral("selected: none")));
    }

    void explainListsEveryQueryTitleVariantActuallyScored()
    {
        // Regression: this diagnostic must show what scoreCandidate really
        // tried, not just cleanTitle's output -- a query with a strippable
        // localized gloss is scored against *two* query-side title
        // variants (see queryTitleVariants), and both must be visible here.
        const TrackQuery query{QStringLiteral("青さは止んだ (青春已逝)"),
                               {QStringLiteral("ナナツカゼ")}, QString(), 0};
        const Candidate candidate{QStringLiteral("3363002263"), QStringLiteral("青さは止んだ"),
                                  {QStringLiteral("ナナツカゼ")}, QString(), 0};

        const QString explanation = explainMatch(query, {candidate}, false, true);

        QVERIFY(explanation.contains(QStringLiteral("title variants: 青さは止んだ (青春已逝) | 青さは止んだ")));
        QVERIFY(!explanation.contains(QStringLiteral("clean title:")));
    }

    void preserveVersionsUsesAliasesWithoutStrippingTheirSuffixes()
    {
        const TrackQuery query{QStringLiteral("Example Song (Acoustic)"),
                               {QStringLiteral("Singer")}, QString(), 0};
        const Candidate candidate{QStringLiteral("alias"),
                                  QStringLiteral("别名"),
                                  {QStringLiteral("Singer")}, QString(), 0,
                                  {QStringLiteral("Example Song (Acoustic)")}};

        const auto score = scoreCandidate(query, candidate,
                                          MatchPolicy::PreserveVersions);

        QCOMPARE(score.versionTier, VersionTier::Normal);
        QCOMPARE(score.title, 1.0);
        QVERIFY(score.titleViaAlternate);
    }

    // candidateRejectionReason mirrors explainMatch's per-candidate listing
    // classification (used by Resolver's "search ... rejected=" log field);
    // these fixtures drive it directly through ScoreBreakdown rather than a
    // full search so each threshold is isolated.
    void rejectionReasonReportsVersionConflictFromScore()
    {
        RankedCandidate candidate;
        candidate.score.rejectionReason = QStringLiteral("version-conflict");
        candidate.score.title = 0.9;
        candidate.score.total = 0.9;
        QCOMPARE(candidateRejectionReason(candidate), QStringLiteral("version-conflict"));
    }

    void rejectionReasonReportsTitleThreshold()
    {
        RankedCandidate candidate;
        candidate.score.title = 0.3;
        candidate.score.total = 0.9;
        QCOMPARE(candidateRejectionReason(candidate), QStringLiteral("title-threshold"));
    }

    void rejectionReasonReportsTotalThreshold()
    {
        RankedCandidate candidate;
        candidate.score.title = 0.6;
        candidate.score.total = 0.3;
        QCOMPARE(candidateRejectionReason(candidate), QStringLiteral("total-threshold"));
    }

    void rejectionReasonReportsAliasArtistThreshold()
    {
        RankedCandidate candidate;
        candidate.score.title = 0.6;
        candidate.score.total = 0.6;
        candidate.score.titleViaAlternate = true;
        candidate.score.artists = 0.1;
        QCOMPARE(candidateRejectionReason(candidate), QStringLiteral("alias-artist-threshold"));
    }

    void rejectionReasonIsEmptyForAnAcceptableCandidate()
    {
        RankedCandidate candidate;
        candidate.score.title = 0.9;
        candidate.score.total = 0.9;
        candidate.score.artists = 1.0;
        QVERIFY(candidateRejectionReason(candidate).isEmpty());
    }
};

QTEST_GUILESS_MAIN(MatcherTest)
#include "tst_matcher.moc"
