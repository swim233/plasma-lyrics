#pragma once

#include "core/store/lyricstore.h"
#include "daemon/src/mpris/mpristypes.h"

#include <QObject>
#include <QStringList>
#include <functional>
#include <optional>

namespace PlasmaLyrics {

class Resolver;

class ControlService final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.swim233.PlasmaLyrics.Control")

public:
    using CurrentState = std::function<std::optional<MprisState>()>;
    using ResolveCurrent = std::function<void(const MprisState &)>;
    using CurrentRef = std::function<std::optional<TrackRef>()>;
    using PublishCurrent = std::function<void()>;

    ControlService(LyricStore &store, Resolver &resolver, CurrentState currentState,
                   ResolveCurrent resolveCurrent, CurrentRef currentRef = {},
                   PublishCurrent publishCurrent = {}, QStringList supportedProviders = {},
                   QObject *parent = nullptr);

    static QString serviceName();
    static QString objectPath();
    static QString interfaceName();

public Q_SLOTS:
    // An empty return value means success; a non-empty value is a stable
    // error code localized by the frontend.  The expected fingerprint is mandatory so a
    // context menu opened for song A can never mutate song B after a change.
    QString SetPreferredProvider(const QString &expectedFingerprint,
                                 const QString &provider);
    QString ClearPreferredProvider(const QString &expectedFingerprint);
    QString Research(const QString &expectedFingerprint);
    QString AdjustOffset(const QString &expectedFingerprint, int deltaMs);
    QString ResetOffset(const QString &expectedFingerprint);
    QString RefreshGlobalOffset();
    QStringList AvailableProviders() const;

private:
    std::optional<MprisState> checkedState(const QString &expectedFingerprint,
                                           QString *error) const;

    LyricStore &m_store;
    Resolver &m_resolver;
    CurrentState m_currentState;
    ResolveCurrent m_resolveCurrent;
    CurrentRef m_currentRef;
    PublishCurrent m_publishCurrent;
    QStringList m_supportedProviders;
};

} // namespace PlasmaLyrics
