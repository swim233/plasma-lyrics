#include "lyricsource.h"

#include "core/lyric/lyricmodel.h"
#include "core/lyric/timeline.h"
#include "core/store/lyricstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

} // namespace

LyricSource::LyricSource(QObject *parent)
    : LyricSource(&LyricSource::monotonicNowNs, parent)
{
}

LyricSource::LyricSource(std::function<qint64()> clock, QObject *parent)
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
bool LyricSource::canAdjustOffset() const { return !m_provider.isEmpty() && !m_trackId.isEmpty(); }

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
    }
}

void LyricSource::updateServiceHealth()
{
    if (m_pid > 0 && !processExists(m_pid)) {
        setUnavailable(true);
        return;
    }
    if (m_serviceAvailable && canAdjustOffset()) {
        LyricStore store;
        if (store.open()) {
            const int sharedOffset = store.offset({m_provider, m_trackId, 0});
            if (sharedOffset != m_offsetMs) {
                m_offsetMs = sharedOffset;
                Q_EMIT offsetChanged();
                advance();
            }
        }
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
    const int oldOffset = m_offsetMs;

    const auto track = root.value(QStringLiteral("track")).toObject();
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
        || oldProvider != m_provider || oldTrackId != m_trackId) Q_EMIT trackChanged();
    if (oldOffset != m_offsetMs) Q_EMIT offsetChanged();
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
    LyricStore store;
    if (!store.open()) {
        return false;
    }
    const auto adjusted = store.adjustOffset({m_provider, m_trackId, 0}, deltaMs);
    if (!adjusted) {
        return false;
    }
    m_offsetMs = *adjusted;
    Q_EMIT offsetChanged();
    advance();
    return true;
}

bool LyricSource::resetOffset()
{
    if (!canAdjustOffset()) {
        return false;
    }
    LyricStore store;
    if (!store.open() || !store.setOffset({m_provider, m_trackId, 0}, 0)) {
        return false;
    }
    m_offsetMs = 0;
    Q_EMIT offsetChanged();
    advance();
    return true;
}
