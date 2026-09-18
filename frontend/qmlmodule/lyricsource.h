#pragma once

#include "core/lyric/lyricmodel.h"

#include <QFileSystemWatcher>
#include <QVariantList>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <functional>

class LyricSource : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString snapshotPath READ snapshotPath WRITE setSnapshotPath NOTIFY snapshotPathChanged)
    Q_PROPERTY(bool serviceAvailable READ serviceAvailable NOTIFY statusChanged)
    Q_PROPERTY(bool stale READ stale NOTIFY statusChanged)
    Q_PROPERTY(bool determined READ determined NOTIFY determinedChanged)
    Q_PROPERTY(QString lyricState READ lyricState NOTIFY lyricStateChanged)
    Q_PROPERTY(QString playbackStatus READ playbackStatus NOTIFY playbackChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY trackChanged)
    Q_PROPERTY(QString trackArtists READ trackArtists NOTIFY trackChanged)
    Q_PROPERTY(QString currentText READ currentText NOTIFY currentLineChanged)
    Q_PROPERTY(QString currentTranslation READ currentTranslation NOTIFY currentLineChanged)
    Q_PROPERTY(QString currentRomanization READ currentRomanization NOTIFY currentLineChanged)
    Q_PROPERTY(QVariantList currentWords READ currentWords NOTIFY currentLineChanged)
    // Synthetic per-character words for the current line, populated only
    // when the *whole document* -- every line, not just this one -- carries
    // no real word timings (DESIGN.md's synthetic word-by-word decision:
    // 204/204 documents with any real words had them on every line, 0 mixed
    // documents observed). Empty whenever currentWords() is not. This is a
    // second, independent read-only property rather than a fallback baked
    // into currentWords() itself: main.qml shares one LyricSource instance
    // between the desktop and panel representations (see main.qml's
    // `LyricSource { id: lyricSource }`), so the synthetic-or-not choice
    // cannot live as a writable property on this object -- it has to be
    // made per-representation, in LyricsView.
    Q_PROPERTY(QVariantList currentSyntheticWords READ currentSyntheticWords NOTIFY currentLineChanged)
    Q_PROPERTY(qint64 currentPositionMs READ currentPositionMs NOTIFY currentPositionChanged)
    Q_PROPERTY(int offsetMs READ offsetMs NOTIFY offsetChanged)
    Q_PROPERTY(bool canAdjustOffset READ canAdjustOffset NOTIFY canAdjustOffsetChanged)
    Q_PROPERTY(bool globalOffsetEnabled READ globalOffsetEnabled NOTIFY globalOffsetEnabledChanged)
    Q_PROPERTY(QString fingerprint READ fingerprint NOTIFY trackChanged)
    Q_PROPERTY(QString preferredProvider READ preferredProvider NOTIFY providerStateChanged)
    Q_PROPERTY(QString effectivePreferredProvider READ effectivePreferredProvider NOTIFY providerStateChanged)
    Q_PROPERTY(QString actualProvider READ actualProvider NOTIFY providerStateChanged)
    Q_PROPERTY(bool temporaryFallback READ temporaryFallback NOTIFY providerStateChanged)
    Q_PROPERTY(QStringList availableProviders READ availableProviders NOTIFY providerStateChanged)
    Q_PROPERTY(QString switchingProvider READ switchingProvider NOTIFY providerStateChanged)
    Q_PROPERTY(bool canControlProvider READ canControlProvider NOTIFY canControlProviderChanged)
    Q_PROPERTY(bool controlInProgress READ controlInProgress NOTIFY controlInProgressChanged)
    Q_PROPERTY(QString controlError READ controlError NOTIFY controlErrorChanged)

