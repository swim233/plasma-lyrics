#include "lyricsource.h"

#include "core/lyric/lyricmodel.h"
#include "core/lyric/timeline.h"

#include <QDir>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <KLocalizedString>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <time.h>
#include <unistd.h>

using namespace PlasmaLyrics;

namespace {

QString defaultSnapshotPath()
{
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty()) {
        runtime = QDir::tempPath() + QStringLiteral("/runtime-") + QString::number(getuid());
    }
    return runtime + QStringLiteral("/plasma-lyricsd/state.json");
}

bool processExists(qint64 pid)
{
    if (pid <= 0) {
        return false;
    }
    errno = 0;
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

QString localizedControlError(const QString &error)
{
    const char *domain = "plasma_applet_io.github.swim233.plasma-lyrics";
    if (error == QStringLiteral("no-current-song")) {
        return i18nd(domain, "No current song is available.");
    }
    if (error == QStringLiteral("song-changed")) {
        return i18nd(domain, "The current song changed before the command was applied.");
    }
    if (error == QStringLiteral("provider-unavailable")) {
        return i18nd(domain, "The requested lyrics source is unavailable.");
    }
    if (error == QStringLiteral("preference-save-failed")) {
        return i18nd(domain, "Could not save the song's preferred lyrics source.");
    }
    if (error == QStringLiteral("preference-clear-failed")) {
        return i18nd(domain, "Could not clear the song's preferred lyrics source.");
    }
    if (error == QStringLiteral("no-track-ref")) {
        return i18nd(domain, "There are no lyrics to adjust for the current song.");
    }
    if (error == QStringLiteral("offset-save-failed")) {
        return i18nd(domain, "Could not save the lyric offset.");
    }
    return error.isEmpty()
        ? QString()
        : i18nd(domain, "Lyrics source command failed: %1", error);
}

} // namespace

LyricSource::LyricSource(QObject *parent)
    : LyricSource(&LyricSource::monotonicNowNs, QString(), parent)
{
}

LyricSource::LyricSource(std::function<qint64()> clock, QString, QObject *parent)
    : QObject(parent)
    , m_clock(std::move(clock))
    , m_snapshotPath(defaultSnapshotPath())
{
    m_retryTimer.setInterval(2000);
    // The frame timer is armed at the next line boundary rather than polling:
    // the position is analytic (anchor + monotonic clock), so the switch time is
    // known exactly. See DESIGN.md decision 38.
    m_frameTimer.setSingleShot(true);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    m_healthTimer.setInterval(2000);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &LyricSource::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &LyricSource::reload);
    connect(&m_retryTimer, &QTimer::timeout, this, &LyricSource::reload);
    connect(&m_frameTimer, &QTimer::timeout, this, &LyricSource::advance);
    connect(&m_healthTimer, &QTimer::timeout, this, &LyricSource::updateServiceHealth);
    m_healthTimer.start();
    QTimer::singleShot(0, this, &LyricSource::reload);
}

qint64 LyricSource::monotonicNowNs()
{
    timespec value{};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return static_cast<qint64>(value.tv_sec) * 1000000000LL + value.tv_nsec;
}

QString LyricSource::snapshotPath() const { return m_snapshotPath; }
bool LyricSource::serviceAvailable() const { return m_serviceAvailable; }
bool LyricSource::stale() const { return m_stale; }
bool LyricSource::determined() const { return m_determined; }
QString LyricSource::lyricState() const { return m_lyricState; }
QString LyricSource::playbackStatus() const { return m_playbackStatus; }
QString LyricSource::trackTitle() const { return m_trackTitle; }
QString LyricSource::trackArtists() const { return m_trackArtists; }
qint64 LyricSource::currentPositionMs() const { return m_currentPositionMs; }
int LyricSource::offsetMs() const { return m_offsetMs; }
bool LyricSource::globalOffsetEnabled() const { return m_globalOffsetEnabled; }
QString LyricSource::fingerprint() const { return m_fingerprint; }
QString LyricSource::preferredProvider() const { return m_preferredProvider; }
QString LyricSource::effectivePreferredProvider() const { return m_effectivePreferredProvider; }
QString LyricSource::actualProvider() const { return m_actualProvider; }
bool LyricSource::temporaryFallback() const { return m_temporaryFallback; }
QStringList LyricSource::availableProviders() const { return m_availableProviders; }
QString LyricSource::switchingProvider() const { return m_switchingProvider; }
bool LyricSource::controlInProgress() const
{
    return m_controlInProgress || !m_switchingProvider.isEmpty();
}
QString LyricSource::controlError() const { return m_controlError; }
bool LyricSource::hasTrackRef() const { return !m_provider.isEmpty() && !m_trackId.isEmpty(); }

