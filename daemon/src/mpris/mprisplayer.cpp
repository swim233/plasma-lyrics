#include "mprisplayer.h"

#include "mprispolicy.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>
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
    : MprisPlayer(std::move(service), &MprisPlayer::monotonicNowNs, parent)
{
}

MprisPlayer::MprisPlayer(QString service, std::function<qint64()> clock,
                         QObject *parent)
    : QObject(parent)
    , m_clock(clock ? std::move(clock)
                    : std::function<qint64()>(&MprisPlayer::monotonicNowNs))
{
    m_state.service = std::move(service);
    QDBusConnection::sessionBus().connect(
        m_state.service, QString::fromLatin1(objectPath), QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"), this,
        SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    QDBusConnection::sessionBus().connect(
        m_state.service, QString::fromLatin1(objectPath),
        QString::fromLatin1(playerInterface), QStringLiteral("Seeked"), this,
        SLOT(onSeeked(qlonglong)));
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
    const double oldRate = m_state.rate;
    const qint64 now = m_clock();
    const bool hasPosition = properties.contains(QStringLiteral("Position"));
    const bool hasMetadata = properties.contains(QStringLiteral("Metadata"));
    const bool hasRate = properties.contains(QStringLiteral("Rate"));
    const double newRate = hasRate
        ? unwrap(properties.value(QStringLiteral("Rate"))).toDouble() : oldRate;
    const bool rateChanged = hasRate
        && (!std::isfinite(oldRate) || !std::isfinite(newRate)
            || qAbs(newRate - oldRate) > 0.000000001);

    // Rate is sometimes the only changed MPRIS property. Advance from the
    // latest coherent Position sample using the rate that governed that
    // interval before installing the new rate. A normal poll refreshes this
    // sample without necessarily moving the published anchor, so using the
    // latter here would count the pre-poll interval a second time.
    const qint64 samplePosition = m_lastSamplePositionUs >= 0
        ? m_lastSamplePositionUs : m_state.positionUs;
    const qint64 sampleMonotonicNs = m_lastSampleMonotonicNs > 0
        ? m_lastSampleMonotonicNs : m_state.anchorMonotonicNs;
    if (!initial && rateChanged && !hasPosition && !hasMetadata
        && oldStatus == QStringLiteral("Playing")
        && sampleMonotonicNs > 0 && now > sampleMonotonicNs
        && std::isfinite(oldRate) && oldRate > 0.0) {
        const long double elapsedUs = static_cast<long double>(
            now - sampleMonotonicNs) / 1000.0L;
        const long double advanced = static_cast<long double>(samplePosition)
            + elapsedUs * static_cast<long double>(oldRate);
        const long double upper = m_state.lengthUs > 0
            ? static_cast<long double>(m_state.lengthUs)
            : static_cast<long double>(std::numeric_limits<qint64>::max());
        m_state.positionUs = static_cast<qint64>(
            std::clamp(advanced, 0.0L, upper));
    }

    if (properties.contains(QStringLiteral("PlaybackStatus"))) {
        m_state.playbackStatus = unwrap(properties.value(QStringLiteral("PlaybackStatus"))).toString();
    }
    if (hasRate) {
        m_state.rate = newRate;
    }
    if (hasPosition) {
        m_state.positionUs = unwrap(properties.value(QStringLiteral("Position"))).toLongLong();
    }
    if (hasMetadata) {
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
    const bool playbackRound = !initial && !metadataChanged
        && oldStatus == QStringLiteral("Playing")
        && hasPosition
        && MprisPolicy::isPlaybackRound(
            m_lastSamplePositionUs, m_lastSampleMonotonicNs,
            m_state.positionUs, now, m_state.lengthUs, oldRate,
            m_state.playbackStatus);
    if (initial || metadataChanged || hasPosition
        || oldStatus != m_state.playbackStatus || rateChanged) {
        m_state.anchorMonotonicNs = now;
        m_lastSamplePositionUs = m_state.positionUs;
        m_lastSampleMonotonicNs = now;
    }
    if (!initial) {
        const bool statusChanged = oldStatus != m_state.playbackStatus;
        const bool anchorChanged = hasPosition || statusChanged || rateChanged;
        const bool becamePlaying = oldStatus != QStringLiteral("Playing")
            && m_state.playbackStatus == QStringLiteral("Playing");
        // A resume re-anchors against the last known position, which is stale if
        // the user seeked while paused. Neither browser integration nor several
        // native players emit Seeked, so read Position back explicitly whenever
        // the track or the transport state moved without one.
        const bool needsPositionPoll = (metadataChanged || statusChanged) && !hasPosition;
        const QPointer<MprisPlayer> self(this);

        Q_EMIT changed(metadataChanged, anchorChanged, becamePlaying);

        if (!self) {
            return;
        }
        if (playbackRound) {
            Q_EMIT playbackRoundStarted();
            if (!self) {
                return;
            }
        }
        if (needsPositionPoll) {
            QTimer::singleShot(0, self.data(), &MprisPlayer::pollPosition);
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
    const qint64 now = m_clock();
    const qint64 position = reply.value().variant().toLongLong();
    const bool jump = MprisPolicy::isPositionJump(m_lastSamplePositionUs, m_lastSampleMonotonicNs,
                                                   position, now, m_state.rate, m_state.playbackStatus);
    const bool playbackRound = MprisPolicy::isPlaybackRound(
        m_lastSamplePositionUs, m_lastSampleMonotonicNs, position, now,
        m_state.lengthUs, m_state.rate, m_state.playbackStatus);
    m_state.positionUs = position;
    if (jump) {
        m_state.anchorMonotonicNs = now;
    }
    m_lastSamplePositionUs = position;
    m_lastSampleMonotonicNs = now;
    if (jump) {
        const QPointer<MprisPlayer> self(this);
        Q_EMIT changed(false, true, false);
        if (!self) {
            return;
        }
        if (playbackRound) {
            Q_EMIT playbackRoundStarted();
        }
    }
}

void MprisPlayer::onSeeked(qlonglong position)
{
    const qint64 now = m_clock();
    // Most Seeked signals describe a manual discontinuity and therefore must
    // suppress round detection. A few players also emit it for their natural
    // repeat boundary, so retain a round only when the same monotonic/rate
    // evidence independently proves that playback could have crossed the end.
    const bool playbackRound = MprisPolicy::isPlaybackRound(
        m_lastSamplePositionUs, m_lastSampleMonotonicNs, position, now,
        m_state.lengthUs, m_state.rate, m_state.playbackStatus);
    m_state.positionUs = position;
    m_state.anchorMonotonicNs = now;
    m_lastSamplePositionUs = position;
    m_lastSampleMonotonicNs = now;
    const QPointer<MprisPlayer> self(this);
    Q_EMIT changed(false, true, false);
    if (self && playbackRound) {
        Q_EMIT playbackRoundStarted();
    }
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
