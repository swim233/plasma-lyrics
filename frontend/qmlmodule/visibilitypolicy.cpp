#include "visibilitypolicy.h"

#include <time.h>

VisibilityPolicy::VisibilityPolicy(QObject *parent)
    : VisibilityPolicy(&VisibilityPolicy::monotonicNowMs, parent)
{
}

VisibilityPolicy::VisibilityPolicy(std::function<qint64()> clock, QObject *parent)
    : QObject(parent)
    , m_clock(std::move(clock))
{
    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, [this] { setShouldBeVisible(false); });
}

qint64 VisibilityPolicy::monotonicNowMs()
{
    timespec value{};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return static_cast<qint64>(value.tv_sec) * 1000 + value.tv_nsec / 1000000;
}

bool VisibilityPolicy::serviceAvailable() const { return m_serviceAvailable; }
bool VisibilityPolicy::stale() const { return m_stale; }
QString VisibilityPolicy::playbackStatus() const { return m_playbackStatus; }
QString VisibilityPolicy::trackTitle() const { return m_trackTitle; }
QString VisibilityPolicy::lyricState() const { return m_lyricState; }
bool VisibilityPolicy::determined() const { return m_determined; }
bool VisibilityPolicy::enabled() const { return m_enabled; }
bool VisibilityPolicy::hideNonMusic() const { return m_hideNonMusic; }
int VisibilityPolicy::delayMs() const { return m_delayMs; }
bool VisibilityPolicy::shouldBeVisible() const { return m_shouldBeVisible; }

bool VisibilityPolicy::idle() const
{
    // Same underlying condition LyricsView.qml's effectiveText falls back to
    // idleText on: a title can go empty before playbackStatus catches up to
    // Stopped, and the two must not disagree about whether there is "a
    // song" on screen. Not a claim that the two are exact mirrors, though --
    // LyricsView tests lyricState for searching/not-found/no-lyric *before*
    // this condition, so a state crossing one of those with Stopped could in
    // principle still show text while this already calls it hide-worthy.
    return m_playbackStatus == QStringLiteral("Stopped") || m_trackTitle.isEmpty();
}

bool VisibilityPolicy::filtered() const
{
    return m_lyricState == QStringLiteral("filtered");
}

void VisibilityPolicy::setServiceAvailable(bool value)
{
    if (m_serviceAvailable == value) return;
    m_serviceAvailable = value;
    Q_EMIT serviceAvailableChanged();
    evaluate();
}

void VisibilityPolicy::setStale(bool value)
{
    if (m_stale == value) return;
    m_stale = value;
    Q_EMIT staleChanged();
    evaluate();
}

void VisibilityPolicy::setPlaybackStatus(const QString &value)
{
    if (m_playbackStatus == value) return;
    m_playbackStatus = value;
    Q_EMIT playbackStatusChanged();
    evaluate();
}

void VisibilityPolicy::setTrackTitle(const QString &value)
{
    if (m_trackTitle == value) return;
    m_trackTitle = value;
    Q_EMIT trackTitleChanged();
    evaluate();
}

void VisibilityPolicy::setLyricState(const QString &value)
{
    if (m_lyricState == value) return;
    m_lyricState = value;
    Q_EMIT lyricStateChanged();
    evaluate();
}

void VisibilityPolicy::setDetermined(bool value)
{
    if (m_determined == value) return;
    const bool becameDetermined = value && !m_determined;
    m_determined = value;
    Q_EMIT determinedChanged();
    // The undetermined -> determined transition happens exactly once, ever
    // (LyricSource::determined never goes back to false), and it is the one
    // case that must bypass the buffer entirely: see evaluateColdStart().
    // Every other property change funnels through the ordinary evaluate().
    if (becameDetermined) {
        evaluateColdStart();
    } else {
        evaluate();
    }
}

void VisibilityPolicy::setEnabled(bool value)
{
    if (m_enabled == value) return;
    m_enabled = value;
    Q_EMIT enabledChanged();
    evaluate();
}

void VisibilityPolicy::setHideNonMusic(bool value)
{
    if (m_hideNonMusic == value) return;
    m_hideNonMusic = value;
    Q_EMIT hideNonMusicChanged();
    evaluate();
}

void VisibilityPolicy::setDelayMs(int value)
{
    if (m_delayMs == value) return;
    m_delayMs = value;
    Q_EMIT delayMsChanged();
    evaluate();
}

void VisibilityPolicy::setShouldBeVisible(bool value)
{
    if (m_shouldBeVisible == value) return;
    m_shouldBeVisible = value;
    Q_EMIT shouldBeVisibleChanged();
}

void VisibilityPolicy::stopHideTimer()
{
    m_hideTimer.stop();
    m_armedDeadlineMs = -1;
}

