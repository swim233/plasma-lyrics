import QtQuick
import org.kde.kirigami as Kirigami

import "ThemePolicy.js" as ThemePolicy

// DESIGN.md decision 75: which of its two appearance sets one form factor
// renders with, and whether colours should animate into it right now.
// A component of its own rather than a few properties on main.qml, which is
// a PlasmoidItem the QML test suite cannot instantiate.
QtObject {
    id: theme

    // Plasmoid.configuration, or any object with the same keys.
    required property var configuration
    // "desktop" or "panel": the key prefix, not Plasmoid.formFactor.
    required property string formFactor
    property int colorScheme: Application.styleHints.colorScheme
    // Kirigami scales its durations by the Plasma animation speed and makes
    // them 0 or 1 ms when animations are off -- the same test main.qml's
    // effectiveFadeMs applies to longDuration.
    property int transitionMs: Kirigami.Units.veryLongDuration

    readonly property bool wantDark: ThemePolicy.isDark(theme.colorScheme,
        theme.configuration[ThemePolicy.modeKey(theme.formFactor)])
    // Follows wantDark by assignment, never by a binding: a switch has to set
    // `transitioning` first, so that every colour depending on `dark` sees it
    // already true when it re-evaluates. As a binding, `dark` could update
    // before or after the Behaviors' `enabled` did.
    property bool dark: false
    // True for transitionMs after `dark` changes, and never while it holds
    // still: editing a colour of the set in effect applies at once, as it
    // did before there were two sets.
    property bool transitioning: false
    readonly property string prefix: ThemePolicy.keyPrefix(theme.formFactor, theme.dark)

    // The key of the set in effect with this suffix, e.g. value("TextColor")
    // reads desktopTextColor or desktopLightTextColor.
    function value(suffix) {
        return theme.configuration[theme.prefix + suffix];
    }

    property bool completed: false
    property Timer settleTimer: Timer {
        interval: theme.transitionMs
        onTriggered: theme.transitioning = false
    }

    onWantDarkChanged: {
        if (!theme.completed || theme.wantDark === theme.dark) {
            return;
        }
        if (theme.transitionMs > 1) {
            theme.transitioning = true;
            theme.settleTimer.restart();
        }
        theme.dark = theme.wantDark;
    }

    // The set in effect at startup is simply there: plasmashell starting up
    // is not a switch.
    Component.onCompleted: {
        theme.dark = theme.wantDark;
        theme.completed = true;
    }
}
