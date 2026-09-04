#include "mprisplayer.h"

#include "mprispolicy.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QTimer>
#include <QUrl>
#include <time.h>

namespace PlasmaLyrics {
namespace {

constexpr auto objectPath = "/org/mpris/MediaPlayer2";
constexpr auto playerInterface = "org.mpris.MediaPlayer2.Player";

QVariant unwrap(const QVariant &value)
{
    return value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant() : value;
}

} // namespace

MprisPlayer::MprisPlayer(QString service, QObject *parent)
    : QObject(parent)
{
    m_state.service = std::move(service);
    QDBusConnection::sessionBus().connect(
        m_state.service, QString::fromLatin1(objectPath), QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"), this,
        SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    refresh();
}

qint64 MprisPlayer::monotonicNowNs()
{
    timespec value{};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return static_cast<qint64>(value.tv_sec) * 1000000000LL + value.tv_nsec;
}

MprisState MprisPlayer::state() const
{
    return m_state;
}

QVariantMap MprisPlayer::variantMap(const QVariant &value)
{
    const QVariant plain = unwrap(value);
    if (plain.metaType() == QMetaType::fromType<QDBusArgument>()) {
        return qdbus_cast<QVariantMap>(plain.value<QDBusArgument>());
    }
    return plain.toMap();
}

QStringList MprisPlayer::variantStringList(const QVariant &value)
{
    const QVariant plain = unwrap(value);
    if (plain.metaType() == QMetaType::fromType<QDBusArgument>()) {
        return qdbus_cast<QStringList>(plain.value<QDBusArgument>());
    }
    return plain.toStringList();
}

QVariantMap MprisPlayer::getAll(const QString &interface) const
{
    QDBusInterface properties(m_state.service, QString::fromLatin1(objectPath),
                              QStringLiteral("org.freedesktop.DBus.Properties"), QDBusConnection::sessionBus());
    const QDBusReply<QVariantMap> reply = properties.call(QStringLiteral("GetAll"), interface);
    return reply.isValid() ? reply.value() : QVariantMap{};
}

void MprisPlayer::refresh()
{
    const auto root = getAll(QStringLiteral("org.mpris.MediaPlayer2"));
    m_state.identity = root.value(QStringLiteral("Identity")).toString();
    apply(getAll(QString::fromLatin1(playerInterface)), true);
}

void MprisPlayer::apply(const QVariantMap &properties, bool initial)
{
    const QString oldFingerprint = m_state.fingerprint;
    const QString oldStatus = m_state.playbackStatus;

    if (properties.contains(QStringLiteral("PlaybackStatus"))) {
        m_state.playbackStatus = unwrap(properties.value(QStringLiteral("PlaybackStatus"))).toString();
    }
    if (properties.contains(QStringLiteral("Rate"))) {
        m_state.rate = unwrap(properties.value(QStringLiteral("Rate"))).toDouble();
    }
    if (properties.contains(QStringLiteral("Position"))) {
        m_state.positionUs = unwrap(properties.value(QStringLiteral("Position"))).toLongLong();
    }
    if (properties.contains(QStringLiteral("Metadata"))) {
        const auto metadata = variantMap(properties.value(QStringLiteral("Metadata")));
        m_state.title = unwrap(metadata.value(QStringLiteral("xesam:title"))).toString();
        m_state.artists = variantStringList(metadata.value(QStringLiteral("xesam:artist")));
        m_state.album = unwrap(metadata.value(QStringLiteral("xesam:album"))).toString();
        m_state.lengthUs = unwrap(metadata.value(QStringLiteral("mpris:length"))).toLongLong();
        m_state.trackId = unwrap(metadata.value(QStringLiteral("mpris:trackid"))).toString();
        m_state.url = unwrap(metadata.value(QStringLiteral("xesam:url"))).toString();
        m_state.mediaSrc = unwrap(metadata.value(QStringLiteral("kde:mediaSrc"))).toString();
        m_state.kdePid = unwrap(metadata.value(QStringLiteral("kde:pid"))).toLongLong();
        m_state.fingerprint = MprisPolicy::fingerprint(m_state);
    }
    const bool metadataChanged = oldFingerprint != m_state.fingerprint;
    const qint64 now = monotonicNowNs();
    if (initial || metadataChanged || properties.contains(QStringLiteral("Position"))
        || oldStatus != m_state.playbackStatus) {
        m_state.anchorMonotonicNs = now;
        m_lastSamplePositionUs = m_state.positionUs;
        m_lastSampleMonotonicNs = now;
    }
    if (!initial) {
        Q_EMIT changed(metadataChanged,
                       properties.contains(QStringLiteral("Position")) || oldStatus != m_state.playbackStatus,
                       oldStatus != QStringLiteral("Playing") && m_state.playbackStatus == QStringLiteral("Playing"));
        // A resume re-anchors against the last known position, which is stale if
        // the user seeked while paused. Neither browser integration nor several
        // native players emit Seeked, so read Position back explicitly whenever
        // the track or the transport state moved without one.
        if ((metadataChanged || oldStatus != m_state.playbackStatus)
            && !properties.contains(QStringLiteral("Position"))) {
            QTimer::singleShot(0, this, &MprisPlayer::pollPosition);
        }
    }
}

void MprisPlayer::pollPosition()
{
    QDBusInterface properties(m_state.service, QString::fromLatin1(objectPath),
                              QStringLiteral("org.freedesktop.DBus.Properties"), QDBusConnection::sessionBus());
    const QDBusReply<QDBusVariant> reply = properties.call(QStringLiteral("Get"),
                                                            QString::fromLatin1(playerInterface),
                                                            QStringLiteral("Position"));
    if (!reply.isValid()) {
        return;
    }
    const qint64 now = monotonicNowNs();
    const qint64 position = reply.value().variant().toLongLong();
    const bool jump = MprisPolicy::isPositionJump(m_lastSamplePositionUs, m_lastSampleMonotonicNs,
                                                   position, now, m_state.rate, m_state.playbackStatus);
    m_state.positionUs = position;
    if (jump) {
        m_state.anchorMonotonicNs = now;
        Q_EMIT changed(false, true, false);
    }
    m_lastSamplePositionUs = position;
    m_lastSampleMonotonicNs = now;
}

void MprisPlayer::onPropertiesChanged(const QString &interface,
                                      const QVariantMap &changedProperties,
                                      const QStringList &invalidatedProperties)
{
    Q_UNUSED(invalidatedProperties)
    if (interface == QString::fromLatin1(playerInterface)) {
        apply(changedProperties);
    }
}

} // namespace PlasmaLyrics
