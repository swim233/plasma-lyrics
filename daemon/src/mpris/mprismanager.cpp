#include "mprismanager.h"

#include "mprisplayer.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>

namespace PlasmaLyrics {
namespace {

constexpr QLatin1StringView servicePrefix("org.mpris.MediaPlayer2.");

} // namespace

MprisManager::MprisManager(PolicyConfig config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
{
    // Match NameOwnerChanged directly rather than going through
    // QDBusServiceWatcher. That class only signals for names handed to
    // addWatchedService(), and it takes no wildcards, so the only names it
    // could ever announce are the ones already on the bus when this
    // constructor ran. The daemon starts with the session, before any player
    // exists, which made every player invisible until it was restarted.
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"), this,
        SLOT(onNameOwnerChanged(QString,QString,QString)));

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

void MprisManager::onNameOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner)
{
    // Every name on the bus reaches this slot, unique connection names
    // included, so the prefix test comes first and does nothing the rest of
    // the time. A name that changes hands arrives with both owners set, and
    // has to drop the old player before adopting the new one.
    if (!service.startsWith(servicePrefix)) {
        return;
    }
    if (!oldOwner.isEmpty()) {
        removeService(service);
    }
    if (!newOwner.isEmpty()) {
        addService(service);
    }
}

void MprisManager::addService(const QString &service)
{
    if (!service.startsWith(servicePrefix) || m_players.contains(service)) {
        return;
    }
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
