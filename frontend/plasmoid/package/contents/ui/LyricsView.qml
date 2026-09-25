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
    // fontFamily/trackInfoFontFamily and the two weights arrive already
    // resolved by main.qml through FontPolicy.js: a stored family that is not
    // installed comes in as the Plasma font, and each weight as one of its
    // family's real faces. The defaults are what an instance with nothing
    // configured renders in.
    property string fontFamily: Kirigami.Theme.defaultFont.family
    property int fontSize: 34
    property int fontWeight: Font.Normal
    property string overflowMode: "fit"
    property string animationMode: "slide"
    property bool showTranslation: true
    // Which of the two second lines to show when showTranslation is on. The
    // pair is one three-way user choice -- translation / romanization / off --
    // kept as two keys so the existing on/off setting survives an upgrade
    // untouched instead of needing a migration to read it back.
    property string secondLineSource: "translation"
    property bool secondLineColorEnabled: false
    property color secondLineColor: "#adfffaf5"
    property int lineHeightPercent: 125
    // Floor applied at render time, independent of what is actually stored
    // in the configuration -- see main.qml's fullRepresentation (125, the
    // desktop instance) and compactRepresentation (unset, so this default of
    // 100 stands for the panel). Read here rather than clamped in main.qml,
    // which is a PlasmoidItem the QML test suite cannot instantiate
    // (CLAUDE.md); LyricsView can be, so the clamp is testable here instead.
    property int lineHeightMinPercent: 100

    property bool wordByWord: true
    // Off by default (DESIGN.md's synthetic word-by-word decision): when on,
    // and only when the *whole* current document carries no real word
    // timings, effectiveWords below reaches for source.currentSyntheticWords
    // instead of source.currentWords. Gated on wordByWord itself -- reading
    // the synthetic property with the master switch off would arm the
    // per-frame clock (see wordClockRunning) for tracks that used to cost
    // nothing at all.
    property bool syntheticWordByWord: false
    property color wordUnsungColor: "#8cfffaf5"
    property color wordActiveColor: "#e6fffaf5"
    property color wordSungColor: "#c4fffaf5"
    property bool wordLift: true
    property int wordLiftPercent: 14
    property bool wordBrightness: true
    property int wordBrightnessPercent: 60
    property bool wordBlurGlow: false
    // Word particles (DESIGN.md decision 77). main.qml sets all three from
    // the current appearance; the defaults mirror desktopWordParticles and
    // friends. wordParticleColor only applies while
    // wordParticleColorEnabled is on -- otherwise the particles take
    // wordActiveColor at full opacity.
    property bool wordParticles: true
    property bool wordParticleColorEnabled: false
    property color wordParticleColor: "#fffaf5"
    property string idleText: i18n("No media is playing")
    property string notFoundText: ""
    property string noLyricText: ""
    property string networkErrorText: ""
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
    property string trackInfoFontFamily: Kirigami.Theme.defaultFont.family
    property int trackInfoFontSize: 19
    property int trackInfoFontWeight: Font.Normal
    property color trackInfoColor: "#b3fffaf5"
    property bool trackInfoStrokeEnabled: false
    property color trackInfoStrokeColor: "#cc000000"
    property string trackInfoOverflow: "fit"

    // DESIGN.md decision 76: main.qml turns this on only while the appearance
    // set in effect is switching (AppearanceTheme's `transitioning`), so the
    // colours below fade into the other set's and every other edit applies
    // at once. The Behaviors sit on this item's own colour properties rather
    // than on each word: everything drawn reads its colour from one of them,
    // effectiveSecondLineColor included. Nothing that is not a colour
    // animates -- the plate mode, stroke switches and fonts switch
    // immediately.
    property bool animateColors: false
    property int colorTransitionMs: Kirigami.Units.veryLongDuration

    Behavior on solidColor {
        enabled: root.animateColors
        ColorAnimation { duration: root.colorTransitionMs }
    }
    Behavior on textColor {
        enabled: root.animateColors
        ColorAnimation { duration: root.colorTransitionMs }
    }
    Behavior on strokeColor {
        enabled: root.animateColors
        ColorAnimation { duration: root.colorTransitionMs }
    }
    Behavior on secondLineColor {
        enabled: root.animateColors
        ColorAnimation { duration: root.colorTransitionMs }
    }
    Behavior on wordUnsungColor {
        enabled: root.animateColors
        ColorAnimation { duration: root.colorTransitionMs }
    }
    Behavior on wordActiveColor {
        enabled: root.animateColors
        ColorAnimation { duration: root.colorTransitionMs }
    }
    Behavior on wordSungColor {
        enabled: root.animateColors
        ColorAnimation { duration: root.colorTransitionMs }
    }
    Behavior on wordParticleColor {
        enabled: root.animateColors
        ColorAnimation { duration: root.colorTransitionMs }
    }
    Behavior on trackInfoColor {
        enabled: root.animateColors
        ColorAnimation { duration: root.colorTransitionMs }
    }
    Behavior on trackInfoStrokeColor {
        enabled: root.animateColors
        ColorAnimation { duration: root.colorTransitionMs }
    }

    readonly property string effectiveText: {
        if (!source.serviceAvailable || source.stale) return "";
        if (source.lyricState === "searching") return i18n("Searching for lyrics…");
        if (source.lyricState === "not-found") return root.notFoundText;
        if (source.lyricState === "no-lyric") return root.noLyricText;
        if (source.lyricState === "network-error") return root.networkErrorText;
        if (source.playbackStatus === "Stopped" || source.trackTitle.length === 0) return root.idleText;
        if (source.lyricState === "filtered") return "";
        return source.currentText;
    }
    readonly property string effectiveSecondLine: {
        if (!root.showTranslation || source.lyricState !== "ok") return "";
        return root.secondLineSource === "romanization"
            ? source.currentRomanization : source.currentTranslation;
    }
    // A line with no romanization at all is the normal case, not a failure:
    // only one source carries any, so picking romanization on a track from
    // anywhere else leaves the second line empty rather than falling back to
    // the translation, which would make the setting mean two different things
    // depending on the track.
    readonly property color effectiveSecondLineColor: root.secondLineColorEnabled
        ? root.secondLineColor
        : Qt.rgba(root.textColor.r, root.textColor.g, root.textColor.b, 0.68)
    // Whether the lyric slot is showing the track's lyrics: exactly the
    // branch of effectiveText above that returns source.currentText, a line
    // without words and a filtered-out one aside. Anything else there -- the
    // idle text after a Stop, a search, a not-found or error message --
    // drops every particle rather than leaving them frozen over it. Paused
    // is still lyrics, and the particles stay where they are.
    readonly property bool showingLyrics: root.source.serviceAvailable && !root.source.stale
        && root.source.lyricState === "ok"
        && root.source.playbackStatus !== "Stopped" && root.source.trackTitle.length > 0

    // Opaque whichever colour it follows: each particle's own brightness
    // envelope is its alpha, and the 10% translucency the current-word colour
    // carries by default would only dim every particle once more. Fades with
    // the theme through the two colours' own Behaviors.
    readonly property color effectiveWordParticleColor: {
        const c = root.wordParticleColorEnabled ? root.wordParticleColor : root.wordActiveColor;
        return Qt.rgba(c.r, c.g, c.b, 1);
    }

    // Word timings belong to the lyric line alone. Every other thing this slot
    // can show -- "Searching…", the idle text, a custom not-found message --
    // is plain text that merely occupies the same row, and handing it the
    // current line's timings would highlight fragments of it.
    //
    // root.source.currentSyntheticWords is read only when syntheticWordByWord
    // is on AND currentWords is empty -- never merely because the switch is
    // on. LyricSource itself already empties currentSyntheticWords for any
    // document that has real words anywhere (its own document-level gate),
    // so the `currentWords.length === 0` half here is belt-and-braces, not a
    // second copy of that decision. What the ordering here actually buys is
    // that the property is never even touched while the switch is off --
    // reading it is what would otherwise justify computing synthetic timings
    // for a track that used to cost nothing at all.
    readonly property var effectiveWords: root.wordByWord
        && root.effectiveText.length > 0
        && root.source.lyricState === "ok"
        && root.effectiveText === root.source.currentText
        ? (root.syntheticWordByWord && root.source.currentWords.length === 0
            ? root.source.currentSyntheticWords
            : root.source.currentWords)
        : []

    // Word-level scanning is pulled per frame rather than pushed from a timer:
    // DESIGN.md decision 38 settled that a frame-driven source stops by itself
    // while the window is not rendering, where a 16 ms QTimer would go on
    // firing. The position is asked of LyricSource each frame rather than
    // accumulated here, so a seek or an offset change lands immediately.
    property real lyricPositionMs: 0
    function syncLyricPosition() {
        root.lyricPositionMs = root.source.lyricPositionMs();
    }
    // Covers both edges the frame pull cannot: the line changing while paused,
    // and playback resuming after the animation had stopped itself.
    readonly property string lyricPositionCue: root.source.currentText
        + "\u0000" + root.source.playbackStatus
    onLyricPositionCueChanged: root.syncLyricPosition()
    Component.onCompleted: root.syncLyricPosition()

    // Named rather than inlined into the FrameAnimation so the one condition
    // that decides whether the widget wakes 60 times a second is observable --
    // the animation is not an Item and cannot be reached from a test.
    // Switching word-by-word off empties effectiveWords, which both clears the
    // Repeater's model and stops this: that is the whole point of the setting,
    // and it is why turning the effects off or setting three identical colours
    // is not a substitute. See DESIGN.md decision 38 for why the wakeup count,
    // not the per-frame cost, is the thing being protected here.
    //
    // Word particles (DESIGN.md decision 77) keep it running past the last
    // word: a line that ends in a switch to one without words -- an
    // interlude, a blank line, the end of the song -- would otherwise stop
    // positionMs and leave every particle hanging in the air until the next
    // line with words. particlesAliveUntilMs is when the last one goes out,
    // 2050 ms of lyric time after it was born, and a line switched away from
    // keeps only the particles born by then, so each switch to a line
    // without words costs up to that much more of this clock (about 295
    // frames at 144 Hz), and decision 38's wakeup count rises accordingly on
    // tracks with particles. Nothing more: particles off,
    // word-by-word off and anything but lyrics in this slot all clear them
    // at once, and -Infinity makes the clause false, so off still means
    // decision 38's profile exactly.
    //
    // syntheticWordByWord does not weaken this: effectiveWords only ever
    // reads source.currentSyntheticWords inside the wordByWord-gated branch
    // above, so turning wordByWord off silences the synthetic clock along
    // with the real one regardless of what syntheticWordByWord itself is set
    // to. What DOES change with this switch, independent of wordByWord, is
    // the previously-free case: a track whose document has no real word
    // timings used to leave this clock permanently disarmed (decision 38's
    // "zero cost today" reasoning); with syntheticWordByWord on, such a
    // track now arms it too.
    readonly property bool wordClockRunning: (root.effectiveWords.length > 0
            || root.lyricPositionMs < lyric.particlesAliveUntilMs)
        && root.source.playbackStatus === "Playing"
        && (root.panelMode || root.shouldBeVisible)

    FrameAnimation {
        running: root.wordClockRunning
        onTriggered: root.syncLyricPosition()
    }
    // TrackInfo already collapses to zero height on an empty title (see
    // TrackInfo.qml), so gating the config off just means feeding it an
    // empty title too -- no separate visibility flag to keep in sync. It
    // stays up through searching/not-found/filtered/no-lyric/network-error on purpose:
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
        id: content
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
            fontFamily: root.trackInfoFontFamily
            fontSize: root.trackInfoFontSize
            fontWeight: root.trackInfoFontWeight
            overflowMode: root.trackInfoOverflow
        }

        AnimatedLyric {
            id: lyric
            Layout.fillWidth: true
            Layout.fillHeight: true
            lyricText: root.effectiveText
            translationText: root.effectiveSecondLine
            textColor: root.textColor
            secondLineColor: root.effectiveSecondLineColor
            strokeEnabled: root.strokeEnabled
            strokeColor: root.strokeColor
            fontFamily: root.fontFamily
            fontSize: root.fontSize
            fontWeight: root.fontWeight
            overflowMode: root.overflowMode
            animationMode: root.animationMode
            words: root.effectiveWords
            positionMs: root.lyricPositionMs
            unsungColor: root.wordUnsungColor
            activeColor: root.wordActiveColor
            sungColor: root.wordSungColor
            // A panel sets the widget's height itself, so a lifted word would
            // simply be clipped. Forced off here rather than left to the
            // configuration, which has no panel key for it at all.
            liftEnabled: !root.panelMode && root.wordLift
            liftEm: root.wordLiftPercent / 100
            brightnessEnabled: root.wordBrightness
            brightnessStrength: root.wordBrightnessPercent / 100
            blurGlowEnabled: root.wordBlurGlow
            lineHeightFactor: Math.max(root.lineHeightMinPercent, root.lineHeightPercent) / 100
            particlesEnabled: root.wordParticles && root.wordByWord && root.showingLyrics
            particleColor: root.effectiveWordParticleColor
            // The test stand-ins for LyricSource carry no fingerprint.
            particleFingerprint: root.source.fingerprint ?? ""
            // This whole item, clipped at its edges -- in a panel that is the
            // applet's own bounds.
            particleArea: Qt.rect(-(content.x + lyric.x), -(content.y + lyric.y), root.width, root.height)
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
