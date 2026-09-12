#pragma once

#include "daemon/src/mpris/mprispolicy.h"

#include <QSettings>
#include <QString>
#include <QStringList>

namespace PlasmaLyrics {

class Config
{
public:
    Config();

    // One-time, idempotent cleanup for a leftover `@Invalid()` literal
    // (see core/config/stringlistsetting.h) on players/blacklist,
    // filter/musicUrlPrefixes and filter/platforms. The target state is
    // not the same for all three, and not derived from one uniform
    // argument (DESIGN.md decision 67 -- not a severity comparison across
    // the three keys either, an earlier version tried that and got the
    // ranking backwards): players/blacklist migrates to "unset" on
    // case-specific evidence (a real journal capture plus the user's own
    // request) that covers only this one key. filter/musicUrlPrefixes also
    // migrates to "unset", but the choice is unobservable rather than
    // evidence-backed -- the built-in default here is byte-identical to
    // platformRules()'s netease urlPrefixes, and platformFor() decides
    // before the musicUrlPrefixes loop is ever reached, so both migration
    // targets produce identical results; it migrates the same way as
    // players/blacklist because Q24 named the two together, not on its
    // own argument. filter/platforms migrates to "explicitly empty"
    // instead, on origin rather than severity: `@Invalid()` here can only
    // come from a user actively unchecking both platform checkboxes, so
    // it is real state, not a defect artifact -- severity in fact points
    // the other way (an unchecked platform is rejected unconditionally,
    // more severe than musicUrlPrefixes' non-difference above). Call once
    // at startup, before policy(). Safe to call more than once; a second
    // call changes nothing.
    void migrateLegacySettings();

    // The hard-coded fallback provider order used when providers/order is
    // absent or empty, and (via enabledProviderOrder()'s own fallback)
    // when providers/enabled selects nothing from it. Exposed so tests can
    // assert against the same constant Config itself uses rather than
    // duplicating it as a literal list -- a literal silently goes stale
    // the next time a provider is added to this list.
    static const QStringList &builtInProviderOrder();

    PolicyConfig policy() const;
    QUrl neteaseBaseUrl() const;
    int networkTimeoutMs() const;
    // The persisted priority list is independent from the enabled subset so
    // disabling a provider never erases its position from configuration UI.
    QStringList providerOrder() const;
    QStringList enabledProviderOrder() const;
    QString localLyricsDirectory() const;
    QUrl amllIndexUrl() const;
    QUrl amllContentBaseUrl() const;
    int amllTimeoutMs() const;
    qint64 amllIndexMaxAgeSeconds() const;
    bool fileLoggingEnabled() const;
    QString logFilePath() const;
    bool debugLoggingEnabled() const;
    bool filterCredits() const;
    // "none", "system" or "manual"; an unknown stored value reads back as
    // "none" (a qCWarning is logged when that happens).
    QString proxyMode() const;
    QString proxyUrl() const;

private:
    mutable QSettings m_settings;
};

} // namespace PlasmaLyrics
