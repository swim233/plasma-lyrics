#pragma once

#include <QObject>
#include <QQmlEngine>

class BackendConfig : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString serviceBlacklist READ serviceBlacklist WRITE setServiceBlacklist NOTIFY changed)
    Q_PROPERTY(QString musicUrlPrefixes READ musicUrlPrefixes WRITE setMusicUrlPrefixes NOTIFY changed)
    Q_PROPERTY(bool metadataHeuristic READ metadataHeuristic WRITE setMetadataHeuristic NOTIFY changed)
    Q_PROPERTY(bool filterCredits READ filterCredits WRITE setFilterCredits NOTIFY changed)
    Q_PROPERTY(QString neteaseBaseUrl READ neteaseBaseUrl WRITE setNeteaseBaseUrl NOTIFY changed)
    Q_PROPERTY(int networkTimeoutMs READ networkTimeoutMs WRITE setNetworkTimeoutMs NOTIFY changed)
    Q_PROPERTY(bool fileLoggingEnabled READ fileLoggingEnabled WRITE setFileLoggingEnabled NOTIFY changed)
    Q_PROPERTY(QString logFilePath READ logFilePath WRITE setLogFilePath NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

public:
    explicit BackendConfig(QObject *parent = nullptr);

    QString serviceBlacklist() const;
    QString musicUrlPrefixes() const;
    bool metadataHeuristic() const;
    bool filterCredits() const;
    QString neteaseBaseUrl() const;
    int networkTimeoutMs() const;
    bool fileLoggingEnabled() const;
    QString logFilePath() const;
    bool dirty() const;

    void setServiceBlacklist(const QString &value);
    void setMusicUrlPrefixes(const QString &value);
    void setMetadataHeuristic(bool value);
    void setFilterCredits(bool value);
    void setNeteaseBaseUrl(const QString &value);
    void setNetworkTimeoutMs(int value);
    void setFileLoggingEnabled(bool value);
    void setLogFilePath(const QString &value);

    Q_INVOKABLE void load();
    Q_INVOKABLE bool save();

Q_SIGNALS:
    void changed();
    void dirtyChanged();
    void saved();

private:
    void markDirty();

    QString m_serviceBlacklist;
    QString m_musicUrlPrefixes;
    bool m_metadataHeuristic = true;
    bool m_filterCredits = true;
    QString m_neteaseBaseUrl;
    int m_networkTimeoutMs = 8000;
    bool m_fileLoggingEnabled = false;
    QString m_logFilePath;
    bool m_dirty = false;
};
