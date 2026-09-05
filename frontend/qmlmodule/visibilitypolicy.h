#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <functional>

// DESIGN.md decision 40: six discrete inputs in, one shouldBeVisible bool
// out. Kept free of any compile-time dependency on LyricSource -- unlike
// LyricsView, which reads a whole snapshot-backed object, this class is
// tested by driving setters directly, without assembling a snapshot file.
class VisibilityPolicy : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool serviceAvailable READ serviceAvailable WRITE setServiceAvailable NOTIFY serviceAvailableChanged)
    Q_PROPERTY(bool stale READ stale WRITE setStale NOTIFY staleChanged)
    Q_PROPERTY(QString playbackStatus READ playbackStatus WRITE setPlaybackStatus NOTIFY playbackStatusChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle WRITE setTrackTitle NOTIFY trackTitleChanged)
    Q_PROPERTY(QString lyricState READ lyricState WRITE setLyricState NOTIFY lyricStateChanged)
    Q_PROPERTY(bool determined READ determined WRITE setDetermined NOTIFY determinedChanged)

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool hideNonMusic READ hideNonMusic WRITE setHideNonMusic NOTIFY hideNonMusicChanged)
    Q_PROPERTY(int delayMs READ delayMs WRITE setDelayMs NOTIFY delayMsChanged)

    Q_PROPERTY(bool shouldBeVisible READ shouldBeVisible NOTIFY shouldBeVisibleChanged)

public:
    explicit VisibilityPolicy(QObject *parent = nullptr);
    explicit VisibilityPolicy(std::function<qint64()> clock, QObject *parent = nullptr);

    bool serviceAvailable() const;
    bool stale() const;
    QString playbackStatus() const;
    QString trackTitle() const;
    QString lyricState() const;
    bool determined() const;
    bool enabled() const;
    bool hideNonMusic() const;
    int delayMs() const;
    bool shouldBeVisible() const;

    void setServiceAvailable(bool value);
    void setStale(bool value);
    void setPlaybackStatus(const QString &value);
    void setTrackTitle(const QString &value);
    void setLyricState(const QString &value);
    void setDetermined(bool value);
    void setEnabled(bool value);
    void setHideNonMusic(bool value);
    void setDelayMs(int value);

Q_SIGNALS:
    void serviceAvailableChanged();
    void staleChanged();
    void playbackStatusChanged();
    void trackTitleChanged();
    void lyricStateChanged();
    void determinedChanged();
    void enabledChanged();
    void hideNonMusicChanged();
    void delayMsChanged();
    void shouldBeVisibleChanged();

private:
    static qint64 monotonicNowMs();
    bool idle() const;
    bool filtered() const;
    void setShouldBeVisible(bool value);
    void stopHideTimer();
    void evaluate();
    void evaluateColdStart();

    std::function<qint64()> m_clock;
    QTimer m_hideTimer;

    bool m_serviceAvailable = false;
    bool m_stale = false;
    QString m_playbackStatus = QStringLiteral("Stopped");
    QString m_trackTitle;
    QString m_lyricState = QStringLiteral("filtered");
    bool m_determined = false;

    bool m_enabled = false;
    bool m_hideNonMusic = true;
    int m_delayMs = 5000;

    // Visible by default. This is not just "a sensible starting guess" --
    // QML applies every declarative property binding (including `enabled:`)
    // during construction, and a setter that receives the same value the
    // member already holds returns early without calling evaluate() (see
    // e.g. setEnabled()). Auto-hide ships default OFF (decision 40), so on
    // a fresh install every one of the six inputs' and three config
    // properties' bound initial value coincides with its C++ default here,
    // and NOTHING calls evaluate() before main.qml's Component.onCompleted
    // reads shouldBeVisible for the first time. That first read has to look
    // like auto-hide was never touched -- true -- not like the "undetermined
    // behaves as hidden" rule from decision 40, which exists only to stop
    // the *feature* from flashing content it's about to fade out and has
    // nothing to say when the feature is off. Whenever a bound property
    // *does* differ from its default, the corresponding setter runs
    // evaluate()/evaluateColdStart() for real and overwrites this with the
    // correct answer before anything ever reads it.
    bool m_shouldBeVisible = true;
    // Monotonic timestamp of when serviceAvailable && !stale && (idle ||
    // filtered) last became true, independent of `enabled`/`hideNonMusic` --
    // see evaluate()'s comment for why the buffer has to be measured against
    // this rather than against whichever config flip most recently made
    // hiding actually apply.
    qint64 m_hideWorthySinceMs = -1;
    // The absolute deadline (monotonic ms) the running m_hideTimer is
    // currently armed to fire at, or -1 if it isn't armed. Lets evaluate()
    // tell "still counting down to the same moment" apart from "the
    // deadline moved" -- e.g. delayMs changed mid-buffer -- so it restarts
    // the timer exactly when it needs to and never otherwise.
    qint64 m_armedDeadlineMs = -1;
};
