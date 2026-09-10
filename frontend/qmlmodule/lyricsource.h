#pragma once

#include "core/lyric/lyricmodel.h"

#include <QFileSystemWatcher>
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
    Q_PROPERTY(bool canControlProvider READ canControlProvider NOTIFY canControlProviderChanged)
    Q_PROPERTY(bool controlInProgress READ controlInProgress NOTIFY controlInProgressChanged)
    Q_PROPERTY(QString controlError READ controlError NOTIFY controlErrorChanged)

public:
    explicit LyricSource(QObject *parent = nullptr);
    // The QString argument is retained as a source-compatible no-op for old
    // tests/plugins. Widgets no longer open the SQLite store.
    explicit LyricSource(std::function<qint64()> clock, QString = {}, QObject *parent = nullptr);

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
    bool canControlProvider() const;
    bool controlInProgress() const;
    QString controlError() const;

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
    void advance();
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
    bool m_controlInProgress = false;
    QString m_controlError;
    PlasmaLyrics::LyricLines m_lines;
    qint64 m_positionUs = 0;
    qint64 m_anchorMonotonicNs = 0;
    double m_rate = 1.0;
    int m_offsetMs = 0;
    bool m_globalOffsetEnabled = false;
    int m_currentLine = -1;
    qint64 m_currentPositionMs = 0;
};
