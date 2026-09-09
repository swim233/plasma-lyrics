#include "controlservice.h"

#include "core/store/lyricstore.h"
#include "daemon/src/resolver.h"

namespace PlasmaLyrics {

ControlService::ControlService(LyricStore &store, Resolver &resolver,
                               CurrentState currentState, ResolveCurrent resolveCurrent,
                               QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_resolver(resolver)
    , m_currentState(std::move(currentState))
    , m_resolveCurrent(std::move(resolveCurrent))
{
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

} // namespace PlasmaLyrics
