#pragma once

#include "logformat.h"

#include <QString>

namespace PlasmaLyrics {

// One settings change as the frontend reports it (DESIGN.md decision 75).
// store is "ini" (the daemon's QSettings INI), "db" (LyricStore's setting
// table) or "applet" (one plasmoid instance's kcfg keys); applet and form
// identify the instance for "applet" and are empty otherwise. key is the
// storage key verbatim. oldValue/newValue are the frontend's rendering of
// the stored value (frontend/qmlmodule/settingslog.h) and are always
// quoted here, numbers and booleans included, so the two fields parse the
// same way whatever the key's type is.
struct ConfigChange
{
    QString store;
    QString applet;
    QString form;
    QString key;
    QString oldValue;
    QString newValue;
};

// These render the exact line both the frontend (plasmalyrics.config) and
// the daemon (plasmalyrics.daemon, via the Note* Control methods) log, so
// the two copies of every event are identical by construction. Lives in
// core/ because it depends on QString only and both sides include it.
inline QString configChangedLine(const ConfigChange &change)
{
    QString line = QStringLiteral("config changed store=%1").arg(change.store);
    if (!change.applet.isEmpty()) {
        line += QStringLiteral(" applet=%1 form=%2").arg(change.applet, change.form);
    }
    line += QStringLiteral(" key=%1 old=%2 new=%3")
                .arg(change.key, quoted(change.oldValue), quoted(change.newValue));
    return line;
}

inline QString configSaveFailedLine(const QString &store, const QString &reason)
{
    return QStringLiteral("config save failed store=%1 reason=%2").arg(store, quoted(reason));
}

inline QString restartRequestedLine()
{
    return QStringLiteral("config restart requested");
}

inline QString restartFinishedLine(bool success, const QString &error)
{
    if (success) {
        return QStringLiteral("config restart finished result=ok");
    }
    return QStringLiteral("config restart finished result=failed error=%1").arg(quoted(error));
}

} // namespace PlasmaLyrics
