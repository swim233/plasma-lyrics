#include "frontend/qmlmodule/visibilitypolicy.h"

#include <QTest>
#include <vector>

// DESIGN.md decision 40. Every scenario below sets `delayMs` large enough
// (or, for the cold-start rows, doesn't matter at all) that if a test
// wrongly took the buffered path instead of the bypass it means to exercise,
// the assertion would fail rather than pass by accident because the buffer
// happened to be short.
class VisibilityPolicyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The six-input truth table, including the "undetermined" row and the
    // service-unavailable exception. Every row goes through
    // evaluateColdStart() (determined is set last, after the other five),
    // which is deliberately how a freshly-constructed policy always starts
    // out -- so this doubles as coverage that the bypass applies uniformly,
    // not just to a hand-picked idle case.
    void truthTable()
    {
        struct Row
        {
            const char *name;
            bool serviceAvailable;
            bool stale;
            QString playbackStatus;
            QString trackTitle;
            QString lyricState;
            bool setDetermined; // false = leave at the default (never called)
            bool enabled;
            bool hideNonMusic;
            bool expectedVisible;
        };
        const QString stopped = QStringLiteral("Stopped");
        const QString playing = QStringLiteral("Playing");
        const QString paused = QStringLiteral("Paused");
        const QString ok = QStringLiteral("ok");
        const QString filtered = QStringLiteral("filtered");
        const QString empty;
        const QString song = QStringLiteral("song");
        const QString videoTitle = QStringLiteral("video title");

        const std::vector<Row> rows = {
            {"undetermined stays hidden even though everything else says idle",
             true, false, stopped, empty, ok, false, true, true, false},
            {"undetermined stays hidden even though everything else says playing",
             true, false, playing, song, ok, false, true, true, false},
            {"service unavailable never hides, despite idle + enabled + hideNonMusic",
             false, false, stopped, empty, filtered, true, true, true, true},
            {"stale never hides, despite idle + enabled + hideNonMusic",
             true, true, stopped, empty, filtered, true, true, true, true},
            {"idle + enabled hides",
             true, false, stopped, empty, ok, true, true, true, false},
            {"Stopped alone, with a non-empty title, is also idle",
             true, false, stopped, song, ok, true, true, true, false},
            {"paused with a title is not idle -- stays visible",
             true, false, paused, song, ok, true, true, true, true},
            {"empty title while nominally playing still counts as idle",
             true, false, playing, empty, ok, true, true, true, false},
            {"filtered with hideNonMusic off does not hide",
             true, false, playing, videoTitle, filtered, true, true, false, true},
            {"filtered with hideNonMusic on hides",
             true, false, playing, videoTitle, filtered, true, true, true, false},
            {"master switch off never hides, even while idle",
             true, false, stopped, empty, ok, true, false, true, true},
        };

        for (const auto &row : rows) {
            VisibilityPolicy policy;
            policy.setDelayMs(60000); // must not matter: cold start bypasses it
            policy.setEnabled(row.enabled);
            policy.setHideNonMusic(row.hideNonMusic);
            policy.setServiceAvailable(row.serviceAvailable);
            policy.setStale(row.stale);
            policy.setPlaybackStatus(row.playbackStatus);
            policy.setTrackTitle(row.trackTitle);
            policy.setLyricState(row.lyricState);
            if (row.setDetermined) {
                policy.setDetermined(true);
            }
            QVERIFY2(policy.shouldBeVisible() == row.expectedVisible, row.name);
        }
    }

    // DESIGN.md decision 40 ships auto-hide default OFF specifically so
    // nothing changes for anyone who has never touched it. QML applies every
    // declarative property binding by calling the matching setter during
    // construction, and a setter that receives the same value the member
    // already defaults to returns early without calling evaluate() at all
    // (see setEnabled() et al.) -- so on a fresh install, where every one of
    // main.qml's bound properties happens to equal its own C++ default here,
    // evaluate() never runs before anything first reads shouldBeVisible.
    // That first read (main.qml's Component.onCompleted, ahead of
    // LyricSource's deferred first reload()) has to see "visible", matching
    // pre-feature behaviour -- not "hidden" from the undetermined rule below,
    // which exists to protect a feature that here is not even switched on.
    void staysVisibleBeforeAnythingIsEverSet()
    {
        VisibilityPolicy policy;
        QVERIFY(policy.shouldBeVisible());
    }

    void landsHiddenImmediatelyOnColdStart()
    {
        VisibilityPolicy policy;
        policy.setDelayMs(5000);
        policy.setEnabled(true);
        policy.setHideNonMusic(true);
        policy.setServiceAvailable(true);
        policy.setStale(false);
        policy.setPlaybackStatus(QStringLiteral("Stopped"));
        policy.setTrackTitle(QString());
        policy.setLyricState(QStringLiteral("ok"));
        QVERIFY(!policy.shouldBeVisible()); // still undetermined

        policy.setDetermined(true);
        // No QTest::qWait anywhere in this test: the assertion below has to
        // hold synchronously, proving the 5s delay was never applied.
        QVERIFY(!policy.shouldBeVisible());
    }

    void cancelsHideWithinTheBuffer()
    {
        VisibilityPolicy policy;
        policy.setDelayMs(80);
        policy.setEnabled(true);
        policy.setHideNonMusic(true);
        policy.setServiceAvailable(true);
        policy.setStale(false);
        policy.setLyricState(QStringLiteral("ok"));
        policy.setPlaybackStatus(QStringLiteral("Playing"));
        policy.setTrackTitle(QStringLiteral("song"));
        policy.setDetermined(true);
        QVERIFY(policy.shouldBeVisible());

        policy.setPlaybackStatus(QStringLiteral("Stopped")); // starts buffering
        QVERIFY(policy.shouldBeVisible());
        QTest::qWait(30);
        // A new track arrives inside the 80ms buffer: cancel, don't animate.
        policy.setTrackTitle(QStringLiteral("song2"));
        policy.setPlaybackStatus(QStringLiteral("Playing"));
        QVERIFY(policy.shouldBeVisible());

        // Wait past where the original timer would have fired. If the timer
        // had merely been outrun rather than actually stopped, this would
        // catch a stale, spurious hide.
        QTest::qWait(120);
        QVERIFY(policy.shouldBeVisible());
    }

    // Re-arming the hide timer has to react to delayMs changing mid-buffer,
    // but only by exactly the right amount: neither ignoring the change (the
    // deadline stays at the stale original mark) nor recomputing it as a
    // fresh full delayMs counted from *now* (which would double-count the
    // time already elapsed). Confirmed by trying both of those broken
    // shapes against this test before landing the fix: the stale variant
    // (drop the `m_armedDeadlineMs != deadlineMs` check and never re-arm)
    // still shows visible at the check below, and so does a "restart with
    // m_delayMs" variant that forgets to subtract elapsed time.
    void adjustsTheHideDeadlineWhenDelayChangesMidBuffer()
    {
        VisibilityPolicy policy;
        policy.setDelayMs(400);
        policy.setEnabled(true);
        policy.setHideNonMusic(true);
        policy.setServiceAvailable(true);
        policy.setStale(false);
        policy.setLyricState(QStringLiteral("ok"));
        policy.setPlaybackStatus(QStringLiteral("Playing"));
        policy.setTrackTitle(QStringLiteral("song"));
        policy.setDetermined(true);
        QVERIFY(policy.shouldBeVisible());

        policy.setPlaybackStatus(QStringLiteral("Stopped")); // idle begins, t=0
        QTest::qWait(50);
        QVERIFY(policy.shouldBeVisible());

        // Shortened to 100ms while ~50ms of it has already elapsed: the new
        // deadline is ~100ms after idle *began* (t=100), i.e. ~50ms from
        // this call -- not the untouched stale mark (t=400), and not a
        // fresh 100ms counted from this instant either (that would be
        // t=150).
        policy.setDelayMs(100);

        QTest::qWait(75); // ~125ms since idle began: past t=100, short of
                          // both t=150 and t=400
        QVERIFY(!policy.shouldBeVisible());
    }

    // Companion to the test above: a property that does not change *why* we
    // are hide-worthy (idle here, so hideNonMusic is irrelevant) still
    // re-evaluates and must leave the deadline exactly where it was --
    // neither stale nor pushed out. A variant that restarts the timer with
    // a fresh m_delayMs on every evaluate() call, forgetting to account for
    // time already elapsed, extends the deadline here and fails the check
    // below; confirmed by trying that variant before landing the fix.
    void irrelevantPropertyChangeMidBufferDoesNotMoveTheDeadline()
    {
        VisibilityPolicy policy;
        policy.setDelayMs(150);
        policy.setEnabled(true);
        policy.setHideNonMusic(true);
        policy.setServiceAvailable(true);
        policy.setStale(false);
        policy.setLyricState(QStringLiteral("ok"));
        policy.setPlaybackStatus(QStringLiteral("Playing"));
        policy.setTrackTitle(QStringLiteral("song"));
        policy.setDetermined(true);

        policy.setPlaybackStatus(QStringLiteral("Stopped")); // idle begins, t=0
        QTest::qWait(60);
        QVERIFY(policy.shouldBeVisible());

        // Irrelevant to why we're hiding (the reason is "idle", not
        // "filtered"), but still triggers an evaluate() call.
        policy.setHideNonMusic(false);

        QTest::qWait(70); // ~130ms since idle began: short of the 150ms deadline
        QVERIFY(policy.shouldBeVisible());

        QTest::qWait(40); // ~170ms since idle began: past the original deadline
        QVERIFY(!policy.shouldBeVisible());
    }

    void reversesImmediatelyOnceAlreadyHidden()
    {
        VisibilityPolicy policy;
        policy.setDelayMs(20);
        policy.setEnabled(true);
        policy.setHideNonMusic(true);
        policy.setServiceAvailable(true);
        policy.setStale(false);
        policy.setLyricState(QStringLiteral("ok"));
        policy.setPlaybackStatus(QStringLiteral("Playing"));
        policy.setTrackTitle(QStringLiteral("song"));
        policy.setDetermined(true);

        policy.setPlaybackStatus(QStringLiteral("Stopped"));
        QTRY_VERIFY(!policy.shouldBeVisible()); // buffer elapses for real

        policy.setPlaybackStatus(QStringLiteral("Playing"));
        policy.setTrackTitle(QStringLiteral("song2"));
        // Showing again is never buffered -- this has to be true right away,
        // no QTRY_VERIFY needed.
        QVERIFY(policy.shouldBeVisible());
    }

    // QA regression: idle beginning *after* cold start, while auto-hide is
    // still off, must be tracked exactly like idle-already-true-at-cold-start
    // is. Cold start here is *playing* (not hide-worthy, so
    // evaluateColdStart() records nothing), then playback stops while the
    // switch is still off, then a long wait, then the switch flips on --
    // must hide immediately, not restart a fresh buffer from the moment of
    // enabling. This is the gap the previous fix for the "toggled on after a
    // long idle" scenario below left open: gating m_hideWorthySinceMs's own
    // bookkeeping behind m_enabled (rather than only the output) meant a
    // hide-worthy transition that happens *after* cold start while disabled
    // was never timestamped at all.
    void tracksIdleStartEvenWhileDisabledSoEnablingLaterHidesImmediately()
    {
        qint64 now = 0;
        VisibilityPolicy policy([&now] { return now; });
        policy.setDelayMs(5000);
        policy.setHideNonMusic(true);
        policy.setServiceAvailable(true);
        policy.setStale(false);
        policy.setLyricState(QStringLiteral("ok"));
        policy.setPlaybackStatus(QStringLiteral("Playing"));
        policy.setTrackTitle(QStringLiteral("song"));
        policy.setEnabled(false); // the default, spelled out for clarity
        policy.setDetermined(true); // cold start: playing, not hide-worthy yet
        QVERIFY(policy.shouldBeVisible());

        policy.setPlaybackStatus(QStringLiteral("Stopped")); // idle begins, still disabled
        QVERIFY(policy.shouldBeVisible());

        now += 20000; // idle 20s while still disabled -- far past the 5s delay
        policy.setEnabled(true);
        // Hides right away: idle's start time was tracked the whole time,
        // even though nothing was watching it while the switch was off.
        QVERIFY(!policy.shouldBeVisible());
    }

    // QA2: rawHideWorthy's `filtered()` and `!m_stale` conjuncts
    // (visibilitypolicy.cpp's evaluate(), not evaluateColdStart()) had zero
    // coverage on the evaluate() path -- every filtered/stale row in
    // truthTable() calls setDetermined(true) *last*, so they only ever
    // exercise evaluateColdStart()'s copy of the same logic. This test
    // reaches "filtered" via a setter call *after* setDetermined(true) has
    // already run, landing on evaluate() instead: cold start is
    // Playing+titled (not hide-worthy), then lyricState flips to "filtered"
    // afterwards. Dropping `|| filtered()` from evaluate()'s rawHideWorthy
    // means a video starting mid-session, after the widget has already
    // settled, would silently never be tracked as hide-worthy again for the
    // rest of that session.
    void filteredBecomingTruePostDeterminationHidesAfterTheBuffer()
    {
        VisibilityPolicy policy;
        policy.setDelayMs(20);
        policy.setEnabled(true);
        policy.setHideNonMusic(true);
        policy.setServiceAvailable(true);
        policy.setStale(false);
        policy.setLyricState(QStringLiteral("ok"));
        policy.setPlaybackStatus(QStringLiteral("Playing"));
        policy.setTrackTitle(QStringLiteral("song"));
        policy.setDetermined(true); // cold start: playing + titled, not hide-worthy
        QVERIFY(policy.shouldBeVisible());

        policy.setLyricState(QStringLiteral("filtered")); // post-determination, via evaluate()
        QVERIFY(policy.shouldBeVisible()); // still buffering
        QTRY_VERIFY(!policy.shouldBeVisible()); // buffer elapses for real
    }

    // QA2's other half: `!m_stale` on the evaluate() path. Cold start is
    // again not hide-worthy (playing + titled), then both stale and idle
    // arrive together via setters. Decision 40 pins "!serviceAvailable ||
    // stale 时绝不隐藏，且不可配置" -- the one surface (LyricsView.qml's
    // Loader) telling the user the daemon crashed must never be the thing
    // auto-hide fades out from under them. Dropping `!m_stale` from
    // evaluate()'s rawHideWorthy would let a daemon crash mid-session (which
    // typically also makes the last-known playback state look idle) hide the
    // widget instead of leaving that diagnostic surface up.
    void staleBecomingTruePostDeterminationStaysVisibleWithNoTimerArmed()
    {
        VisibilityPolicy policy;
        policy.setDelayMs(20);
        policy.setEnabled(true);
        policy.setHideNonMusic(true);
        policy.setServiceAvailable(true);
        policy.setStale(false);
        policy.setLyricState(QStringLiteral("ok"));
        policy.setPlaybackStatus(QStringLiteral("Playing"));
        policy.setTrackTitle(QStringLiteral("song"));
        policy.setDetermined(true); // cold start: playing + titled, not hide-worthy
        QVERIFY(policy.shouldBeVisible());

        policy.setStale(true); // daemon looks stale...
        policy.setPlaybackStatus(QStringLiteral("Stopped")); // ...and now idle too
        QVERIFY(policy.shouldBeVisible());

        // If a timer had been (wrongly) armed despite staleness, it would
        // have fired well within this wait -- staying visible here proves
        // none was armed, not just that this check outran it.
        QTest::qWait(60);
        QVERIFY(policy.shouldBeVisible());
    }

    // DESIGN.md decision 40: "缓冲计的是判据成立至今多久，不是配置变更至今
    // 多久". Not one of the four tests the lead's checklist names explicitly,
    // but a direct spec point (decision 40, consensus doc §2 #38) that the
    // simpler "arm a timer on the hide-worthy transition" design would get
    // wrong, so it earns its own case.
    void hidesImmediatelyWhenEnabledIsToggledOnAfterALongIdle()
    {
        qint64 now = 0;
        VisibilityPolicy policy([&now] { return now; });
        policy.setDelayMs(5000);
        policy.setHideNonMusic(true);
        policy.setServiceAvailable(true);
        policy.setStale(false);
        policy.setPlaybackStatus(QStringLiteral("Stopped"));
        policy.setTrackTitle(QString());
        policy.setLyricState(QStringLiteral("ok"));
        policy.setEnabled(false); // auto-hide off while already idle
        policy.setDetermined(true);
        QVERIFY(policy.shouldBeVisible());

        now += 10000; // idle for 10s -- far past the 5s delay -- switch still off
        policy.setEnabled(true); // user ticks the master switch on
        // Hides right away: the buffer had already run out while the switch
        // was off, so there is nothing left to wait for.
        QVERIFY(!policy.shouldBeVisible());
    }
};

QTEST_GUILESS_MAIN(VisibilityPolicyTest)
#include "tst_visibilitypolicy.moc"
