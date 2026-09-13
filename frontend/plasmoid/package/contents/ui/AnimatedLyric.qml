import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: root

    property string lyricText: ""
    // Whichever second line the user picked -- translation or romanization.
    property string translationText: ""
    property color textColor: "white"
    property color secondLineColor: Qt.rgba(root.textColor.r, root.textColor.g,
                                            root.textColor.b, 0.68)
    property bool strokeEnabled: false
    property color strokeColor: "black"
    property int fontSize: 34
    property int fontWeight: Font.Normal
    property string overflowMode: "fit"
    property string animationMode: "slide"

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

    readonly property string effectiveAnimationMode: Kirigami.Units.longDuration > 0
        ? animationMode : "none"

    property string shownText: ""
    property string shownTranslation: ""
    property var shownWords: []
    property string previousText: ""
    property string previousTranslation: ""
    property var previousWords: []

    // Identity is no use here: reading LyricSource.currentWords builds a fresh
    // JS array every time, so `!==` would fire on any re-evaluation of the
    // binding feeding `words`, not only on an actual line change. First and
    // last timing plus the count pins the line down without walking it.
    function wordsKey(list) {
        if (!list || list.length === 0) {
            return "";
        }
        return list.length + "@" + list[0].startMs + "-" + list[list.length - 1].endMs;
    }

    // Both halves, not just the lyric. The previous block stays alive at
    // opacity 0 and, crucially, still `visible`, so whatever text it holds
    // keeps satisfying LyricLine's marquee arm condition -- leaving the
    // translation behind left an infinite animation scrolling a line nobody
    // can see, for as long as the widget was up.
    function releasePrevious() {
        previousText = "";
        previousTranslation = "";
        previousWords = [];
    }

    function switchLine() {
        // Two consecutive lines can carry the same text and different word
        // timings (a repeated refrain line), and those have to transition so
        // the highlight restarts from the first word.
        if (shownText === lyricText && shownTranslation === translationText
            && wordsKey(shownWords) === wordsKey(words)) return;
        previousText = shownText;
        previousTranslation = shownTranslation;
        previousWords = shownWords;
        shownText = lyricText;
        shownTranslation = translationText;
        shownWords = words;
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
        shownWords = words;
    }
    onLyricTextChanged: switchTimer.restart()
    onTranslationTextChanged: switchTimer.restart()
    onWordsChanged: switchTimer.restart()

    // Do not "fix" this delay into a synchronous call. It means the word
    // glyphs clear one event-loop turn after LyricsView's clock has already
    // stopped -- clock first, glyphs a turn later -- and that is the safe
    // ordering of the two. The reverse would be glyphs gone while the clock
    // still runs, i.e. "hidden but still ticking", which is precisely the
    // state the word-by-word toggle exists to make unreachable (DESIGN.md
    // decision 38 and the toggle's entry in decision 69).
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
        // With a fade or slide transition this block goes on holding its word
        // glyphs for the ~260 ms the animation lasts, which looks wrong in a
        // screenshot and is not. positionMs is past this line's last word by
        // the time the transition starts, so every glyph here is in the sung
        // colour; the last word may still be coming down -- its spring release
        // (LyricLine, decision 73) runs on for ~600 ms past endMs -- and that
        // is the intended look, the outgoing line settling as it fades, not a
        // glyph being scanned. Removing the words would cost the outgoing line
        // its colours mid-fade for no gain.
        words: root.previousWords
        textColor: root.textColor
        secondLineColor: root.secondLineColor
        strokeEnabled: root.strokeEnabled
        strokeColor: root.strokeColor
        fontSize: root.fontSize
        fontWeight: root.fontWeight
        overflowMode: root.overflowMode
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
    }

    LyricBlock {
        id: current
        property real slideOffset: 0
        anchors.left: parent.left
        anchors.right: parent.right
        y: (parent.height - height) / 2 + slideOffset
        lyricText: root.shownText
        translationText: root.shownTranslation
        words: root.shownWords
        textColor: root.textColor
        secondLineColor: root.secondLineColor
        strokeEnabled: root.strokeEnabled
        strokeColor: root.strokeColor
        fontSize: root.fontSize
        fontWeight: root.fontWeight
        overflowMode: root.overflowMode
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
