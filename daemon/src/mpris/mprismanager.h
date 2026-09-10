#pragma once

#include "mprispolicy.h"

#include <QHash>
#include <QObject>
#include <QTimer>

namespace PlasmaLyrics {

class MprisPlayer;

class MprisManager final : public QObject
{
    Q_OBJECT

public:
    explicit MprisManager(PolicyConfig config, QObject *parent = nullptr);
    std::optional<MprisState> activeState() const;

Q_SIGNALS:
    void activeStateChanged(bool trackChanged);
    void playbackRoundStarted();

private Q_SLOTS:
    void onNameOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void addService(const QString &service);
    void removeService(const QString &service);
    void selectActive(bool hintedTrackChange = false);

private:
    void onPlayerChanged(const QString &service, bool metadataChanged, bool anchorChanged, bool becamePlaying);
    // Logs "filtered ..." for a service only when its fingerprint differs
    // from the last one logged for that service; a run of unchanged
    // fingerprints logs once, but a fingerprint that reappears after a
    // different one intervened logs again (only the last value is
    // remembered, not the full history). Forgotten when that service
    // vanishes.
    void logFilteredOnce(const MprisState &state, const QString &reason);

    PolicyConfig m_config;
    QHash<QString, MprisPlayer *> m_players;
    QHash<QString, quint64> m_playingSerials;
    QHash<QString, QString> m_lastFilteredFingerprint;
    QString m_activeService;
    QTimer m_pollTimer;
    quint64 m_serial = 0;
};

} // namespace PlasmaLyrics
