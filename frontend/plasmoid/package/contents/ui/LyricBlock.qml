import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: root

    property string lyricText: ""
    // The secondary lyrics the user picked -- translation or romanization.
    property string secondaryLyricText: ""
    property color textColor: "white"
    // Defaults to the alpha the secondary lyrics take from the text colour
    // while their own colour is off.
    property color secondaryLyricColor: Qt.rgba(root.textColor.r, root.textColor.g,
                                                root.textColor.b, 0.68)
    property bool strokeEnabled: false
    property color strokeColor: "black"
    // The second line shares the lyric's family: it is part of the lyric,
    // not track info.
    property string fontFamily: Kirigami.Theme.defaultFont.family
    property int fontSize: 34
    property int fontWeight: Font.Normal
    property string overflowMode: "fit"

    property var words: []
    property real positionMs: 0
    property color unsungColor: root.textColor
    property color activeColor: root.textColor
    property color sungColor: root.textColor
    property bool liftEnabled: false
    property real liftEm: 0.14
    property bool brightnessEnabled: false
    property real brightnessStrength: 0.6
    property bool blurGlowEnabled: false
    property real lineHeightFactor: 1.25

    implicitHeight: origin.implicitHeight + (secondaryLyric.visible ? secondaryLyric.implicitHeight : 0)
    height: implicitHeight

    // For AnimatedLyric's particle layer (DESIGN.md decision 77). Only the
    // lyric itself spawns particles, never the secondary lyrics; its line
    // sits at this block's top left. particlesWanted only on the current
    // block.
    property bool particlesWanted: false
    readonly property var particleLine: origin.particleLine
    readonly property real particleScrollOffset: origin.particleScrollOffset

    LyricLine {
        id: origin
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        lineText: root.lyricText
        textColor: root.textColor
        strokeEnabled: root.strokeEnabled
        strokeColor: root.strokeColor
        fontFamily: root.fontFamily
        fontSize: root.fontSize
        fontWeight: root.fontWeight
        overflowMode: root.overflowMode
        words: root.words
        positionMs: root.positionMs
        unsungColor: root.unsungColor
        activeColor: root.activeColor
        sungColor: root.sungColor
        liftEnabled: root.liftEnabled
        liftEm: root.liftEm
        brightnessEnabled: root.brightnessEnabled
        brightnessStrength: root.brightnessStrength
        blurGlowEnabled: root.blurGlowEnabled
        lineHeightFactor: root.lineHeightFactor
        particlesWanted: root.particlesWanted
    }

    // The secondary lyrics stay whole-line on purpose: word timings belong
    // to the lyric itself, and neither a translation nor a romanization line
    // is aligned to them (DESIGN.md 26 already treats the secondary lyrics as
    // the weaker of the two).
    LyricLine {
        id: secondaryLyric
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: origin.bottom
        visible: root.secondaryLyricText.length > 0
        lineText: root.secondaryLyricText
        textColor: root.secondaryLyricColor
        strokeEnabled: root.strokeEnabled
        strokeColor: root.strokeColor
        fontFamily: root.fontFamily
        fontSize: root.fontSize
        fontWeight: root.fontWeight
        overflowMode: root.overflowMode
        lineHeightFactor: root.lineHeightFactor
    }
}
