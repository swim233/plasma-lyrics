#pragma once

#include "core/log/configlog.h"

#include <QString>
#include <QVariant>

namespace PlasmaLyrics {

// How a stored settings value is spelled inside a `config changed` line
// before quoted() wraps it: bool -> true/false, integers -> decimal,
// QString verbatim, QStringList joined with ",", anything else through
// QVariant::toString(). The frontend renders because only it knows the
// value's type; the daemon receives the rendered text and quotes it.
QString renderConfigValue(const QVariant &value);

// network/proxyUrl may carry credentials: scheme://host:port when the
// address parses, ProxySpec::redactedForLog() otherwise, and only the
// length when nothing can be shown safely. Empty stays empty.
QString renderProxyUrlForLog(const QString &raw);

// Each report* logs the line locally under plasmalyrics.config and hands
// the same fields to the daemon's matching Note* Control method with
// QDBus::NoBlock -- fire and forget, like GlobalConfig's RefreshGlobalOffset
// call: no reply, no error handling, never blocks plasmashell. A daemon
// that is not running, or one that predates these methods, simply has no
// copy of the line; the frontend's own copy is unaffected.
void reportConfigChange(const ConfigChange &change);
void reportSaveFailed(const QString &store, const QString &reason);
void reportRestartRequested();
void reportRestartFinished(bool success, const QString &error);

} // namespace PlasmaLyrics
