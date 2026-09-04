import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: root

    property string lineText: ""
    property color textColor: "white"
    property bool strokeEnabled: false
    property color strokeColor: "black"
    property int fontSize: 34
    property string overflowMode: "fit"

    readonly property real lineHeight: Math.ceil(fontSize * 1.25)
    implicitHeight: overflowMode === "wrap" ? Math.min(mainText.implicitHeight, lineHeight * 2) : lineHeight
    height: implicitHeight
    clip: true

    readonly property var directions: [
        [-1, -1], [0, -1], [1, -1], [-1, 0],
        [1, 0], [-1, 1], [0, 1], [1, 1]
    ]

    Repeater {
        model: root.strokeEnabled ? root.directions : []
        delegate: Text {
            required property var modelData
            x: mainText.x + modelData[0]
            y: mainText.y + modelData[1]
            width: mainText.width
            height: mainText.height
            text: root.lineText
            color: root.strokeColor
            font: mainText.font
            fontSizeMode: mainText.fontSizeMode
            minimumPixelSize: mainText.minimumPixelSize
            wrapMode: mainText.wrapMode
            maximumLineCount: mainText.maximumLineCount
            elide: mainText.elide
            horizontalAlignment: mainText.horizontalAlignment
            verticalAlignment: mainText.verticalAlignment
        }
    }

    Text {
        id: mainText
        x: 0
        y: 0
        width: root.overflowMode === "marquee" ? implicitWidth : root.width
        height: root.height
        text: root.lineText
        color: root.textColor
        // The family follows the Plasma font setting; size and colour stay with
        // the widget's own configuration, because lyrics sit on the wallpaper
        // where a theme colour is not guaranteed to be readable -- DESIGN.md
        // decision 30.
        font.family: Kirigami.Theme.defaultFont.family
        font.pixelSize: root.fontSize
        fontSizeMode: root.overflowMode === "fit" ? Text.HorizontalFit : Text.FixedSize
        minimumPixelSize: Math.round(root.fontSize * 0.6)
        wrapMode: root.overflowMode === "wrap" ? Text.WordWrap : Text.NoWrap
        maximumLineCount: root.overflowMode === "wrap" ? 2 : 1
        elide: root.overflowMode === "fit" || root.overflowMode === "wrap" ? Text.ElideRight : Text.ElideNone
        horizontalAlignment: root.overflowMode === "marquee" ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    SequentialAnimation {
        id: marquee
        running: Kirigami.Units.longDuration > 0
            && root.overflowMode === "marquee"
            && mainText.implicitWidth > root.width
            && root.visible
        loops: Animation.Infinite
        PauseAnimation { duration: 900 }
        NumberAnimation {
            target: mainText
            property: "x"
            from: 0
            to: Math.min(0, root.width - mainText.implicitWidth)
            duration: Math.max(2500, (mainText.implicitWidth - root.width) * 28)
            easing.type: Easing.Linear
        }
        PauseAnimation { duration: 900 }
        ScriptAction { script: mainText.x = 0 }
    }
}
