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
    Q_PROPERTY(QString lyricState READ lyricState NOTIFY lyricStateChanged)
    Q_PROPERTY(QString playbackStatus READ playbackStatus NOTIFY playbackChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY trackChanged)
    Q_PROPERTY(QString trackArtists READ trackArtists NOTIFY trackChanged)
    Q_PROPERTY(QString currentText READ currentText NOTIFY currentLineChanged)
    Q_PROPERTY(QString currentTranslation READ currentTranslation NOTIFY currentLineChanged)
    Q_PROPERTY(qint64 currentPositionMs READ currentPositionMs NOTIFY currentPositionChanged)
    Q_PROPERTY(int offsetMs READ offsetMs NOTIFY offsetChanged)
    Q_PROPERTY(bool canAdjustOffset READ canAdjustOffset NOTIFY trackChanged)

public:
    explicit LyricSource(QObject *parent = nullptr);
    explicit LyricSource(std::function<qint64()> clock, QObject *parent = nullptr);

    QString snapshotPath() const;
    void setSnapshotPath(const QString &path);
    bool serviceAvailable() const;
    bool stale() const;
    QString lyricState() const;
    QString playbackStatus() const;
    QString trackTitle() const;
    QString trackArtists() const;
    QString currentText() const;
    QString currentTranslation() const;
    qint64 currentPositionMs() const;
    int offsetMs() const;
    bool canAdjustOffset() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE bool adjustOffset(int deltaMs);
    Q_INVOKABLE bool resetOffset();

Q_SIGNALS:
    void snapshotPathChanged();
    void statusChanged();
    void lyricStateChanged();
    void playbackChanged();
    void trackChanged();
    void currentLineChanged();
    void currentPositionChanged();
    void offsetChanged();

private:
    static qint64 monotonicNowNs();
    void rearm();
    void setUnavailable(bool stale);
    void updateServiceHealth();
    void advance();

    QFileSystemWatcher m_watcher;
    QTimer m_retryTimer;
    QTimer m_frameTimer;
    QTimer m_healthTimer;
    std::function<qint64()> m_clock;
    QString m_snapshotPath;
    bool m_serviceAvailable = false;
    bool m_stale = false;
    qint64 m_pid = 0;
    qint64 m_sequence = -1;
    QString m_lyricState = QStringLiteral("filtered");
    QString m_playbackStatus = QStringLiteral("Stopped");
    QString m_trackTitle;
    QString m_trackArtists;
    QString m_provider;
    QString m_trackId;
    PlasmaLyrics::LyricLines m_lines;
    qint64 m_positionUs = 0;
    qint64 m_anchorMonotonicNs = 0;
    double m_rate = 1.0;
    int m_offsetMs = 0;
    int m_currentLine = -1;
    qint64 m_currentPositionMs = 0;
};
