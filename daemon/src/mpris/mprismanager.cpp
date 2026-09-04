#include "mprismanager.h"

#include "mprisplayer.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>

namespace PlasmaLyrics {

MprisManager::MprisManager(PolicyConfig config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_watcher(new QDBusServiceWatcher(this))
{
    m_watcher->setConnection(QDBusConnection::sessionBus());
    m_watcher->setWatchMode(QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &MprisManager::addService);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &MprisManager::removeService);

    const QDBusReply<QStringList> names = QDBusConnection::sessionBus().interface()->registeredServiceNames();
    if (names.isValid()) {
        for (const auto &service : names.value()) {
            addService(service);
        }
    }
    m_pollTimer.setInterval(1000);
    connect(&m_pollTimer, &QTimer::timeout, this, [this] {
        if (auto *player = m_players.value(m_activeService)) {
            player->pollPosition();
        }
    });
    m_pollTimer.start();
    selectActive();
}

void MprisManager::addService(const QString &service)
{
    if (!service.startsWith(QStringLiteral("org.mpris.MediaPlayer2.")) || m_players.contains(service)) {
        return;
    }
    m_watcher->addWatchedService(service);
    auto *player = new MprisPlayer(service, this);
    if (player->state().playbackStatus == QStringLiteral("Playing")) {
        m_playingSerials.insert(service, ++m_serial);
    }
    connect(player, &MprisPlayer::changed, this,
            [this, service](bool metadata, bool anchor, bool becamePlaying) {
                onPlayerChanged(service, metadata, anchor, becamePlaying);
            });
    m_players.insert(service, player);
    selectActive(true);
}

void MprisManager::removeService(const QString &service)
{
    if (auto *player = m_players.take(service)) {
        player->deleteLater();
    }
    m_playingSerials.remove(service);
    const bool wasActive = service == m_activeService;
    if (wasActive) {
        m_activeService.clear();
    }
    selectActive(wasActive);
}

void MprisManager::onPlayerChanged(const QString &service, bool metadataChanged, bool anchorChanged, bool becamePlaying)
{
    if (becamePlaying) {
        // A monotonically increasing recency marker lives in manager state rather than trusting trackid.
        m_playingSerials.insert(service, ++m_serial);
    }
    const QString previous = m_activeService;
    selectActive(metadataChanged);
    if (service == m_activeService && previous == m_activeService && (metadataChanged || anchorChanged)) {
        Q_EMIT activeStateChanged(metadataChanged);
    }
}

void MprisManager::selectActive(bool hintedTrackChange)
{
    QList<MprisState> states;
    states.reserve(m_players.size());
    for (auto *player : std::as_const(m_players)) {
        auto state = player->state();
        state.playingSerial = m_playingSerials.value(state.service);
        states.append(state);
    }
    const QString selected = MprisPolicy::choosePlayer(states, m_activeService, m_config);
    if (selected != m_activeService) {
        m_activeService = selected;
        Q_EMIT activeStateChanged(true);
    } else if (hintedTrackChange && selected.isEmpty()) {
        Q_EMIT activeStateChanged(true);
    }
}

std::optional<MprisState> MprisManager::activeState() const
{
    const auto *player = m_players.value(m_activeService);
    if (player) {
        auto state = player->state();
        state.music = MprisPolicy::isMusic(state, m_config);
        return state;
    }

    // Keep a currently-playing non-music source visible to the resolver as a
    // filtered state. This lets the widget stay empty for a video instead of
    // claiming that nothing is playing, while it still never searches for it.
    std::optional<MprisState> filtered;
    quint64 newestSerial = 0;
    for (auto *candidate : m_players) {
        auto state = candidate->state();
        const quint64 serial = m_playingSerials.value(state.service);
        if (state.playbackStatus == QStringLiteral("Playing") && serial >= newestSerial) {
            state.music = false;
            filtered = state;
            newestSerial = serial;
        }
    }
    return filtered;
}

} // namespace PlasmaLyrics
