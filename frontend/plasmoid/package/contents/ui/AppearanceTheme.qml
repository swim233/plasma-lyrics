import QtQuick
import org.kde.kirigami as Kirigami

import "ThemePolicy.js" as ThemePolicy

// DESIGN.md decision 76: which of its two appearance sets one form factor
// renders with, and whether colours should animate into it right now.
// A component of its own rather than a few properties on main.qml, which is
// a PlasmoidItem the QML test suite cannot instantiate.
QtObject {
    id: theme

    // Plasmoid.configuration, or any object with the same keys.
    required property var configuration
    // "desktop" or "panel": the key prefix, not Plasmoid.formFactor.
    required property string formFactor
    // Whether the Plasma style -- what draws the "ksvg" plate and the panel
    // -- is a dark one. main.qml derives it from Kirigami.Theme on an Item of
    // the widget, where Kirigami reports the style's colours; this QtObject
    // has no Kirigami.Theme of its own to read.
    required property bool styleDark
    // Kirigami scales its durations by the Plasma animation speed and makes
    // them 0 or 1 ms when animations are off -- the same test main.qml's
    // effectiveFadeMs applies to longDuration.
    property int transitionMs: Kirigami.Units.veryLongDuration

    readonly property bool wantDark: ThemePolicy.isDark(theme.styleDark,
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

    // False from holdStill() until the next turn of the event loop; until
    // then `dark` follows wantDark without a transition.
    property bool settled: false
    property Timer settleTimer: Timer {
        interval: theme.transitionMs
        onTriggered: theme.transitioning = false
    }

    onWantDarkChanged: {
        if (!theme.settled) {
            theme.dark = theme.wantDark;
            return;
        }
        if (theme.wantDark === theme.dark) {
            return;
        }
        if (theme.transitionMs > 1) {
            theme.transitioning = true;
            theme.settleTimer.restart();
        }
        theme.dark = theme.wantDark;
    }

    // styleDark is only right on an item that has a parent: the widget's
    // Kirigami.Theme has no style to inherit before that, and plasmoidviewer
    // reports #000000 -- dark, whatever the style -- until Plasma parents
    // the root item, with the style's colour arriving in the same turn of
    // the event loop (in a panel, a millisecond after the parent itself).
    // So main.qml calls this whenever the root item's parent changes, and
    // whatever styleDark does for the rest of that turn is it settling, not
    // a switch. It also runs on creation: plasmashell starting up is not a
    // switch either.
    function holdStill() {
        theme.settled = false;
        theme.dark = theme.wantDark;
        Qt.callLater(theme.settle);
    }
    function settle() {
        theme.dark = theme.wantDark;
        theme.settled = true;
    }

    Component.onCompleted: theme.holdStill()
}
