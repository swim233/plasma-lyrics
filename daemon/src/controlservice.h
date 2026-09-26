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
    // Whether a forced resolve may answer from the new chain head's cached
    // lyric (a provider switch) or must search again (Research).
    enum class CachePolicy { PreferCached, Bypass };
    using CurrentState = std::function<std::optional<MprisState>()>;
    using ResolveCurrent = std::function<void(const MprisState &, const QString &trigger,
                                              CachePolicy cache)>;
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
    // Both change only the current song's own offset, in either global
    // offset mode (DESIGN.md decision 79), and so need its lyric ref.
    QString AdjustOffset(const QString &expectedFingerprint, int deltaMs);
    QString ResetOffset(const QString &expectedFingerprint);
    // The configuration dialog's write of a song's own offset, an absolute
    // value. The ref itself names the song, so unlike the methods above it
    // takes no expected fingerprint and works for any song, current or not.
    QString SetOffsetForTrack(const QString &provider, const QString &trackId, int offsetMs);
    QString RefreshGlobalOffset();
    QStringList AvailableProviders() const;

    // These exist so a settings change made in the frontend's configuration
    // dialog also reaches the daemon's journal unit and log file, not just
    // plasmashell's (core/log/configlog.h renders the identical line on both
    // sides). The frontend sends them fire-and-forget with QDBus::NoBlock;
    // the daemon deliberately trusts the caller rather than re-deriving the
    // change from its own state (DESIGN.md decision 75 -- forging these
    // lines was judged not worth a D-Bus design for a purely diagnostic log).
    void NoteConfigChange(const QString &store, const QString &applet, const QString &form,
                          const QString &key, const QString &oldValue, const QString &newValue);
    void NoteSaveFailed(const QString &store, const QString &reason);
    void NoteRestartRequested();
    void NoteRestartFinished(bool success, const QString &error);

private:
    std::optional<MprisState> checkedState(const QString &expectedFingerprint,
                                           QString *error) const;
    std::optional<TrackRef> checkedOffsetRef(const QString &expectedFingerprint,
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
