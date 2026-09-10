#include "logging.h"

namespace PlasmaLyrics {

// Default severity is info for every category; debug output only appears
// once Config::debugLoggingEnabled() installs the "plasmalyrics.*.debug=true"
// filter rule (or QT_LOGGING_RULES overrides it).
Q_LOGGING_CATEGORY(lcDaemon, "plasmalyrics.daemon", QtInfoMsg)
Q_LOGGING_CATEGORY(lcResolver, "plasmalyrics.resolver", QtInfoMsg)
Q_LOGGING_CATEGORY(lcMpris, "plasmalyrics.mpris", QtInfoMsg)

} // namespace PlasmaLyrics