public:
    explicit LyricSource(QObject *parent = nullptr);
    explicit LyricSource(std::function<qint64()> clock, QObject *parent = nullptr);

    QString snapshotPath() const;
    void setSnapshotPath(const QString &path);
    bool serviceAvailable() const;
    bool stale() const;
    bool determined() const;
    QString lyricState() const;
    QString playbackStatus() const;
    QString trackTitle() const;
    QString trackArtists() const;
    QString currentText() const;
    QString currentTranslation() const;
    QString currentRomanization() const;
    QVariantList currentWords() const;
    QVariantList currentSyntheticWords() const;
    qint64 currentPositionMs() const;
    int offsetMs() const;
    bool canAdjustOffset() const;
    bool globalOffsetEnabled() const;
    QString fingerprint() const;
    QString preferredProvider() const;
    QString effectivePreferredProvider() const;
    QString actualProvider() const;
    bool temporaryFallback() const;
    QStringList availableProviders() const;
    QString switchingProvider() const;
    bool canControlProvider() const;
    bool controlInProgress() const;
    QString controlError() const;

    /// The position the lyric timeline is at right now, offset already applied,
    /// so it can be compared directly against a line's or a word's own times.
    /// Pulled per frame by the word-by-word renderer rather than pushed: that
    /// side is driven by a QML FrameAnimation, which stops on its own while the
    /// window is not rendering (DESIGN.md decision 38).
    Q_INVOKABLE qint64 lyricPositionMs() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE bool adjustOffset(int deltaMs);
    Q_INVOKABLE bool resetOffset();
    Q_INVOKABLE bool setPreferredProvider(const QString &provider);
    Q_INVOKABLE bool clearPreferredProvider();
    Q_INVOKABLE bool research();
    Q_INVOKABLE QString providerDisplayName(const QString &provider) const;

Q_SIGNALS:
    void snapshotPathChanged();
    void statusChanged();
    void determinedChanged();
    void lyricStateChanged();
    void playbackChanged();
    void trackChanged();
    void currentLineChanged();
    void currentPositionChanged();
    void offsetChanged();
    void canAdjustOffsetChanged();
    void globalOffsetEnabledChanged();
    void providerStateChanged();
    void canControlProviderChanged();
    void controlInProgressChanged();
    void controlErrorChanged();
    void controlFailed(const QString &error);

private:
    static qint64 monotonicNowNs();
    void rearm();
    void rearmFrame();
    void setUnavailable(bool stale);
    void setDetermined(bool value);
    void updateServiceHealth();
    void reloadImpl();
    qint64 livePositionMs() const;
    void advance();
    void updateCurrentLine(bool lineContentChanged);
    bool hasTrackRef() const;
    bool sendControlCommand(const QString &method, const QVariantList &arguments);

    QFileSystemWatcher m_watcher;
    QTimer m_retryTimer;
    QTimer m_frameTimer;
    QTimer m_healthTimer;
    std::function<qint64()> m_clock;
    QString m_snapshotPath;
    bool m_serviceAvailable = false;
    bool m_stale = false;
    bool m_determined = false;
    qint64 m_pid = 0;
    qint64 m_sequence = -1;
    QString m_lyricState = QStringLiteral("filtered");
    QString m_playbackStatus = QStringLiteral("Stopped");
    QString m_trackTitle;
    QString m_trackArtists;
    QString m_fingerprint;
    QString m_provider;
    QString m_trackId;
    QString m_preferredProvider;
    QString m_effectivePreferredProvider;
    QString m_actualProvider;
    QStringList m_availableProviders;
    bool m_temporaryFallback = false;
    QString m_switchingProvider;
    bool m_controlInProgress = false;
    QString m_controlError;
    PlasmaLyrics::LyricLines m_lines;
    // Cached alongside m_lines rather than recomputed per call: it only
    // needs to change when m_lines itself does, and currentSyntheticWords()
    // is read every frame while the word clock is armed.
    bool m_documentHasWords = false;
    qint64 m_positionUs = 0;
    qint64 m_anchorMonotonicNs = 0;
    double m_rate = 1.0;
    int m_offsetMs = 0;
    bool m_globalOffsetEnabled = false;
    int m_currentLine = -1;
    qint64 m_currentPositionMs = 0;
};
