import QtQuick

Item {
    id: root

    property string title: ""
    property string artists: ""
    property string layoutMode: "single"
    property color textColor: "white"
    property bool strokeEnabled: false
    property color strokeColor: "black"
    property int fontSize: 19
    property int fontWeight: Font.Normal
    property string overflowMode: "fit"

    // Deliberately not read off artistLine.visible: Item.visible reflects
    // effective (ancestor-combined) visibility, not just this condition, so
    // it is the wrong thing to build implicitHeight on top of.
    readonly property bool showArtistLine: root.layoutMode === "double" && root.artists.length > 0

    visible: root.title.length > 0
    implicitHeight: root.title.length === 0
        ? 0
        : (root.showArtistLine ? titleLine.implicitHeight + artistLine.implicitHeight : titleLine.implicitHeight)
    height: implicitHeight

    LyricLine {
        id: titleLine
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        // In double mode this is the title alone; in single mode it carries
        // the whole "title — artist" text, DESIGN.md's persistent track row.
        lineText: root.layoutMode === "double"
            ? root.title
            : (root.artists.length > 0
                ? i18nc("@info track info: %1 is the song title, %2 is the artist name(s)", "%1 — %2", root.title, root.artists)
                : root.title)
        textColor: root.textColor
        strokeEnabled: root.strokeEnabled
        strokeColor: root.strokeColor
        fontSize: root.fontSize
        fontWeight: root.fontWeight
        overflowMode: root.overflowMode
    }

    LyricLine {
        id: artistLine
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleLine.bottom
        visible: root.showArtistLine
        lineText: root.artists
        // Same idiom as LyricBlock.qml's translation line, but the alpha is
        // multiplied against the configured colour's own alpha rather than a
        // flat 0.75, because that colour already carries its own alpha byte.
        textColor: Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, root.textColor.a * 0.75)
        strokeEnabled: root.strokeEnabled
        strokeColor: root.strokeColor
        fontSize: root.fontSize
        fontWeight: root.fontWeight
        overflowMode: root.overflowMode
    }
}