QString LyricSource::providerDisplayName(const QString &provider) const
{
    constexpr auto domain = "plasma_applet_io.github.swim233.plasma-lyrics";
    if (provider == QStringLiteral("amll")) return i18nd(domain, "AMLL");
    if (provider == QStringLiteral("netease")) return i18nd(domain, "NetEase");
    if (provider == QStringLiteral("local")) return i18nd(domain, "Local files");
    if (provider == QStringLiteral("waylyrics")) return i18nd(domain, "Waylyrics import");
    if (provider.isEmpty()) return i18nd(domain, "No lyrics source");
    return i18nd(domain, "Other lyrics source (%1)", provider);
}

bool LyricSource::canControlProvider() const
{
    return m_serviceAvailable && !m_stale && !m_fingerprint.isEmpty()
        && !controlInProgress();
}

bool LyricSource::canAdjustOffset() const
{
    // Global mode shares one offset across every track, including tracks
    // this instance never resolved a provider/trackId for (no lyrics found,
    // or a purely instrumental track) -- gating on hasTrackRef() there would
    // make the menu actions dead exactly when the global offset is most
    // useful. Per-track mode keeps the original gate: adjusting requires an
    // actual (provider, trackId) to key the per-track table on.
    return m_serviceAvailable && !m_stale && !m_fingerprint.isEmpty()
        && !controlInProgress() && (m_globalOffsetEnabled || hasTrackRef());
}

QString LyricSource::currentText() const
{
    return m_currentLine >= 0 && m_currentLine < m_lines.size() ? m_lines[m_currentLine].text : QString();
}

QString LyricSource::currentTranslation() const
{
    if (m_currentLine < 0 || m_currentLine >= m_lines.size() || !m_lines[m_currentLine].translation) {
        return {};
    }
    return *m_lines[m_currentLine].translation;
}

void LyricSource::setSnapshotPath(const QString &path)
{
    if (path == m_snapshotPath) {
        return;
    }
    const auto watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty()) {
        m_watcher.removePaths(watched);
    }
    m_snapshotPath = path;
    m_sequence = -1;
    Q_EMIT snapshotPathChanged();
    reload();
}

void LyricSource::rearm()
{
    const QString directory = QFileInfo(m_snapshotPath).absolutePath();
    if (QFileInfo::exists(directory) && !m_watcher.directories().contains(directory)) {
        m_watcher.addPath(directory);
    }
    if (QFileInfo::exists(m_snapshotPath) && !m_watcher.files().contains(m_snapshotPath)) {
        m_watcher.addPath(m_snapshotPath);
    }
}

void LyricSource::setUnavailable(bool staleValue)
{
    const bool changed = m_serviceAvailable || m_stale != staleValue;
    m_serviceAvailable = false;
    m_stale = staleValue;
    m_frameTimer.stop();
    if (!m_retryTimer.isActive()) {
        m_retryTimer.start();
    }
    if (changed) {
        Q_EMIT statusChanged();
        // canAdjustOffset() reads m_serviceAvailable directly in global
        // mode, so anything that can flip serviceAvailable/stale has to
        // re-notify it too.
        Q_EMIT canAdjustOffsetChanged();
        Q_EMIT canControlProviderChanged();
    }
}

