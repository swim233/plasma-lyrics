import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: root

    property string lineText: ""
    property color textColor: "white"
    property bool strokeEnabled: false
    property color strokeColor: "black"
    property int fontSize: 34
    property int fontWeight: Font.Normal
    property string overflowMode: "fit"

    readonly property real lineHeight: Math.ceil(fontSize * 1.25)
    implicitHeight: overflowMode === "wrap" ? Math.min(mainText.implicitHeight, lineHeight * 2) : lineHeight
    height: implicitHeight
    clip: true

    // The scroll position lives here, not on mainText.x, so that x stays a
    // binding. An animation that writes a property directly owns it until
    // something writes it back, and nothing ever did: leaving marquee mode
    // mid-scroll stranded the line at whatever negative x the stopped
    // NumberAnimation had reached, because the reset sat at the tail of the
    // sequence and a mid-cycle stop never reaches it.
    property real marqueeOffset: 0

    // Split in two deliberately. `marqueeApplies` is what the x binding reads,
    // and it leaves `visible` out: Item.visible is ancestor-combined and reads
    // false throughout the QML test suite (see the comment on
    // test_trackInfoStaysUpThroughBlankLyricStates), so folding it in here
    // would pin x at 0 in every test and let the regression tests below pass
    // just as well with this whole mechanism deleted. `marqueeRunning` adds
    // the conditions that only decide whether running the animation is worth
    // the power.
    readonly property bool marqueeApplies: overflowMode === "marquee"
        && mainText.implicitWidth > width
    readonly property bool marqueeRunning: marqueeApplies
        && Kirigami.Units.longDuration > 0
        && visible
        && !restarting

    // A new line has to scroll from its start, and the only way to restart a
    // declaratively driven animation is to let the binding stop it: calling
    // marquee.restart() would assign `running` imperatively and destroy that
    // binding, after which leaving marquee mode would never stop the animation
    // again -- the trap DESIGN.md decision 40 documents for Plasmoid.status,
    // in the opposite direction. The zero-interval Timer follows the existing
    // switchTimer in AnimatedLyric.qml.
    property bool restarting: false
    onLineTextChanged: {
        restarting = true;
        marqueeOffset = 0;
        restartPulse.restart();
    }

    Timer {
        id: restartPulse
        interval: 0
        onTriggered: root.restarting = false
    }

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
        x: root.marqueeApplies ? root.marqueeOffset : 0
        y: 0
        width: root.overflowMode === "marquee" ? implicitWidth : root.width
        height: root.height
        text: root.lineText
        color: root.textColor
        // The family follows the Plasma font setting; size, weight and colour
        // stay with the widget's own configuration, because lyrics sit on the
        // wallpaper, where a theme colour is not guaranteed to be readable --
        // DESIGN.md decision 30.
        font.family: Kirigami.Theme.defaultFont.family
        font.pixelSize: root.fontSize
        font.weight: root.fontWeight
        fontSizeMode: root.overflowMode === "fit" ? Text.HorizontalFit : Text.FixedSize
        minimumPixelSize: Math.round(root.fontSize * 0.6)
        wrapMode: root.overflowMode === "wrap" ? Text.WordWrap : Text.NoWrap
        maximumLineCount: root.overflowMode === "wrap" ? 2 : 1
        elide: root.overflowMode === "fit" || root.overflowMode === "wrap" || root.overflowMode === "elide"
            ? Text.ElideRight : Text.ElideNone
        horizontalAlignment: root.overflowMode === "marquee" ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    SequentialAnimation {
        id: marquee
        running: root.marqueeRunning
        loops: Animation.Infinite
        // Zeroes at the top of every cycle and on every re-arm, so re-entering
        // marquee mode carrying a stale offset cannot display it through the
        // lead-in pause below. This replaces the reset that used to sit at the
        // tail of the sequence, where only an uninterrupted cycle reached it.
        PropertyAction { target: root; property: "marqueeOffset"; value: 0 }
        PauseAnimation { duration: 900 }
        NumberAnimation {
            target: root
            property: "marqueeOffset"
            from: 0
            to: Math.min(0, root.width - mainText.implicitWidth)
            duration: Math.max(2500, (mainText.implicitWidth - root.width) * 28)
            easing.type: Easing.Linear
        }
        PauseAnimation { duration: 900 }
    }
}
