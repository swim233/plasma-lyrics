import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: root

    property string lyricText: ""
    property string translationText: ""
    property color textColor: "white"
    property bool strokeEnabled: false
    property color strokeColor: "black"
    property int fontSize: 34
    property int fontWeight: Font.Normal
    property string overflowMode: "fit"
    property string animationMode: "slide"
    readonly property string effectiveAnimationMode: Kirigami.Units.longDuration > 0
        ? animationMode : "none"

    property string shownText: ""
    property string shownTranslation: ""
    property string previousText: ""
    property string previousTranslation: ""

    // Both halves, not just the lyric. The previous block stays alive at
    // opacity 0 and, crucially, still `visible`, so whatever text it holds
    // keeps satisfying LyricLine's marquee arm condition -- leaving the
    // translation behind left an infinite animation scrolling a line nobody
    // can see, for as long as the widget was up.
    function releasePrevious() {
        previousText = "";
        previousTranslation = "";
    }

    function switchLine() {
        if (shownText === lyricText && shownTranslation === translationText) return;
        previousText = shownText;
        previousTranslation = shownTranslation;
        shownText = lyricText;
        shownTranslation = translationText;
        transition.stop();
        previous.opacity = effectiveAnimationMode === "none" ? 0 : 1;
        previous.slideOffset = 0;
        current.opacity = effectiveAnimationMode === "none" ? 1 : 0;
        current.slideOffset = effectiveAnimationMode === "slide" ? height * 0.28 : 0;
        if (effectiveAnimationMode === "none") {
            releasePrevious();
        } else {
            transition.start();
        }
    }

    Component.onCompleted: {
        shownText = lyricText;
        shownTranslation = translationText;
    }
    onLyricTextChanged: switchTimer.restart()
    onTranslationTextChanged: switchTimer.restart()

    Timer {
        id: switchTimer
        interval: 0
        onTriggered: root.switchLine()
    }

    LyricBlock {
        id: previous
        property real slideOffset: 0
        anchors.left: parent.left
        anchors.right: parent.right
        y: (parent.height - height) / 2 + slideOffset
        lyricText: root.previousText
        translationText: root.previousTranslation
        textColor: root.textColor
        strokeEnabled: root.strokeEnabled
        strokeColor: root.strokeColor
        fontSize: root.fontSize
        fontWeight: root.fontWeight
        overflowMode: root.overflowMode
    }

    LyricBlock {
        id: current
        property real slideOffset: 0
        anchors.left: parent.left
        anchors.right: parent.right
        y: (parent.height - height) / 2 + slideOffset
        lyricText: root.shownText
        translationText: root.shownTranslation
        textColor: root.textColor
        strokeEnabled: root.strokeEnabled
        strokeColor: root.strokeColor
        fontSize: root.fontSize
        fontWeight: root.fontWeight
        overflowMode: root.overflowMode
    }

    ParallelAnimation {
        id: transition
        NumberAnimation {
            target: previous
            property: "opacity"
            to: 0
            duration: root.effectiveAnimationMode === "fade" ? 180 : 260
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: current
            property: "opacity"
            to: 1
            duration: root.effectiveAnimationMode === "fade" ? 180 : 260
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: previous
            property: "slideOffset"
            to: root.effectiveAnimationMode === "slide" ? -root.height * 0.28 : 0
            duration: root.effectiveAnimationMode === "fade" ? 180 : 260
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: current
            property: "slideOffset"
            to: 0
            duration: root.effectiveAnimationMode === "fade" ? 180 : 260
            easing.type: Easing.OutCubic
        }
        onFinished: root.releasePrevious()
    }
}
