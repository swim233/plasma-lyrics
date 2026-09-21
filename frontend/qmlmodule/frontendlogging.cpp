#include "frontendlogging.h"

namespace PlasmaLyrics {

// Settings changes made through the configuration dialog (DESIGN.md
// decision 75). This code runs inside plasmashell, so these lines land in
// plasmashell's journal -- `journalctl --user QT_CATEGORY=plasmalyrics.config`
// -- and the daemon repeats each one under plasmalyrics.daemon. Default
// severity is info like the daemon's categories; the frontend has no debug
// output, so logging/debug does not concern this category.
Q_LOGGING_CATEGORY(lcConfig, "plasmalyrics.config", QtInfoMsg)

} // namespace PlasmaLyrics
