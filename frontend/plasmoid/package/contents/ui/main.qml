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
