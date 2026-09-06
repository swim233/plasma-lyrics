#include "globalconfig.h"

#include "core/store/lyricstore.h"

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
        return false;
    }
    if (!store.setGlobalOffsetEnabled(m_enabled) || !store.setGlobalOffsetMs(m_offsetMs)) {
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
    Q_EMIT saved();
    return true;
}
