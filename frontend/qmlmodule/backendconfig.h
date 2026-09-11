#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>

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
    Q_PROPERTY(QStringList providerOrder READ providerOrder WRITE setProviderOrder NOTIFY changed)
    Q_PROPERTY(QStringList enabledProviders READ enabledProviders WRITE setEnabledProviders NOTIFY changed)
    Q_PROPERTY(QVariantList providerEntries READ providerEntries NOTIFY changed)
    Q_PROPERTY(bool providerDiscoveryFallback READ providerDiscoveryFallback NOTIFY changed)
    Q_PROPERTY(QString localLyricsDirectory READ localLyricsDirectory WRITE setLocalLyricsDirectory NOTIFY changed)
    Q_PROPERTY(QString amllIndexUrl READ amllIndexUrl WRITE setAmllIndexUrl NOTIFY changed)
    Q_PROPERTY(QString amllContentBaseUrl READ amllContentBaseUrl WRITE setAmllContentBaseUrl NOTIFY changed)
    Q_PROPERTY(int amllTimeoutMs READ amllTimeoutMs WRITE setAmllTimeoutMs NOTIFY changed)
    Q_PROPERTY(int amllIndexRefreshHours READ amllIndexRefreshHours WRITE setAmllIndexRefreshHours NOTIFY changed)
    Q_PROPERTY(bool fileLoggingEnabled READ fileLoggingEnabled WRITE setFileLoggingEnabled NOTIFY changed)
    Q_PROPERTY(QString logFilePath READ logFilePath WRITE setLogFilePath NOTIFY changed)
    Q_PROPERTY(bool debugLoggingEnabled READ debugLoggingEnabled WRITE setDebugLoggingEnabled NOTIFY changed)
    Q_PROPERTY(QString proxyMode READ proxyMode WRITE setProxyMode NOTIFY changed)
    Q_PROPERTY(QString proxyUrl READ proxyUrl WRITE setProxyUrl NOTIFY changed)
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
    QStringList providerOrder() const;
    QStringList enabledProviders() const;
    QVariantList providerEntries() const;
    bool providerDiscoveryFallback() const;
    QString localLyricsDirectory() const;
    QString amllIndexUrl() const;
    QString amllContentBaseUrl() const;
    int amllTimeoutMs() const;
    int amllIndexRefreshHours() const;
    bool fileLoggingEnabled() const;
    QString logFilePath() const;
    bool debugLoggingEnabled() const;
    QString proxyMode() const;
    QString proxyUrl() const;
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
    void setProviderOrder(const QStringList &value);
    void setEnabledProviders(const QStringList &value);
    void setLocalLyricsDirectory(const QString &value);
    void setAmllIndexUrl(const QString &value);
    void setAmllContentBaseUrl(const QString &value);
    void setAmllTimeoutMs(int value);
    void setAmllIndexRefreshHours(int value);
    void setFileLoggingEnabled(bool value);
    void setLogFilePath(const QString &value);
    void setDebugLoggingEnabled(bool value);
    void setProxyMode(const QString &value);
    void setProxyUrl(const QString &value);

    Q_INVOKABLE void load();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool restartService();
    Q_INVOKABLE bool moveProvider(int from, int to);
    Q_INVOKABLE bool setProviderEnabled(const QString &provider, bool enabled);
    // Empty when url parses as a valid manual proxy address; otherwise a
    // user-facing description of what is wrong with it. Only meaningful
    // when proxyMode is "manual" -- callers are responsible for that check.
    Q_INVOKABLE QString proxyUrlError(const QString &url) const;

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
    void discoverProviders();
    QStringList visibleProviderOrder() const;

    QString m_serviceBlacklist;
    QString m_musicUrlPrefixes;
    bool m_platformNetease = true;
    bool m_platformApple = true;
    bool m_metadataHeuristic = true;
    bool m_filterCredits = true;
    QString m_neteaseBaseUrl;
    int m_networkTimeoutMs = 4000;
    QStringList m_providerOrder;
    QStringList m_enabledProviders;
    QStringList m_availableProviders;
    bool m_providerDiscoveryFallback = true;
    QString m_localLyricsDirectory;
    QString m_amllIndexUrl;
    QString m_amllContentBaseUrl;
    int m_amllTimeoutMs = 8000;
    int m_amllIndexRefreshHours = 24;
    bool m_fileLoggingEnabled = false;
    QString m_logFilePath;
    bool m_debugLoggingEnabled = false;
    QString m_proxyMode = QStringLiteral("none");
    QString m_proxyUrl;
    bool m_dirty = false;
    QString m_restartProgram;
    QStringList m_restartArguments;
    QProcess *m_restartProcess = nullptr;
    RestartState m_restartState = RestartIdle;
    bool m_restartInProgress = false;
    QString m_restartError;
};