void LyricSource::updateServiceHealth()
{
    if (m_pid > 0 && !processExists(m_pid)) {
        setUnavailable(true);
        return;
    }
}

void LyricSource::reload()
{
    reloadImpl();
    // "Determined" means "at least one reload has run to completion",
    // success or failure alike. Wrapping reloadImpl() rather than sprinkling
    // a call at each of its several return points keeps that guarantee
    // structural -- a future new early return in reloadImpl() cannot
    // silently forget it, the way it would if this lived inline.
    setDetermined(true);
}

void LyricSource::setDetermined(bool value)
{
    // Deliberately its own signal rather than folding into statusChanged():
    // on the cold-start-with-a-dead-daemon path, setUnavailable(false) runs
    // with m_serviceAvailable and m_stale already sitting at their false
    // initial values, so its own `changed` check comes out false and it
    // never emits statusChanged() at all (see setUnavailable() below).
    // determined has to fire regardless of that -- it is the only signal
    // telling VisibilityPolicy (DESIGN.md decision 40) that the "maybe still
    // loading" undetermined state is over, one way or the other. Reusing
    // statusChanged would leave a bound VisibilityPolicy hidden forever, with
    // no diagnostic text either, on exactly the daemon-down path that
    // decision 40's "!serviceAvailable 时绝不隐藏" rule exists to protect.
    if (m_determined == value) {
        return;
    }
    m_determined = value;
    Q_EMIT determinedChanged();
}

