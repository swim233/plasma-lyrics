import QtQuick

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

    implicitHeight: origin.implicitHeight + (translation.visible ? translation.implicitHeight : 0)
    height: implicitHeight

    LyricLine {
        id: origin
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        lineText: root.lyricText
        textColor: root.textColor
        strokeEnabled: root.strokeEnabled
        strokeColor: root.strokeColor
        fontSize: root.fontSize
        fontWeight: root.fontWeight
        overflowMode: root.overflowMode
    }

    LyricLine {
        id: translation
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: origin.bottom
        visible: root.translationText.length > 0
        lineText: root.translationText
        textColor: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.68)
        strokeEnabled: root.strokeEnabled
        strokeColor: root.strokeColor
        fontSize: root.fontSize
        fontWeight: root.fontWeight
        overflowMode: root.overflowMode
    }
}

