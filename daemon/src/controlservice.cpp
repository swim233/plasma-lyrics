#include "controlservice.h"

#include "core/store/lyricstore.h"
#include "daemon/src/resolver.h"

namespace PlasmaLyrics {

ControlService::ControlService(LyricStore &store, Resolver &resolver,
                               CurrentState currentState, ResolveCurrent resolveCurrent,
                               CurrentRef currentRef, PublishCurrent publishCurrent,
                               QStringList supportedProviders, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_resolver(resolver)
    , m_currentState(std::move(currentState))
    , m_resolveCurrent(std::move(resolveCurrent))
    , m_currentRef(std::move(currentRef))
    , m_publishCurrent(std::move(publishCurrent))
    , m_supportedProviders(std::move(supportedProviders))
{
    if (m_supportedProviders.isEmpty()) {
        m_supportedProviders = m_resolver.availableProviders();
    }
}

QString ControlService::serviceName() { return QStringLiteral("io.github.swim233.PlasmaLyrics"); }
QString ControlService::objectPath() { return QStringLiteral("/io/github/swim233/PlasmaLyrics"); }
QString ControlService::interfaceName() { return QStringLiteral("io.github.swim233.PlasmaLyrics.Control"); }

std::optional<MprisState> ControlService::checkedState(const QString &expectedFingerprint,
                                                       QString *error) const
{
    const auto state = m_currentState ? m_currentState() : std::nullopt;
    if (!state || state->fingerprint.isEmpty()) {
        *error = QStringLiteral("no-current-song");
        return std::nullopt;
    }
    if (expectedFingerprint.isEmpty() || state->fingerprint != expectedFingerprint) {
        *error = QStringLiteral("song-changed");
        return std::nullopt;
    }
    return state;
}

bool ControlService::checkedOffsetContext(const QString &expectedFingerprint,
                                          QString *error) const
{
    const auto state = m_currentState ? m_currentState() : std::nullopt;
    if (!state || state->fingerprint.isEmpty()) {
        if (m_store.globalOffsetEnabled() && expectedFingerprint.isEmpty()) {
            return true;
        }
        *error = QStringLiteral("no-current-song");
        return false;
    }
    if (expectedFingerprint.isEmpty() || state->fingerprint != expectedFingerprint) {
        *error = QStringLiteral("song-changed");
        return false;
    }
    return true;
}

QString ControlService::SetPreferredProvider(const QString &expectedFingerprint,
                                             const QString &provider)
{
    QString error;
    const auto state = checkedState(expectedFingerprint, &error);
    if (!state) return error;
    if (!m_resolver.availableProviders().contains(provider)) {
        return QStringLiteral("provider-unavailable");
    }
    if (!m_store.setPreferredProvider(expectedFingerprint, provider)) {
        return QStringLiteral("preference-save-failed");
    }
    m_resolveCurrent(*state);
    return {};
}

QString ControlService::ClearPreferredProvider(const QString &expectedFingerprint)
{
    QString error;
    const auto state = checkedState(expectedFingerprint, &error);
    if (!state) return error;
    if (!m_store.clearPreferredProvider(expectedFingerprint)) {
        return QStringLiteral("preference-clear-failed");
    }
    m_resolveCurrent(*state);
    return {};
}

QString ControlService::Research(const QString &expectedFingerprint)
{
    QString error;
    const auto state = checkedState(expectedFingerprint, &error);
    if (!state) return error;
    m_resolveCurrent(*state);
    return {};
}

QString ControlService::AdjustOffset(const QString &expectedFingerprint, int deltaMs)
{
    QString error;
    if (!checkedOffsetContext(expectedFingerprint, &error)) return error;
    if (m_store.globalOffsetEnabled()) {
        if (!m_store.adjustGlobalOffset(deltaMs)) {
            return QStringLiteral("offset-save-failed");
        }
    } else {
        const auto ref = m_currentRef ? m_currentRef() : std::nullopt;
        if (!ref) return QStringLiteral("no-track-ref");
        if (!m_store.adjustOffset(*ref, deltaMs)) {
            return QStringLiteral("offset-save-failed");
        }
    }
    if (m_publishCurrent) m_publishCurrent();
    return {};
}

QString ControlService::ResetOffset(const QString &expectedFingerprint)
{
    QString error;
    if (!checkedOffsetContext(expectedFingerprint, &error)) return error;
    if (m_store.globalOffsetEnabled()) {
        if (!m_store.setGlobalOffsetMs(0)) {
            return QStringLiteral("offset-save-failed");
        }
    } else {
        const auto ref = m_currentRef ? m_currentRef() : std::nullopt;
        if (!ref) return QStringLiteral("no-track-ref");
        if (!m_store.setOffset(*ref, 0)) {
            return QStringLiteral("offset-save-failed");
        }
    }
    if (m_publishCurrent) m_publishCurrent();
    return {};
}

QString ControlService::RefreshGlobalOffset()
{
    // GlobalConfig deliberately writes SQLite itself so it can report a
    // synchronous storage failure to the configuration page. Once that write
    // succeeds, this best-effort notification makes the daemon republish the
    // newly effective value to every snapshot consumer.
    if (m_publishCurrent) m_publishCurrent();
    return {};
}

QStringList ControlService::AvailableProviders() const
{
    return m_supportedProviders;
}

} // namespace PlasmaLyrics