void LyricSource::reloadImpl()
{
    rearm();
    QFile file(m_snapshotPath);
    if (!file.open(QIODevice::ReadOnly)) {
        setUnavailable(false);
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value(QStringLiteral("schema")).toInt() != 1) {
        setUnavailable(false);
        return;
    }
    const auto root = document.object();
    const qint64 sequence = root.value(QStringLiteral("seq")).toInteger(-1);
    m_pid = root.value(QStringLiteral("daemon")).toObject().value(QStringLiteral("pid")).toInteger();
    if (!processExists(m_pid)) {
        setUnavailable(true);
        return;
    }
    const bool statusWasChanged = !m_serviceAvailable || m_stale;
    m_serviceAvailable = true;
    m_stale = false;
    m_retryTimer.stop();
    if (statusWasChanged) {
        Q_EMIT statusChanged();
        Q_EMIT canAdjustOffsetChanged();
        Q_EMIT canControlProviderChanged();
    }
    if (sequence == m_sequence) {
        if (statusWasChanged) {
            // setUnavailable() stopped the frame timer, and a repeat sequence
            // skips the rest of this function. Nothing else would re-arm it.
            advance();
        }
        return;
    }
    m_sequence = sequence;

    const auto oldState = m_lyricState;
    const auto oldPlayback = m_playbackStatus;
    const auto oldTitle = m_trackTitle;
    const auto oldArtists = m_trackArtists;
    const auto oldProvider = m_provider;
    const auto oldTrackId = m_trackId;
    const auto oldFingerprint = m_fingerprint;
    const auto oldPreferredProvider = m_preferredProvider;
    const auto oldEffectivePreferredProvider = m_effectivePreferredProvider;
    const auto oldActualProvider = m_actualProvider;
    const auto oldAvailableProviders = m_availableProviders;
    const bool oldTemporaryFallback = m_temporaryFallback;
    const QString oldSwitchingProvider = m_switchingProvider;
    const int oldOffsetMs = m_offsetMs;
    const bool oldGlobalOffsetEnabled = m_globalOffsetEnabled;

    const auto track = root.value(QStringLiteral("track")).toObject();
    m_fingerprint = track.value(QStringLiteral("fingerprint")).toString();
    m_trackTitle = track.value(QStringLiteral("title")).toString();
    m_trackArtists = track.value(QStringLiteral("artists")).toVariant().toStringList().join(QStringLiteral(" / "));
    const auto ref = track.value(QStringLiteral("ref")).toObject();
    m_provider = ref.value(QStringLiteral("provider")).toString();
    m_trackId = ref.value(QStringLiteral("trackId")).toString();
    const auto playback = root.value(QStringLiteral("playback")).toObject();
    m_playbackStatus = playback.value(QStringLiteral("status")).toString(QStringLiteral("Stopped"));
    m_positionUs = playback.value(QStringLiteral("positionUs")).toInteger();
    m_anchorMonotonicNs = playback.value(QStringLiteral("anchorMonotonicNs")).toInteger();
    m_rate = playback.value(QStringLiteral("rate")).toDouble(1.0);
    const auto lyric = root.value(QStringLiteral("lyric")).toObject();
    m_lyricState = lyric.value(QStringLiteral("state")).toString(QStringLiteral("filtered"));
    m_preferredProvider = lyric.value(QStringLiteral("preferredProvider")).toString();
    m_effectivePreferredProvider = lyric.value(QStringLiteral("effectivePreferredProvider")).toString();
    m_actualProvider = lyric.value(QStringLiteral("actualProvider")).toString(m_provider);
    m_temporaryFallback = lyric.value(QStringLiteral("temporaryFallback")).toBool();
    m_availableProviders = lyric.value(QStringLiteral("availableProviders")).toVariant().toStringList();
    m_switchingProvider = lyric.value(QStringLiteral("switchingProvider")).toString();
    m_globalOffsetEnabled = lyric.value(QStringLiteral("globalOffsetEnabled")).toBool();
    m_offsetMs = lyric.value(QStringLiteral("offsetMs")).toInt();
    m_lines.clear();
    for (const auto &value : lyric.value(QStringLiteral("lines")).toArray()) {
        if (const auto line = lineFromJson(value.toObject())) {
            m_lines.append(*line);
        }
    }

    if (oldState != m_lyricState) Q_EMIT lyricStateChanged();
    if (oldPlayback != m_playbackStatus) Q_EMIT playbackChanged();
    if (oldTitle != m_trackTitle || oldArtists != m_trackArtists
        || oldFingerprint != m_fingerprint
        || oldProvider != m_provider || oldTrackId != m_trackId) {
        Q_EMIT trackChanged();
        // Per-track mode's canAdjustOffset() depends on hasTrackRef().
        Q_EMIT canAdjustOffsetChanged();
    }
    if (oldPreferredProvider != m_preferredProvider
        || oldEffectivePreferredProvider != m_effectivePreferredProvider
        || oldActualProvider != m_actualProvider
        || oldTemporaryFallback != m_temporaryFallback
        || oldAvailableProviders != m_availableProviders
        || oldSwitchingProvider != m_switchingProvider) {
        Q_EMIT providerStateChanged();
    }
    if (oldSwitchingProvider != m_switchingProvider) {
        Q_EMIT controlInProgressChanged();
        Q_EMIT canControlProviderChanged();
        Q_EMIT canAdjustOffsetChanged();
    }
    if (oldGlobalOffsetEnabled != m_globalOffsetEnabled) {
        Q_EMIT globalOffsetEnabledChanged();
        Q_EMIT canAdjustOffsetChanged();
    }
    if (oldOffsetMs != m_offsetMs) Q_EMIT offsetChanged();
    if (oldFingerprint != m_fingerprint) Q_EMIT canControlProviderChanged();
    advance();
}

void LyricSource::advance()
{
    qint64 position = m_positionUs;
    if (m_playbackStatus == QStringLiteral("Playing") && m_anchorMonotonicNs > 0) {
        position += static_cast<qint64>((m_clock() - m_anchorMonotonicNs) / 1000.0 * m_rate);
    }
    const qint64 positionMs = position / 1000;
    if (positionMs != m_currentPositionMs) {
        m_currentPositionMs = positionMs;
        Q_EMIT currentPositionChanged();
    }
    const int line = currentLineIndex(m_lines, positionMs, m_offsetMs);
    if (line != m_currentLine) {
        m_currentLine = line;
        Q_EMIT currentLineChanged();
    }
    rearmFrame();
}

