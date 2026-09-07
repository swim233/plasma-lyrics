#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

class QProcess;

class BackendConfig : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString serviceBlacklist READ serviceBlacklist WRITE setServiceBlacklist NOTIFY changed)
    Q_PROPERTY(QString musicUrlPrefixes READ musicUrlPrefixes WRITE setMusicUrlPrefixes NOTIFY changed)
    Q_PROPERTY(bool platformNetease READ platformNetease WRITE setPlatformNetease NOTIFY changed)
    Q_PROPERTY(bool platformApple READ platformApple WRITE setPlatformApple NOTIFY changed)
    Q_PROPERTY(bool metadataHeuristic READ metadataHeuristic WRITE setMetadataHeuristic NOTIFY changed)
    Q_PROPERTY(bool filterCredits READ filterCredits WRITE setFilterCredits NOTIFY changed)
    Q_PROPERTY(QString neteaseBaseUrl READ neteaseBaseUrl WRITE setNeteaseBaseUrl NOTIFY changed)
    Q_PROPERTY(int networkTimeoutMs READ networkTimeoutMs WRITE setNetworkTimeoutMs NOTIFY changed)
    Q_PROPERTY(bool fileLoggingEnabled READ fileLoggingEnabled WRITE setFileLoggingEnabled NOTIFY changed)
    Q_PROPERTY(QString logFilePath READ logFilePath WRITE setLogFilePath NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(RestartState restartState READ restartState NOTIFY restartStateChanged)
    Q_PROPERTY(bool restartInProgress READ restartInProgress NOTIFY restartInProgressChanged)
    Q_PROPERTY(QString restartError READ restartError NOTIFY restartErrorChanged)

public:
    enum RestartState {
        RestartIdle,
        Restarting,
        RestartSucceeded,
        RestartFailed,
    };
    Q_ENUM(RestartState)

    explicit BackendConfig(QObject *parent = nullptr);
    BackendConfig(QString restartProgram, QStringList restartArguments, QObject *parent = nullptr);
    ~BackendConfig() override;

    QString serviceBlacklist() const;
    QString musicUrlPrefixes() const;
    bool platformNetease() const;
    bool platformApple() const;
    bool metadataHeuristic() const;
    bool filterCredits() const;
    QString neteaseBaseUrl() const;
    int networkTimeoutMs() const;
    bool fileLoggingEnabled() const;
    QString logFilePath() const;
    bool dirty() const;
    RestartState restartState() const;
    bool restartInProgress() const;
    QString restartError() const;

    void setServiceBlacklist(const QString &value);
    void setMusicUrlPrefixes(const QString &value);
    void setPlatformNetease(bool value);
    void setPlatformApple(bool value);
    void setMetadataHeuristic(bool value);
    void setFilterCredits(bool value);
    void setNeteaseBaseUrl(const QString &value);
    void setNetworkTimeoutMs(int value);
    void setFileLoggingEnabled(bool value);
    void setLogFilePath(const QString &value);

    Q_INVOKABLE void load();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool restartService();

Q_SIGNALS:
    void changed();
    void dirtyChanged();
    void saved();
    void restartStateChanged();
    void restartInProgressChanged();
    void restartErrorChanged();
    void restartFinished(bool success, const QString &error);

private:
    void markDirty();
    void finishRestart(RestartState state, const QString &error = {});

    QString m_serviceBlacklist;
    QString m_musicUrlPrefixes;
    bool m_platformNetease = true;
    bool m_platformApple = true;
    bool m_metadataHeuristic = true;
    bool m_filterCredits = true;
    QString m_neteaseBaseUrl;
    int m_networkTimeoutMs = 4000;
    bool m_fileLoggingEnabled = false;
    QString m_logFilePath;
    bool m_dirty = false;
    QString m_restartProgram;
    QStringList m_restartArguments;
    QProcess *m_restartProcess = nullptr;
    RestartState m_restartState = RestartIdle;
    bool m_restartInProgress = false;
    QString m_restartError;
};
