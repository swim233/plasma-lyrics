import QtQuick
import QtQuick.Effects
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

    // Word-by-word. `words` carries LyricSource.currentWords verbatim --
    // [{ startMs, endMs, text }] -- and `positionMs` is the lyric-time
    // position the widget is at, offset already applied by LyricSource, so the
    // two can be compared directly. Empty `words` is the normal case, not an
    // error: only some sources carry word timings at all (DESIGN.md 5), so the
    // whole-line path below has to stay the resting shape of this component.
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

    // Two overflow modes word rendering cannot take over, both falling back to
    // the whole-line path -- the same shape a source without word timings gets.
    //
    // "wrap": the words are a single horizontal run, and two runs stacked by a
    // positioner cannot reproduce the centred two-line block Text.WordWrap
    // lays out.
    //
    // "fit"/"elide" on a line that cannot be made to fit even at
    // minimumPixelSize: the row stays wider than the item, its x pins to 0 and
    // clip cuts the tail, so the word being sung is simply gone -- no ellipsis,
    // no scrolling to bring it back, which is strictly worse than the shrink
    // -and-elide it replaced. The comparison is deliberately written over
    // values that all exist before any delegate does; phrasing it as
    // `wordRow.width > root.width` loops, because the Repeater's model depends
    // on this property.
    readonly property bool wordMode: root.words.length > 0
        && root.overflowMode !== "wrap"
        && (root.overflowMode === "marquee" || root.width <= 0
            || metrics.width * root.minimumPixelSize / root.fontSize <= root.width)

    // Reserved above the text so a lifted word has somewhere to go: the item
    // clips, and at fontSize 34 the plain 1.25 line box leaves ~1.5px over the
    // glyphs against a 0.14em (~4.8px) lift. Keyed on the lift setting alone,
    // never on whether this song happens to carry words -- otherwise the text
    // baseline would hop every time playback moved between a word-timed source
    // and a plain one.
    readonly property real liftHeadroom: root.liftEnabled && root.overflowMode !== "wrap"
        ? Math.ceil(root.fontSize * root.liftEm)
        : 0
    readonly property real lineHeight: Math.ceil(fontSize * root.lineHeightFactor) + root.liftHeadroom
    implicitHeight: overflowMode === "wrap" ? Math.min(mainText.implicitHeight, lineHeight * 2) : lineHeight
    height: implicitHeight
    clip: true

    readonly property int minimumPixelSize: Math.round(root.fontSize * 0.6)

    // Kirigami.Units.longDuration <= 1 is how the rest of the widget reads
    // "the user turned animations off in System Settings" (see LyricsView's
    // fade Behavior). Colour still advances word by word there, because that
    // is information rather than decoration; the lift and brightness envelopes
    // are decoration and go flat.
    readonly property bool envelopesAnimate: Kirigami.Units.longDuration > 1

    // Measured at the nominal size so word mode can reproduce "fit" with one
    // shared pixel size. Text.HorizontalFit cannot be used per word: it would
    // shrink each word to its own width independently, which is a different
    // thing entirely, and Text offers no way to read back the size it settled
    // on for the line as a whole.
    TextMetrics {
        id: metrics
        font.family: Kirigami.Theme.defaultFont.family
        font.pixelSize: root.fontSize
        font.weight: root.fontWeight
        text: root.lineText
    }

    readonly property int wordPixelSize: {
        if (root.overflowMode !== "fit" || root.width <= 0 || metrics.width <= root.width) {
            return root.fontSize;
        }
        return Math.max(root.minimumPixelSize,
                        Math.floor(root.fontSize * root.width / metrics.width));
    }

    readonly property real contentWidth: root.wordMode ? wordRow.width : mainText.implicitWidth

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
        && root.contentWidth > width
    // Split again, for exactly the reason marqueeApplies is split: `visible` is
    // ancestor-combined and reads false throughout the QML test suite, so any
    // condition containing it is unobservable there. Measured: with
    // marqueeApplies true, wordMode false, restarting false and longDuration
    // 200 -- every other clause satisfied -- marqueeRunning was still false.
    // A test asserting on it therefore passes just as well with the rest of
    // the condition deleted, which is what happened: both `!wordMode` and
    // `visible` could be removed with the suite still green. Everything
    // testable now lives in marqueeWanted, which the tests assert on.
    //
    // Word mode scrolls by following the current word instead (see
    // wordScrollOffset), so the timed sweep must not also be running -- two
    // things writing the horizontal position would fight for it.
    //
    // NOT COVERED BY ANY TEST: the longDuration clause below. It guards the
    // reduced-animation setting in System Settings, which is a real user
    // configuration and an accessibility-adjacent one, but Kirigami.Units is a
    // global the suite cannot vary -- it reads 200 throughout, so the clause is
    // never the reason for anything. Closing it needs a second ctest entry that
    // runs this whole suite again under an animation-factor-zero
    // XDG_CONFIG_HOME; judged not worth an ongoing double test run for one
    // clause, and the gap predates word-by-word. The other three clauses are
    // each load-bearing: deleting any one of them fails a test.
    readonly property bool marqueeWanted: marqueeApplies
        && !root.wordMode
        && Kirigami.Units.longDuration > 0
        && !restarting
    readonly property bool marqueeRunning: root.marqueeWanted && visible

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

    // The last word whose start time has passed, or -1 before the line's first
    // word. Not "the word containing the position": between two words -- QRC
    // and yrc both leave gaps -- the word just sung stays the current one
    // rather than the line briefly having none.
    readonly property int activeWordIndex: {
        if (!root.wordMode) {
            return -1;
        }
        for (let i = root.words.length - 1; i >= 0; --i) {
            if (root.positionMs >= root.words[i].startMs) {
                return i;
            }
        }
        return -1;
    }

    // itemAt() is a plain function, so this binding re-runs only when one of
    // the properties read beside it changes. Repeater.count is that property
    // on purpose: it is what moves when a new line replaces every delegate,
    // and reading words.length instead left this holding null forever whenever
    // the first evaluation happened before the delegates existed -- which is
    // exactly the case on a line whose current word is not its first.
    readonly property Item activeWordItem: root.wordMode && root.activeWordIndex >= 0
        && wordRepeater.count > root.activeWordIndex
        ? wordRepeater.itemAt(root.activeWordIndex)
        : null

    // Word mode's marquee follows the current word rather than sweeping the
    // line on a timer: on a line too long to fit, the point of scrolling is
    // that the word being sung is on screen. A separate property from
    // marqueeOffset on purpose -- that one is owned by the sweep animation and
    // has to stay a plain property, while this one is a binding with a
    // Behavior intercepting it, and one property cannot be both.
    property real wordScrollOffset: root.marqueeApplies && root.wordMode && root.activeWordItem
        ? Math.max(Math.min(0, root.width - wordRow.width),
                   Math.min(0, root.width / 2
                               - (root.activeWordItem.x + root.activeWordItem.width / 2)))
        : 0
    Behavior on wordScrollOffset {
        enabled: root.envelopesAnimate
        NumberAnimation { duration: 260; easing.type: Easing.OutCubic }
    }

    // Brightening cannot go through Qt.lighter(): that raises HSV value, and
    // the colours this is applied to are already at value 1.0 (near-white over
    // a wallpaper), so it would be a control that visibly does nothing. Fading
    // toward opaque white moves both the translucency and the remaining colour
    // cast, which is where the headroom actually is.
    function brightened(base, amount) {
        if (amount <= 0) {
            return base;
        }
        const t = Math.min(1, amount);
        return Qt.rgba(base.r + (1 - base.r) * t,
                       base.g + (1 - base.g) * t,
                       base.b + (1 - base.b) * t,
                       base.a + (1 - base.a) * t);
    }

    Repeater {
        model: root.strokeEnabled && !root.wordMode ? root.directions : []
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
        visible: !root.wordMode
        x: root.marqueeApplies ? root.marqueeOffset : 0
        y: root.liftHeadroom
        width: root.overflowMode === "marquee" ? implicitWidth : root.width
        height: root.height - root.liftHeadroom
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
        minimumPixelSize: root.minimumPixelSize
        wrapMode: root.overflowMode === "wrap" ? Text.WordWrap : Text.NoWrap
        maximumLineCount: root.overflowMode === "wrap" ? 2 : 1
        elide: root.overflowMode === "fit" || root.overflowMode === "wrap" || root.overflowMode === "elide"
            ? Text.ElideRight : Text.ElideNone
        horizontalAlignment: root.overflowMode === "marquee" ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Row {
        id: wordRow
        visible: root.wordMode
        x: root.marqueeApplies ? root.wordScrollOffset : Math.max(0, (root.width - width) / 2)
        y: root.liftHeadroom
        height: root.height - root.liftHeadroom

        Repeater {
            id: wordRepeater
            model: root.wordMode ? root.words : []

            delegate: Item {
                id: word
                required property var modelData

                width: glyph.implicitWidth
                height: wordRow.height

                // Zero-length tokens are benign in real data (trailing
                // punctuation lands on one), so the divisor is floored rather
                // than the token dropped.
                readonly property real durationMs: Math.max(1, modelData.endMs - modelData.startMs)
                readonly property real progress: Math.max(0, Math.min(1,
                    (root.positionMs - modelData.startMs) / word.durationMs))
                readonly property bool started: root.positionMs >= modelData.startMs
                readonly property bool finished: root.positionMs >= modelData.endMs
                readonly property bool active: word.started && !word.finished

                // sin(p·π): zero at both ends, peak mid-word, so a word rises
                // and settles without a step at either boundary.
                readonly property real envelope: root.envelopesAnimate && word.active
                    ? Math.sin(word.progress * Math.PI)
                    : 0
                // Colour switches for the whole word at its own start time and
                // is not interpolated inside it; only the lift and the
                // brightening move within a word.
                readonly property color shade: word.finished
                    ? root.sungColor
                    : (word.started ? root.activeColor : root.unsungColor)
                readonly property real lift: root.liftEnabled
                    ? root.fontSize * root.liftEm * word.envelope
                    : 0

                // Only ever one instance: the halo is built for the one word
                // being sung and torn down when it is not. Off by default, and
                // the config page says what it costs.
                Loader {
                    active: root.blurGlowEnabled && word.active
                    anchors.fill: glyph
                    sourceComponent: Item {
                        // MultiEffect hides its source and maps it 1:1 onto its
                        // own geometry, so a halo wider than the glyph has to
                        // come from a source that is already that wide -- both
                        // autoPaddingEnabled and paddingRect were measured to
                        // scale the glyph down instead of growing the sampled
                        // area. Hence a padded copy of the word rather than the
                        // glyph itself, which also leaves the crisp glyph free
                        // to draw over the top.
                        Item {
                            id: haloSource
                            anchors.fill: parent
                            anchors.margins: -Math.round(root.wordPixelSize * 0.35)
                            layer.enabled: true
                            Text {
                                anchors.centerIn: parent
                                text: word.modelData.text
                                color: word.shade
                                font: glyph.font
                            }
                        }
                        MultiEffect {
                            source: haloSource
                            anchors.fill: haloSource
                            autoPaddingEnabled: false
                            blurEnabled: true
                            blur: 1.0
                            blurMax: 24
                            brightness: root.brightnessEnabled ? root.brightnessStrength : 0
                            opacity: word.envelope
                        }
                    }
                }

                Text {
                    id: glyph
                    // The whole-line Text and its stroke copies are Texts too,
                    // so the QML tests need something that says "this is a word
                    // glyph" rather than a shape check that also matches them.
                    objectName: "lyricWord"
                    y: -word.lift
                    height: word.height
                    text: word.modelData.text
                    color: root.brightnessEnabled
                        ? root.brightened(word.shade, word.envelope * root.brightnessStrength)
                        : word.shade
                    font.family: Kirigami.Theme.defaultFont.family
                    font.pixelSize: root.wordPixelSize
                    font.weight: root.fontWeight
                    // Per-word outlining uses Text's own, not the eight offset
                    // copies the whole line uses. Measured on a 35-token CJK
                    // line: eight copies per word is 316 Text items and 9.5 ms
                    // to rebuild on every line change, against 2.7 ms for
                    // this. The spike lands exactly on the line change, which
                    // is the frame the eye is on. The outline it draws is
                    // thinner than the eight-copy one.
                    style: root.strokeEnabled ? Text.Outline : Text.Normal
                    styleColor: root.strokeColor
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
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