void VisibilityPolicy::evaluateColdStart()
{
    // The very first determination must land instantly in whichever
    // direction it goes: there is no "elapsed so far" to weigh against the
    // buffer yet, and applying the buffer here would mean every login (or
    // every daemon restart) fades in before it is allowed to fade back out.
    // DESIGN.md decision 40: "冷启动若判据已成立，直接落 opacity: 0，不播
    // 动画". QML gates the fade animation itself on having passed this first
    // determination (see main.qml); here it just means: no timer, ever, for
    // this one call.
    stopHideTimer();
    // Tracking m_hideWorthySinceMs happens regardless of m_enabled below --
    // this is the one moment real data first arrives, and a later toggle of
    // the master switch (see evaluate()'s comment) needs this baseline to
    // already be there even if auto-hide was off when it was established.
    const bool rawHideWorthy = m_serviceAvailable && !m_stale && (idle() || filtered());
    m_hideWorthySinceMs = rawHideWorthy ? m_clock() : -1;

    // Checked before anything else, output-wise: with the feature off there
    // is nothing for the "undetermined behaves as hidden" landing state to
    // protect (see evaluate()'s matching check for the full rationale). This
    // mirrors evaluate()'s precedence exactly, on purpose.
    if (!m_enabled) {
        setShouldBeVisible(true);
        return;
    }
    const bool wantsHidden = rawHideWorthy && (idle() || (filtered() && m_hideNonMusic));
    setShouldBeVisible(!wantsHidden);
}

void VisibilityPolicy::evaluate()
{
    // Bookkeeping first, unconditionally -- independent of m_enabled *and*
    // of what happens below. m_hideWorthySinceMs's own header comment
    // promises this, and evaluateColdStart() already keeps that promise;
    // this used to gate it behind the m_enabled check below too, which
    // broke the promise for exactly one path: idle (or filtered) becoming
    // true *after* cold start while auto-hide is still off. With no
    // timestamp recorded, re-enabling later would treat "now" as the start
    // of a fresh buffer instead of immediately recognising the buffer had
    // long since run out -- decision 40's "缓冲计的是判据成立至今多久"
    // requires the former to be tracked regardless of whether the latter
    // is even switched on. Folding m_determined into rawHideWorthy here
    // means an undetermined instance still resets the timestamp exactly as
    // before: none of the tracked properties carry real information yet.
    const bool rawHideWorthy = m_determined
        && m_serviceAvailable && !m_stale && (idle() || filtered());
    if (rawHideWorthy) {
        if (m_hideWorthySinceMs < 0) {
            m_hideWorthySinceMs = m_clock();
        }
    } else {
        m_hideWorthySinceMs = -1;
    }

    // Auto-hide switched off suppresses nothing -- not even the
    // "undetermined behaves as hidden" state below. That state exists only
    // to stop the *feature* from flashing content it is about to fade out;
    // with the feature off there is nothing to suppress, and the widget must
    // look exactly as it did before this feature existed. This has to be
    // the very first *output* check: decision 40 ships auto-hide default OFF
    // specifically so existing users see zero behaviour change, and most
    // properties bound from QML land on values that equal their C++
    // defaults at construction time (see m_shouldBeVisible's comment in the
    // header) -- so a check placed *after* the undetermined branch would
    // still let a disabled instance flash HiddenStatus for the one
    // event-loop turn before LyricSource's first reload() completes.
    if (!m_enabled) {
        stopHideTimer();
        setShouldBeVisible(true);
        return;
    }

    if (!m_determined) {
        // Same five values as a confirmed-unavailable daemon (see
        // LyricSource::setDetermined's comment) -- undetermined behaves like
        // hidden, full stop, no timer running underneath it.
        stopHideTimer();
        setShouldBeVisible(false);
        return;
    }

    // Whether we are anywhere in "no song" territory, independent of
    // hideNonMusic (m_enabled and m_determined are both already known true
    // above). DESIGN.md decision 40, "缓冲计的是判据成立至今多久，不是配置
    // 变更至今多久" -- ticking the master switch on after being idle for an
    // hour must fade out right away, not restart a fresh delay. See
    // evaluateColdStart() for the mirror-image rule that applies to the very
    // first determination instead.
    if (!rawHideWorthy) {
        stopHideTimer();
        setShouldBeVisible(true);
        return;
    }

    const bool wantsHidden = idle() || (filtered() && m_hideNonMusic);
    if (!wantsHidden) {
        stopHideTimer();
        setShouldBeVisible(true);
        return;
    }

    if (!m_shouldBeVisible) {
        // Already hidden; nothing left to buffer or animate.
        return;
    }

    const qint64 deadlineMs = m_hideWorthySinceMs + m_delayMs;
    const qint64 remainingMs = deadlineMs - m_clock();
    if (remainingMs <= 0) {
        stopHideTimer();
        setShouldBeVisible(false);
        return;
    }
    // Re-arm only when the deadline itself actually moved -- e.g. delayMs
    // changed mid-buffer. Something going hide-worthy-adjacent again while
    // already buffering with nothing else changed (idle -> filtered,
    // decision 40's "filtered 走同一个缓冲键", or an unrelated property like
    // hideNonMusic notifying while the current reason is idle) must not
    // perturb an already-correct countdown: restarting unconditionally on
    // every evaluate() call is the same mistake as never restarting at all,
    // just aimed the other way (see tst_visibilitypolicy.cpp).
    if (!m_hideTimer.isActive() || m_armedDeadlineMs != deadlineMs) {
        m_hideTimer.start(static_cast<int>(remainingMs));
        m_armedDeadlineMs = deadlineMs;
    }
}
