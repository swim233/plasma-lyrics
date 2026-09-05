import QtQuick

import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

import io.github.swim233.lyrics

PlasmoidItem {
    id: root

    readonly property bool onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar
    readonly property string activePlateMode: root.onDesktop
        ? Plasmoid.configuration.desktopPlateMode
        : Plasmoid.configuration.panelPlateMode

    readonly property bool activeAutoHide: root.onDesktop
        ? Plasmoid.configuration.desktopAutoHide
        : Plasmoid.configuration.panelAutoHide

    // Who draws the "ksvg" plate depends on whether auto-hide is on, because
    // the two things the plate has to do are mutually exclusive from inside an
    // applet.
    //
    // The shell's plate is a *sibling* of this applet's content item, so no
    // opacity we set can fade it -- which is what auto-hide needs on the
    // desktop. Drawing it ourselves fixes that. But the shell does more than
    // put a KSvg.FrameSvgItem on "widgets/background": when the theme ships
    // blurred-* elements (ChromeOS does; Breeze does not), it switches the
    // frame to prefix "blurred" and stacks a MultiEffect that samples the
    // wallpaper and blurs it behind the frame, masked by blurred-mask. That
    // effect reaches into the containment's own window and wallpaper, so an
    // applet cannot reproduce it -- self-drawing unconditionally turns a
    // blurred plate into a flat opaque slab on any theme that has one.
    //
    // So the shell keeps the plate whenever auto-hide is off, which is the
    // default and the only state where the plate never has to fade. Turning
    // auto-hide on trades the blur for a plate that can fade with the rest of
    // the widget. See DESIGN.md decision 40.
    //
    // ConfigurableBackground is left out on purpose: it adds the shell's own
    // show-background checkbox beside our three-way plate setting, and the two
    // would then disagree about the same thing.
    Plasmoid.backgroundHints: root.activePlateMode === "ksvg" && !root.plateSelfDrawn
        ? PlasmaCore.Types.DefaultBackground
        : PlasmaCore.Types.NoBackground
    // How long the desktop fade actually lasts, or 0 when there will be no
    // fade at all. Computed here rather than only inside LyricsView because
    // the plate handover below has to wait exactly as long as the fade does,
    // and two places disagreeing about whether an animation is running is
    // precisely what would leave the plate stranded in the wrong hands.
    readonly property int effectiveFadeMs:
        Plasmoid.configuration.desktopHideAnimationMs > 0 && Kirigami.Units.longDuration > 1
            ? Plasmoid.configuration.desktopHideAnimationMs
            : 0

    // The plate changes hands while auto-hide is on, because the shell draws a
    // strictly better one but ours is the only one that can fade. The shell
    // holds it whenever the widget is sitting fully visible; the applet takes
    // it for the duration of a fade and for as long as the widget is hidden.
    //
    // Both consumers -- Plasmoid.backgroundHints above and LyricsView's
    // `ownsPlate` below -- read this one property, so the handover is a single
    // binding pass rather than two that could disagree for a frame.
    property bool platePassedToShell: true
    readonly property bool plateSelfDrawn: root.activeAutoHide && !root.platePassedToShell

    Timer {
        id: plateHandbackTimer
        interval: root.effectiveFadeMs
        onTriggered: root.platePassedToShell = true
    }

    // Imperative rather than a binding: "hand back once the fade that just
    // started has finished" is a sequence, not a function of the current
    // state, and a binding cannot express the wait.
    function updatePlateOwner() {
        if (!root.activeAutoHide) {
            plateHandbackTimer.stop();
            root.platePassedToShell = true;
            return;
        }
        if (!visibilityPolicy.shouldBeVisible) {
            // Take the plate before the fade-out starts: the shell's does not
            // fade, so leaving it in place would fade the text out inside a
            // plate that stays put.
            plateHandbackTimer.stop();
            root.platePassedToShell = false;
            return;
        }
        if (!root.desktopAnimationsArmed || root.effectiveFadeMs <= 0) {
            // Nothing to wait for -- the landing transition out of
            // "undetermined" never animates, and a zero duration means the
            // widget simply appears.
            plateHandbackTimer.stop();
            root.platePassedToShell = true;
            return;
        }
        root.platePassedToShell = false;
        plateHandbackTimer.restart();
    }

    onActiveAutoHideChanged: root.updatePlateOwner()

    Connections {
        target: visibilityPolicy
        function onShouldBeVisibleChanged() { root.updatePlateOwner(); }
    }

    Plasmoid.title: i18n("Desktop Lyrics")
    preferredRepresentation: root.onDesktop ? fullRepresentation : compactRepresentation

    LyricSource {
        id: lyricSource
    }

    // DESIGN.md decision 40. One instance shared by both representations,
    // living at the PlasmoidItem root rather than inside either
    // representation's Loader: a representation swap (e.g. dragging the
    // widget from the desktop into a panel) destroys and recreates whatever
    // lives inside the Loader, which would silently reset a buffer timer
    // mid-countdown and make lyrics reappear for no reason.
    VisibilityPolicy {
        id: visibilityPolicy
        serviceAvailable: lyricSource.serviceAvailable
        stale: lyricSource.stale
        playbackStatus: lyricSource.playbackStatus
        trackTitle: lyricSource.trackTitle
        lyricState: lyricSource.lyricState
        determined: lyricSource.determined
        enabled: root.onDesktop ? Plasmoid.configuration.desktopAutoHide : Plasmoid.configuration.panelAutoHide
        hideNonMusic: root.onDesktop ? Plasmoid.configuration.desktopHideNonMusic : Plasmoid.configuration.panelHideNonMusic
        delayMs: (root.onDesktop ? Plasmoid.configuration.desktopHideDelaySec : Plasmoid.configuration.panelHideDelaySec) * 1000
    }

    // Gates the desktop fade Behavior (see LyricsView's `animationsArmed`):
    // true forever after the first determination, never reset. Deliberately
    // NOT a direct binding on lyricSource.determined -- see below.
    property bool desktopAnimationsArmed: false

    Connections {
        target: lyricSource
        function onDeterminedChanged() {
            if (lyricSource.determined) {
                // Qt.callLater defers this to a later event-loop turn on
                // purpose. lyricSource.determined flipping true is also what
                // drives visibilityPolicy.shouldBeVisible to its first real
                // value in this very turn (via the `determined:` binding
                // above), and Qt does not guarantee the Behavior's `enabled`
                // binding re-evaluates after the opacity target's does. Arm
                // it one turn later instead of racing that ordering, so the
                // landing transition is guaranteed instantaneous rather than
                // "usually instantaneous".
                Qt.callLater(() => { root.desktopAnimationsArmed = true; });
            }
        }
    }

    // Plasmoid.status has to be reasserted imperatively, not bound: opening
    // the compact representation's popup makes CompactApplet.qml stomp it to
    // RequiresAttentionStatus for as long as the popup is open, restoring
    // whatever it captured beforehand on close. A plain binding would lose
    // to that the moment anyone expands the applet. HiddenStatus is a no-op
    // on the desktop (Planar) form factor, so this only actually does
    // anything for the panel.
    //
    // No edit-mode/userConfiguring escape hatch here on purpose: it already
    // exists, but on the shell's side rather than ours. The panel's own
    // LayoutManager.js binds each applet container's `visible` to exactly
    // `applet.status !== HiddenStatus || (!plasmoid.immutable &&
    // plasmoid.userConfiguring) || plasmoid.corona.editMode` -- setting
    // HiddenStatus while the user is editing the panel is consumed by that
    // binding and never actually hides anything. Duplicating the condition
    // here would be redundant at best and, given it would have to spell it
    // "Plasmoid.containment.corona.editMode" from an applet's side rather
    // than the containment's own "Plasmoid.corona.editMode", an easy place
    // to introduce a typo the shell has already made unnecessary.
    function updateStatus() {
        Plasmoid.status = visibilityPolicy.shouldBeVisible
            ? PlasmaCore.Types.ActiveStatus
            : PlasmaCore.Types.HiddenStatus;
    }

    Component.onCompleted: {
        root.updatePlateOwner();
        if (!root.onDesktop) {
            root.updateStatus();
        }
    }

    Connections {
        target: visibilityPolicy
        function onShouldBeVisibleChanged() {
            // Both `expanded` writes below are gated on !onDesktop: the
            // popup-over-a-hidden-item problem they guard against is
            // structurally a panel-only concern. On the desktop,
            // preferredRepresentation is fullRepresentation (main.qml above),
            // so appletShouldBeExpanded() is always true and the compact
            // representation's popup/expander is never created in the first
            // place -- there is no popup path to reject or close, and
            // writing `expanded` here would be reaching for a door that does
            // not exist on this form factor.
            if (!root.onDesktop) {
                if (!visibilityPolicy.shouldBeVisible) {
                    // A popup anchored on a now-hidden compact item, showing
                    // an empty lyric area, would just be confusing -- close
                    // it rather than leave it stranded open.
                    root.expanded = false;
                }
                root.updateStatus();
            }
        }
    }

    onExpandedChanged: {
        if (!root.onDesktop) {
            if (root.expanded && !visibilityPolicy.shouldBeVisible) {
                // Reject: opening was triggered by the global shortcut or a
                // Space/Enter press on the compact item (CompactApplet.qml),
                // neither of which goes through a MouseArea we could disable.
                root.expanded = false;
                return;
            }
            root.updateStatus();
        }
    }

    compactRepresentation: LyricsView {
        source: lyricSource
        plateMode: Plasmoid.configuration.panelPlateMode
        ownsPlate: root.plateSelfDrawn
        solidColor: Plasmoid.configuration.panelSolidColor
        textColor: Plasmoid.configuration.panelTextColor
        strokeEnabled: Plasmoid.configuration.panelStroke
        strokeColor: Plasmoid.configuration.panelStrokeColor
        fontSize: Plasmoid.configuration.panelFontSize
        fontWeight: Plasmoid.configuration.panelFontWeight
        overflowMode: Plasmoid.configuration.panelOverflow
        animationMode: Plasmoid.configuration.panelAnimation
        showTranslation: Plasmoid.configuration.panelShowTranslation
        idleText: Plasmoid.configuration.idleTextUseDefault
            ? i18n("No media is playing") : Plasmoid.configuration.idleText
        notFoundText: Plasmoid.configuration.notFoundText
        panelMode: true
        showTrackInfo: Plasmoid.configuration.panelShowTrackInfo
        trackInfoLayout: Plasmoid.configuration.panelTrackInfoLayout
        trackInfoFontSize: Plasmoid.configuration.panelTrackInfoFontSize
        trackInfoFontWeight: Plasmoid.configuration.panelTrackInfoFontWeight
        trackInfoColor: Plasmoid.configuration.panelTrackInfoColor
        trackInfoStrokeEnabled: Plasmoid.configuration.panelTrackInfoStroke
        trackInfoStrokeColor: Plasmoid.configuration.panelTrackInfoStrokeColor
        trackInfoOverflow: Plasmoid.configuration.panelTrackInfoOverflow
    }

    fullRepresentation: LyricsView {
        source: lyricSource
        plateMode: Plasmoid.configuration.desktopPlateMode
        ownsPlate: root.plateSelfDrawn
        solidColor: Plasmoid.configuration.desktopSolidColor
        textColor: Plasmoid.configuration.desktopTextColor
        strokeEnabled: Plasmoid.configuration.desktopStroke
        strokeColor: Plasmoid.configuration.desktopStrokeColor
        fontSize: Plasmoid.configuration.desktopFontSize
        fontWeight: Plasmoid.configuration.desktopFontWeight
        overflowMode: Plasmoid.configuration.desktopOverflow
        animationMode: Plasmoid.configuration.desktopAnimation
        showTranslation: Plasmoid.configuration.desktopShowTranslation
        idleText: Plasmoid.configuration.idleTextUseDefault
            ? i18n("No media is playing") : Plasmoid.configuration.idleText
        notFoundText: Plasmoid.configuration.notFoundText
        panelMode: false
        showTrackInfo: Plasmoid.configuration.desktopShowTrackInfo
        trackInfoLayout: Plasmoid.configuration.desktopTrackInfoLayout
        trackInfoFontSize: Plasmoid.configuration.desktopTrackInfoFontSize
        trackInfoFontWeight: Plasmoid.configuration.desktopTrackInfoFontWeight
        trackInfoColor: Plasmoid.configuration.desktopTrackInfoColor
        trackInfoStrokeEnabled: Plasmoid.configuration.desktopTrackInfoStroke
        trackInfoStrokeColor: Plasmoid.configuration.desktopTrackInfoStrokeColor
        trackInfoOverflow: Plasmoid.configuration.desktopTrackInfoOverflow
        shouldBeVisible: visibilityPolicy.shouldBeVisible
        animationsArmed: root.desktopAnimationsArmed
        hideAnimationMs: root.effectiveFadeMs
    }

    toolTipMainText: lyricSource.trackTitle.length > 0 ? lyricSource.trackTitle : i18n("Desktop Lyrics")
    toolTipSubText: lyricSource.trackArtists

    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: i18n("Lyrics 0.5 s earlier")
            icon.name: "go-previous"
            enabled: lyricSource.canAdjustOffset
            onTriggered: lyricSource.adjustOffset(-500)
        },
        PlasmaCore.Action {
            text: i18n("Lyrics 0.5 s later")
            icon.name: "go-next"
            enabled: lyricSource.canAdjustOffset
            onTriggered: lyricSource.adjustOffset(500)
        },
        PlasmaCore.Action {
            text: i18n("Reset lyric offset (%1 ms)", lyricSource.offsetMs)
            icon.name: "edit-undo"
            enabled: lyricSource.canAdjustOffset && lyricSource.offsetMs !== 0
            onTriggered: lyricSource.resetOffset()
        }
    ]
}
