#include "globalconfig.h"

#include "core/store/lyricstore.h"
#include "settingslog.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <algorithm>

using namespace PlasmaLyrics;

GlobalConfig::GlobalConfig(QObject *parent)
    : QObject(parent)
{
    load();
}

bool GlobalConfig::enabled() const { return m_enabled; }
int GlobalConfig::offsetMs() const { return m_offsetMs; }
int GlobalConfig::maximumOffsetMs() const { return LyricStore::maximumGlobalOffsetMs(); }
bool GlobalConfig::unsavedChanges() const { return m_unsavedChanges; }

void GlobalConfig::markUnsaved()
{
    if (!m_unsavedChanges) {
        m_unsavedChanges = true;
        Q_EMIT unsavedChangesChanged();
    }
    Q_EMIT changed();
}

void GlobalConfig::setEnabled(bool value)
{
    if (m_enabled == value) {
        return;
    }
    m_enabled = value;
    markUnsaved();
}

void GlobalConfig::setOffsetMs(int value)
{
    const int clamped = std::clamp(value, -maximumOffsetMs(), maximumOffsetMs());
    if (m_offsetMs == clamped) {
        return;
    }
    m_offsetMs = clamped;
    markUnsaved();
}

void GlobalConfig::load()
{
    LyricStore store;
    if (store.open()) {
        m_enabled = store.globalOffsetEnabled();
        m_offsetMs = store.globalOffsetMs();
    }
    const bool wasUnsaved = m_unsavedChanges;
    m_unsavedChanges = false;
    Q_EMIT changed();
    if (wasUnsaved) {
        Q_EMIT unsavedChangesChanged();
    }
}

bool GlobalConfig::save()
{
    LyricStore store;
    if (!store.open()) {
        PlasmaLyrics::reportSaveFailed(QStringLiteral("db"), QStringLiteral("db-open-failed"));
        return false;
    }
    // Read before writing: these are the "old" values a config-changed line
    // reports below, not what load() last saw.
    const bool oldEnabled = store.globalOffsetEnabled();
    const int oldOffsetMs = store.globalOffsetMs();
    if (!store.setGlobalOffsetEnabled(m_enabled) || !store.setGlobalOffsetMs(m_offsetMs)) {
        PlasmaLyrics::reportSaveFailed(QStringLiteral("db"), QStringLiteral("db-write-failed"));
        return false;
    }
    // Reflects the storage layer's own clamping rather than assuming it
    // matches what was just sent -- setOffsetMs() above already clamps, so
    // this is a no-op in practice, but it keeps the two invariants (QML-side
    // and storage-side) from silently drifting apart if either changes.
    m_offsetMs = store.globalOffsetMs();
    if (m_unsavedChanges) {
        m_unsavedChanges = false;
        Q_EMIT unsavedChangesChanged();
    }
    if (oldEnabled != m_enabled) {
        PlasmaLyrics::reportConfigChange({.store = QStringLiteral("db"),
                                          .applet = QString(),
                                          .form = QString(),
                                          .key = QStringLiteral("globalOffsetEnabled"),
                                          .oldValue = PlasmaLyrics::renderConfigValue(oldEnabled),
                                          .newValue = PlasmaLyrics::renderConfigValue(m_enabled)});
    }
    if (oldOffsetMs != m_offsetMs) {
        PlasmaLyrics::reportConfigChange({.store = QStringLiteral("db"),
                                          .applet = QString(),
                                          .form = QString(),
                                          .key = QStringLiteral("globalOffsetMs"),
                                          .oldValue = PlasmaLyrics::renderConfigValue(oldOffsetMs),
                                          .newValue = PlasmaLyrics::renderConfigValue(m_offsetMs)});
    }
    // The change notes above must reach the daemon before this call, so a
    // listener sees "what changed" before "the effective value changed".
    const auto refresh = QDBusMessage::createMethodCall(
        QStringLiteral("io.github.swim233.PlasmaLyrics"),
        QStringLiteral("/io/github/swim233/PlasmaLyrics"),
        QStringLiteral("io.github.swim233.PlasmaLyrics.Control"),
        QStringLiteral("RefreshGlobalOffset"));
    QDBusConnection::sessionBus().call(refresh, QDBus::NoBlock);
    Q_EMIT saved();
    return true;
}