void LyricSource::rearmFrame()
{
    m_frameTimer.stop();
    // A missing anchor or a non-positive rate means the position never moves on
    // its own; arming would only spin on an interval that changes nothing.
    if (m_playbackStatus != QStringLiteral("Playing") || m_anchorMonotonicNs <= 0 || m_rate <= 0.0) {
        return;
    }
    const auto boundary = nextBoundaryMs(m_lines, m_currentPositionMs, m_offsetMs);
    if (!boundary) {
        return;
    }
    const qint64 delayMs =
        static_cast<qint64>(std::ceil(static_cast<double>(*boundary - m_currentPositionMs) / m_rate));
    // The upper bound keeps a malformed timestamp such as [9999999:00.00] inside
    // the int the timer takes; a long interlude just wakes once a minute.
    m_frameTimer.start(static_cast<int>(std::clamp<qint64>(delayMs, 1, 60000)));
}

bool LyricSource::adjustOffset(int deltaMs)
{
    if (!canAdjustOffset()) {
        return false;
    }
    return sendControlCommand(QStringLiteral("AdjustOffset"),
                              {m_fingerprint, deltaMs});
}

bool LyricSource::resetOffset()
{
    if (!canAdjustOffset()) {
        return false;
    }
    return sendControlCommand(QStringLiteral("ResetOffset"), {m_fingerprint});
}

bool LyricSource::sendControlCommand(const QString &method, const QVariantList &arguments)
{
    if (!canControlProvider()) return false;
    m_controlInProgress = true;
    if (!m_controlError.isEmpty()) {
        m_controlError.clear();
        Q_EMIT controlErrorChanged();
    }
    Q_EMIT controlInProgressChanged();
    Q_EMIT canControlProviderChanged();
    Q_EMIT canAdjustOffsetChanged();

    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("io.github.swim233.PlasmaLyrics"),
        QStringLiteral("/io/github/swim233/PlasmaLyrics"),
        QStringLiteral("io.github.swim233.PlasmaLyrics.Control"), method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(message, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher](QDBusPendingCallWatcher *) {
        const QDBusPendingReply<QString> reply = *watcher;
        QString error;
        if (reply.isError()) {
            error = localizedControlError(reply.error().message());
        } else {
            error = localizedControlError(reply.value());
        }
        watcher->deleteLater();
        m_controlInProgress = false;
        Q_EMIT controlInProgressChanged();
        Q_EMIT canControlProviderChanged();
        Q_EMIT canAdjustOffsetChanged();
        if (!error.isEmpty()) {
            m_controlError = error;
            Q_EMIT controlErrorChanged();
            Q_EMIT controlFailed(error);
            QDBusMessage notification = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.Notifications"),
                QStringLiteral("/org/freedesktop/Notifications"),
                QStringLiteral("org.freedesktop.Notifications"),
                QStringLiteral("Notify"));
            notification.setArguments({QStringLiteral("Desktop Lyrics"), 0u,
                                       QStringLiteral("dialog-error"),
                                       i18nd("plasma_applet_io.github.swim233.plasma-lyrics",
                                             "Desktop Lyrics"), error,
                                       QStringList{}, QVariantMap{}, 5000});
            // A missing notification daemon must not start (or hold open) a
            // helper process just because a control command failed. On a
            // normal Plasma session the service is already registered; in a
            // minimal session this simply becomes a best-effort no-op.
            notification.setAutoStartService(false);
            QDBusConnection::sessionBus().call(notification, QDBus::NoBlock);
        }
    });
    return true;
}

bool LyricSource::setPreferredProvider(const QString &provider)
{
    if (!m_availableProviders.contains(provider)) return false;
    return sendControlCommand(QStringLiteral("SetPreferredProvider"),
                              {m_fingerprint, provider});
}

bool LyricSource::clearPreferredProvider()
{
    return sendControlCommand(QStringLiteral("ClearPreferredProvider"), {m_fingerprint});
}

bool LyricSource::research()
{
    return sendControlCommand(QStringLiteral("Research"), {m_fingerprint});
}
