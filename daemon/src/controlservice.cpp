#include "controlservice.h"

#include "core/log/logformat.h"
#include "core/store/lyricstore.h"
#include "daemon/src/resolver.h"
#include "logging.h"

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
    const auto log = [&](const QString &result) {
        qCInfo(lcDaemon).noquote()
            << QStringLiteral("control SetPreferredProvider fingerprint=%1 provider=%2 result=%3")
                   .arg(quoted(expectedFingerprint), quoted(provider), result);
    };
    QString error;
    const auto state = checkedState(expectedFingerprint, &error);
    if (!state) {
        log(error);
        return error;
    }
    if (!m_resolver.availableProviders().contains(provider)) {
        log(QStringLiteral("provider-unavailable"));
        return QStringLiteral("provider-unavailable");
    }
    if (!m_store.setPreferredProvider(expectedFingerprint, provider)) {
        log(QStringLiteral("preference-save-failed"));
        return QStringLiteral("preference-save-failed");
    }
    m_resolveCurrent(*state, QStringLiteral("set-preferred"));
    log(QStringLiteral("ok"));
    return {};
}

QString ControlService::ClearPreferredProvider(const QString &expectedFingerprint)
{
    const auto log = [&](const QString &result) {
        qCInfo(lcDaemon).noquote()
            << QStringLiteral("control ClearPreferredProvider fingerprint=%1 result=%2")
                   .arg(quoted(expectedFingerprint), result);
    };
    QString error;
    const auto state = checkedState(expectedFingerprint, &error);
    if (!state) {
        log(error);
        return error;
    }
    if (!m_store.clearPreferredProvider(expectedFingerprint)) {
        log(QStringLiteral("preference-clear-failed"));
        return QStringLiteral("preference-clear-failed");
    }
    m_resolveCurrent(*state, QStringLiteral("clear-preferred"));
    log(QStringLiteral("ok"));
    return {};
}

QString ControlService::Research(const QString &expectedFingerprint)
{
    const auto log = [&](const QString &result) {
        qCInfo(lcDaemon).noquote()
            << QStringLiteral("control Research fingerprint=%1 result=%2")
                   .arg(quoted(expectedFingerprint), result);
    };
    QString error;
    const auto state = checkedState(expectedFingerprint, &error);
    if (!state) {
        log(error);
        return error;
    }
    m_resolveCurrent(*state, QStringLiteral("research"));
    log(QStringLiteral("ok"));
    return {};
}

QString ControlService::AdjustOffset(const QString &expectedFingerprint, int deltaMs)
{
    const auto log = [&](const QString &result) {
        qCInfo(lcDaemon).noquote()
            << QStringLiteral("control AdjustOffset fingerprint=%1 deltaMs=%2 result=%3")
                   .arg(quoted(expectedFingerprint), QString::number(deltaMs), result);
    };
    QString error;
    if (!checkedOffsetContext(expectedFingerprint, &error)) {
        log(error);
        return error;
    }
    if (m_store.globalOffsetEnabled()) {
        if (!m_store.adjustGlobalOffset(deltaMs)) {
            log(QStringLiteral("offset-save-failed"));
            return QStringLiteral("offset-save-failed");
        }
    } else {
        const auto ref = m_currentRef ? m_currentRef() : std::nullopt;
        if (!ref) {
            log(QStringLiteral("no-track-ref"));
            return QStringLiteral("no-track-ref");
        }
        if (!m_store.adjustOffset(*ref, deltaMs)) {
            log(QStringLiteral("offset-save-failed"));
            return QStringLiteral("offset-save-failed");
        }
    }
    if (m_publishCurrent) m_publishCurrent();
    log(QStringLiteral("ok"));
    return {};
}

QString ControlService::ResetOffset(const QString &expectedFingerprint)
{
    const auto log = [&](const QString &result) {
        qCInfo(lcDaemon).noquote()
            << QStringLiteral("control ResetOffset fingerprint=%1 result=%2")
                   .arg(quoted(expectedFingerprint), result);
    };
    QString error;
    if (!checkedOffsetContext(expectedFingerprint, &error)) {
        log(error);
        return error;
    }
    if (m_store.globalOffsetEnabled()) {
        if (!m_store.setGlobalOffsetMs(0)) {
            log(QStringLiteral("offset-save-failed"));
            return QStringLiteral("offset-save-failed");
        }
    } else {
        const auto ref = m_currentRef ? m_currentRef() : std::nullopt;
        if (!ref) {
            log(QStringLiteral("no-track-ref"));
            return QStringLiteral("no-track-ref");
        }
        if (!m_store.setOffset(*ref, 0)) {
            log(QStringLiteral("offset-save-failed"));
            return QStringLiteral("offset-save-failed");
        }
    }
    if (m_publishCurrent) m_publishCurrent();
    log(QStringLiteral("ok"));
    return {};
}

QString ControlService::RefreshGlobalOffset()
{
    // GlobalConfig deliberately writes SQLite itself so it can report a
    // synchronous storage failure to the configuration page. Once that write
    // succeeds, this best-effort notification makes the daemon republish the
    // newly effective value to every snapshot consumer.
    if (m_publishCurrent) m_publishCurrent();
    qCInfo(lcDaemon) << "control RefreshGlobalOffset result=ok";
    return {};
}

QStringList ControlService::AvailableProviders() const
{
    qCInfo(lcDaemon).noquote() << QStringLiteral("control AvailableProviders result=%1")
                                      .arg(m_supportedProviders.join(QLatin1Char(',')));
    return m_supportedProviders;
}

} // namespace PlasmaLyrics
