import QtQuick

import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

import io.github.swim233.lyrics

PlasmoidItem {
    id: root

    readonly property bool onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar

    // The themed "ksvg" plate used to be the shell's to draw (backgroundHints:
    // DefaultBackground), on the theory that a hand-drawn FrameSvgItem would
    // be a one-shot snapshot that would not track a later theme or colour
    // scheme change. DESIGN.md decision 40 disproves that theory outright --
    // the shell's own plate is nothing but a KSvg.FrameSvgItem on
    // "widgets/background", the same one LyricsView now draws for itself --
    // but keeps the shell out of it unconditionally anyway, for a structural
    // reason rather than a cosmetic one: the shell's plate is a *sibling* of
    // this applet's content item, so no opacity we set on our side can ever
    // fade it, which auto-hide needs to do on the desktop.
    //
    // ConfigurableBackground is left out on purpose: it adds the shell's own
    // show-background checkbox beside our three-way plate setting, and the two
    // would then disagree about the same thing.
    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground
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
        hideAnimationMs: Plasmoid.configuration.desktopHideAnimationMs
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
