import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3

Item {
    id: root

    required property var source
    property string plateMode: "ksvg"
    property color solidColor: "#99000000"
    property color textColor: "#fffaf5"
    property bool strokeEnabled: false
    property color strokeColor: "#cc000000"
    property int fontSize: 34
    property int fontWeight: Font.Normal
    property string overflowMode: "fit"
    property string animationMode: "slide"
    property bool showTranslation: true
    property string idleText: i18n("No media is playing")
    property string notFoundText: ""
    property bool panelMode: false

    property bool showTrackInfo: true
    property string trackInfoLayout: "single"
    property int trackInfoFontSize: 19
    property int trackInfoFontWeight: Font.Normal
    property color trackInfoColor: "#b3fffaf5"
    property bool trackInfoStrokeEnabled: false
    property color trackInfoStrokeColor: "#cc000000"
    property string trackInfoOverflow: "fit"

    readonly property string effectiveText: {
        if (!source.serviceAvailable || source.stale) return "";
        if (source.lyricState === "searching") return i18n("Searching for lyrics…");
        if (source.lyricState === "not-found") return root.notFoundText;
        if (source.lyricState === "no-lyric") return "";
        if (source.playbackStatus === "Stopped" || source.trackTitle.length === 0) return root.idleText;
        if (source.lyricState === "filtered") return "";
        return source.currentText;
    }
    readonly property string effectiveTranslation: root.showTranslation && source.lyricState === "ok"
        ? source.currentTranslation : ""
    // TrackInfo already collapses to zero height on an empty title (see
    // TrackInfo.qml), so gating the config off just means feeding it an
    // empty title too -- no separate visibility flag to keep in sync. It
    // stays up through searching/not-found/filtered/no-lyric on purpose:
    // those are exactly the states where the lyric area is otherwise blank.
    readonly property string trackInfoTitle: root.showTrackInfo ? root.source.trackTitle : ""

    implicitWidth: panelMode ? Kirigami.Units.gridUnit * 14 : Kirigami.Units.gridUnit * 28
    implicitHeight: panelMode ? Kirigami.Units.gridUnit * 2 : Kirigami.Units.gridUnit * 7.5
    Layout.minimumWidth: panelMode ? Kirigami.Units.gridUnit * 8 : Kirigami.Units.gridUnit * 18
    Layout.minimumHeight: trackInfo.implicitHeight + Kirigami.Units.gridUnit * 2

    Rectangle {
        anchors.fill: parent
        visible: root.plateMode === "solid"
        color: root.solidColor
        radius: Kirigami.Units.cornerRadius
    }

    Loader {
        anchors.fill: parent
        active: !root.source.serviceAvailable || root.source.stale
        sourceComponent: PlasmaComponents3.Label {
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            color: root.textColor
            text: root.source.stale
                ? i18n("The lyrics service stopped. Restart it with:\nsystemctl --user restart plasma-lyricsd")
                : i18n("The lyrics service is not running. Start it with:\nsystemctl --user enable --now plasma-lyricsd")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Math.max(Kirigami.Units.smallSpacing, root.fontSize * 0.35)
        visible: root.source.serviceAvailable && !root.source.stale
        spacing: 0

        TrackInfo {
            id: trackInfo
            Layout.fillWidth: true
            title: root.trackInfoTitle
            artists: root.source.trackArtists
            layoutMode: root.trackInfoLayout
            textColor: root.trackInfoColor
            strokeEnabled: root.trackInfoStrokeEnabled
            strokeColor: root.trackInfoStrokeColor
            fontSize: root.trackInfoFontSize
            fontWeight: root.trackInfoFontWeight
            overflowMode: root.trackInfoOverflow
        }

        AnimatedLyric {
            Layout.fillWidth: true
            Layout.fillHeight: true
            lyricText: root.effectiveText
            translationText: root.effectiveTranslation
            textColor: root.textColor
            strokeEnabled: root.strokeEnabled
            strokeColor: root.strokeColor
            fontSize: root.fontSize
            fontWeight: root.fontWeight
            overflowMode: root.overflowMode
            animationMode: root.animationMode
        }
    }
}
