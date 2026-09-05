import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg
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
    // Default must stay in sync with the panelWidth entry's default in
    // config/main.xml -- the autotests instantiate LyricsView directly,
    // with no kcfg behind it to supply this value.
    property int panelWidth: 280

    // Auto-hide (DESIGN.md decision 40) is desktop-only: the panel hides via
    // Plasmoid.status instead (see main.qml), so panelMode short-circuits
    // all three of these below. Defaults keep the view fully, statically
    // visible when nothing wires them up -- autotests/tst_appearance.qml
    // instantiates LyricsView directly without a VisibilityPolicy at all.
    property bool shouldBeVisible: true
    property bool animationsArmed: false
    // Whether this widget currently owns the ksvg plate rather than the shell.
    // Driven by main.qml, which hands it back and forth around the fade -- see
    // selfDrawnPlate below and main.qml's plateSelfDrawn.
    property bool ownsPlate: false
    property int hideAnimationMs: 1000

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

    // DESIGN.md decision 40 (and its in-place correction of 25/32/36): the
    // "ksvg" plate used to be the shell's job (Plasmoid.backgroundHints:
    // DefaultBackground in main.qml), on the theory that only the shell could
    // draw a themed plate that tracks a theme change. That theory turned out
    // to be wrong -- the shell's plate is just a KSvg.FrameSvgItem on
    // "widgets/background" like the one below -- but the shell's plate is
    // also structurally useless for auto-hide: it is a *sibling* of this
    // item, not a descendant, so no opacity we set on ourselves can ever
    // fade it. Hence self-drawing on desktop, unconditionally.
    //
    // Panel keeps drawing nothing for "ksvg", matching its pre-existing
    // (if slightly misleading -- see the "顺带发现" note in decision 40)
    // behaviour: the panel's own container never painted a per-applet plate
    // to begin with, so there is nothing to take over.
    // Only while this widget holds the plate. The shell draws a strictly
    // better ksvg one -- on a theme with blurred-* elements it blurs the
    // wallpaper behind the frame, which an applet cannot reproduce -- so the
    // plate comes over only for the fade that needs it and goes straight back
    // afterwards. Panels never self-draw: their containment paints no
    // per-applet frame at all, so "ksvg" has always rendered nothing there.
    readonly property bool selfDrawnPlate: root.plateMode === "ksvg"
        && !root.panelMode && root.ownsPlate
    readonly property real baseMargin: Math.max(Kirigami.Units.smallSpacing, root.fontSize * 0.35)
    readonly property real plateMarginLeft: root.selfDrawnPlate ? plate.margins.left : 0
    readonly property real plateMarginTop: root.selfDrawnPlate ? plate.margins.top : 0
    readonly property real plateMarginRight: root.selfDrawnPlate ? plate.margins.right : 0
    readonly property real plateMarginBottom: root.selfDrawnPlate ? plate.margins.bottom : 0

    // The container used to inset our whole item by the shell plate's own
    // margins (BasicAppletContainer.qml's leftPadding et al., driven by the
    // now-unconditionally-NoBackground hint). Now that inset has to happen
    // *inside* this item instead, so it is folded into implicit/minimum size
    // here and applied to the content below, on top of the pre-existing
    // font-relative margin -- otherwise the plate would sit flush against
    // this item's own edges (using up the width it wants to reserve for its
    // own frame) rather than around the text like before.
    // AppletQuickItem forwards only the Layout.* hints below up to the
    // applet container, never implicitWidth/implicitHeight -- so neither of
    // these two ever determines the widget's actual size in either form
    // factor. They still exist as this item's own fallback content size.
    implicitWidth: (panelMode ? Kirigami.Units.gridUnit * 14 : Kirigami.Units.gridUnit * 28)
        + (root.selfDrawnPlate ? plate.margins.horizontal : 0)
    implicitHeight: (panelMode ? Kirigami.Units.gridUnit * 2 : Kirigami.Units.gridUnit * 7.5)
        + (root.selfDrawnPlate ? plate.margins.vertical : 0)
    // QQuickLayouts never renders an item below its own Layout.minimumWidth
    // regardless of preferredWidth, so this floor must never sit above a
    // user's panelWidth or a narrower panelWidth would be silently lost.
    Layout.minimumWidth: (panelMode ? Math.min(Kirigami.Units.gridUnit * 8, root.panelWidth) : Kirigami.Units.gridUnit * 18)
        + (root.selfDrawnPlate ? plate.margins.horizontal : 0)
    Layout.minimumHeight: trackInfo.implicitHeight + Kirigami.Units.gridUnit * 2
        + (root.selfDrawnPlate ? plate.margins.vertical : 0)
    // -1 is the QQuickLayouts "unset" sentinel. The desktop path must keep
    // it: GridLayoutManager.adjustToItemSizeHints() only ever grows items
    // and runs every session even on restored geometry, so an unconditional
    // preferred width here would silently enlarge existing users'
    // hand-shrunk desktop widgets on next login.
    Layout.preferredWidth: panelMode ? root.panelWidth : -1

    // Panel is exempt on purpose (decision 40: "面板无动画") -- it hides via
    // Plasmoid.status/HiddenStatus instead, which pulls the container out of
    // the layout entirely rather than fading a hole into the panel.
    opacity: root.panelMode || root.shouldBeVisible ? 1 : 0
    Behavior on opacity {
        // hideAnimationMs === 0 means "no animation" (decision 40), and
        // Kirigami.Units.longDuration <= 1 means the user turned off
        // animations globally in System Settings -- both skip the Behavior
        // entirely rather than run a NumberAnimation with duration 0, which
        // would still take a frame. animationsArmed guards the one landing
        // transition out of "undetermined": that has to be an instant jump,
        // never a fade, or the widget visibly fades in on every login.
        enabled: !root.panelMode && root.animationsArmed
            && root.hideAnimationMs > 0 && Kirigami.Units.longDuration > 1
        NumberAnimation {
            duration: root.hideAnimationMs
            easing.type: Easing.OutCubic
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.plateMode === "solid"
        color: root.solidColor
        radius: Kirigami.Units.cornerRadius
    }

    Loader {
        anchors.fill: parent
        anchors.topMargin: root.baseMargin + root.plateMarginTop
        anchors.bottomMargin: root.baseMargin + root.plateMarginBottom
        anchors.leftMargin: root.baseMargin + root.plateMarginLeft
        anchors.rightMargin: root.baseMargin + root.plateMarginRight
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
        anchors.topMargin: root.baseMargin + root.plateMarginTop
        anchors.bottomMargin: root.baseMargin + root.plateMarginBottom
        anchors.leftMargin: root.baseMargin + root.plateMarginLeft
        anchors.rightMargin: root.baseMargin + root.plateMarginRight
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

    // Declared last but pinned behind everything above with z (same idiom as
    // the shell's own popup plate, CompactApplet.qml's expandedItem): it has
    // to come after the ColumnLayout it supplies margins to -- QML resolves
    // ids across the whole component regardless of declaration order, so the
    // forward references above are fine -- and appending rather than
    // inserting keeps this a pure addition to the child list instead of
    // renumbering the existing children (autotests/tst_appearance.qml reaches
    // into LyricsView's children by index).
    KSvg.FrameSvgItem {
        id: plate
        z: -1
        anchors.fill: parent
        visible: root.selfDrawnPlate
        imagePath: "widgets/background"

        // The same frame graphics the shell picks, so the plate is as
        // translucent here as it is under a shell-drawn widget: on ChromeOS
        // the plain frame is 96% opaque and the "blurred" one 60%, over
        // identical margins. What we cannot bring across is the MultiEffect
        // the shell stacks behind that frame to blur the wallpaper -- it
        // reaches into the containment's own window -- so the wallpaper shows
        // through sharp rather than blurred. Themes without the prefix (Breeze
        // ships none) fall back to the plain frame, which is what they always
        // drew anyway.
        //
        // hasElementPrefix() is a plain function, not a bindable property, so
        // the binding is re-established whenever the frame reloads. The shell
        // does the same thing for the same reason (BasicAppletContainer.qml's
        // bindBlurEnabled).
        function bindPrefix() {
            prefix = Qt.binding(() => hasElementPrefix("blurred") ? "blurred" : "");
        }
        Component.onCompleted: bindPrefix()
        onRepaintNeeded: bindPrefix()
    }
}
