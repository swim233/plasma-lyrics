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
    // previousActiveService is the service that was active before this call
    // (captured by the caller before any of its own bookkeeping mutates
    // m_activeService), used only for the "active player changed" log's
    // from= field and its own logging decision. The choosePlayer() call
    // inside still reads m_activeService itself, unchanged, so the actual
    // selection logic is untouched by this parameter.
    // By value, not by reference: every non-removeService() caller passes
    // m_activeService itself, and this function goes on to write that same
    // member -- a reference parameter would silently become an alias for
    // it, correct only as long as every read inside happens before that
    // write. By-value copies it once at the call boundary and removes the
    // trap entirely.
    void selectActive(bool hintedTrackChange, const QString previousActiveService);

private:
    void onPlayerChanged(const QString &service, bool metadataChanged, bool anchorChanged, bool becamePlaying);
    // Logs "filtered ..." for a service only when its fingerprint differs
    // from the last one logged for that service while it has stayed
    // non-music; a run of unchanged fingerprints logs once. selectActive()
    // clears the remembered fingerprint as soon as the service is judged
    // music again, so a fingerprint that is filtered, becomes music, and is
    // later filtered again (even with the same fingerprint) logs once more.
    // Forgotten entirely when the service vanishes.
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
