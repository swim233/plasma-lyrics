import QtQuick

import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

import io.github.swim233.lyrics

PlasmoidItem {
    id: root

    readonly property bool onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar

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
        overflowMode: Plasmoid.configuration.panelOverflow
        animationMode: Plasmoid.configuration.panelAnimation
        showTranslation: Plasmoid.configuration.panelShowTranslation
        idleText: Plasmoid.configuration.idleTextUseDefault
            ? i18n("No media is playing") : Plasmoid.configuration.idleText
        notFoundText: Plasmoid.configuration.notFoundText
        panelMode: true
    }

    fullRepresentation: LyricsView {
        source: lyricSource
        plateMode: Plasmoid.configuration.desktopPlateMode
        solidColor: Plasmoid.configuration.desktopSolidColor
        textColor: Plasmoid.configuration.desktopTextColor
        strokeEnabled: Plasmoid.configuration.desktopStroke
        strokeColor: Plasmoid.configuration.desktopStrokeColor
        fontSize: Plasmoid.configuration.desktopFontSize
        overflowMode: Plasmoid.configuration.desktopOverflow
        animationMode: Plasmoid.configuration.desktopAnimation
        showTranslation: Plasmoid.configuration.desktopShowTranslation
        idleText: Plasmoid.configuration.idleTextUseDefault
            ? i18n("No media is playing") : Plasmoid.configuration.idleText
        notFoundText: Plasmoid.configuration.notFoundText
        panelMode: false
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
