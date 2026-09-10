#include "providers/logging.h"

namespace PlasmaLyrics {

// Unreferenced categories cost nothing, so splitting these definitions per
// provider would only add an #if in CMake and here without buying anything.
Q_LOGGING_CATEGORY(lcNetease, "plasmalyrics.provider.netease", QtInfoMsg)
Q_LOGGING_CATEGORY(lcAmll, "plasmalyrics.provider.amll", QtInfoMsg)
Q_LOGGING_CATEGORY(lcLocal, "plasmalyrics.provider.local", QtInfoMsg)

} // namespace PlasmaLyrics
