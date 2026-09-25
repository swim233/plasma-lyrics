import QtQuick
import QtQuick.Effects
import org.kde.kirigami as Kirigami

Item {
    id: root

    property string lineText: ""
    property color textColor: "white"
    property bool strokeEnabled: false
    property color strokeColor: "black"
    // Already resolved by main.qml through FontPolicy.js: an installed family
    // exactly as FontCatalog lists it, never "" and never a stored name that
    // is not installed -- either would leave the choice to fontconfig's
    // substitution. The whole-line Text, the word glyphs and the TextMetrics
    // that sizes both read this one property, so what is measured is what
    // is drawn.
    property string fontFamily: Kirigami.Theme.defaultFont.family
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

    // Word-lift dynamics (DESIGN.md decision 73). Constants, not
    // configuration: properties only so that tests can pin them. A word's
    // envelope is a damped-spring step response in absolute time -- it rises
    // to liftEm in liftArriveMs however long it is sung, holds there, and is
    // released at its endMs with the same damping ratio, so it dips under the
    // baseline once before it rests. Measured on this machine's cache (22
    // word-timed tracks, ~9 000 words): median word 230 ms, 55% under 250 ms,
    // 92.5% of adjacent words touching. The progress-based sin(p·π) this
    // replaced rose and fell inside the word, so a 230 ms word twitched for
    // 115 ms each way and never rested anywhere.
    property real liftArriveMs: 150     // startMs to the first peak
    property real liftOvershoot: 0.10   // how far that peak exceeds liftEm
    property real liftReleaseMs: 300    // endMs to the lowest point

    // ζ from the overshoot, os = exp(-ζπ/√(1-ζ²)). The floor keeps the system
    // under-damped so the closed form stays finite; the overshoot 0.1% implies
    // is invisible. Headroom below uses the raw value, not this clamped one.
    readonly property real liftDamping: {
        const l = Math.log(Math.max(0.001, Math.min(0.9, root.liftOvershoot)));
        return -l / Math.sqrt(Math.PI * Math.PI + l * l);
    }
    readonly property real liftDampingRoot: Math.sqrt(1 - root.liftDamping * root.liftDamping)
    // Natural frequencies (rad/s) that put the first peak of the rise at
    // liftArriveMs and the lowest point of the release at liftReleaseMs:
    // t_peak = π / (ω·√(1-ζ²)).
    readonly property real liftRiseOmega: Math.PI / (root.liftArriveMs / 1000 * root.liftDampingRoot)
    readonly property real liftReleaseOmega: Math.PI / (root.liftReleaseMs / 1000 * root.liftDampingRoot)
    // This long after endMs the release is within 1% of rest (the
    // oscillation's amplitude is bounded by e^(-ζωt)/√(1-ζ²)) and the envelope
    // is snapped to exactly 0. Without the snap every sung word on the line
    // would carry a sub-pixel y that changes on every frame, forever.
    readonly property real liftSettleMs: 1000 * Math.log(100 / root.liftDampingRoot)
        / (root.liftDamping * root.liftReleaseOmega)

    // Unit step response of the under-damped second-order system, tau in ms.
    function stepResponse(tauMs, omega) {
        if (tauMs <= 0) {
            return 0;
        }
        const tau = tauMs / 1000;
        const z = root.liftDamping, q = root.liftDampingRoot;
        return 1 - Math.exp(-z * omega * tau)
            * (Math.cos(omega * q * tau) + (z / q) * Math.sin(omega * q * tau));
    }

    // One word's lift at lyric time t, in units of liftEm: 0 before startMs;
    // the rise from startMs, held for as long as the word is sung (a word
    // shorter than liftArriveMs is released part-way up); from endMs a release
    // from wherever it was, discarding the rise's velocity; exactly 0 once
    // settled. The release scales the step response instead of superposing a
    // second one so that a short word cannot be driven under the baseline by
    // a release that outruns its own rise.
    function liftEnvelope(t, startMs, endMs) {
        if (t < startMs) {
            return 0;
        }
        if (t < endMs) {
            return root.stepResponse(t - startMs, root.liftRiseOmega);
        }
        if (t >= endMs + root.liftSettleMs) {
            return 0;
        }
        return root.stepResponse(endMs - startMs, root.liftRiseOmega)
            * (1 - root.stepResponse(t - endMs, root.liftReleaseOmega));
    }

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

    // Reserved above the text so a lifted word has somewhere to go: at
    // fontSize 34 the plain 1.25 line box leaves ~1.5px over the glyphs
    // against a 0.14em (~4.8px) lift whose spring peak is another 10%
    // (~5.2px) on top -- hence the (1 + overshoot) factor, 6px at 34px where
    // the settled lift alone would round to 5. Nothing is reserved below for
    // the release's ~10% dip under the baseline (~0.5px at 34px); that is
    // covered by glyphSpill below, alongside the font's own descent, not by
    // a margin sized for the lift. Keyed on the lift setting alone, never on
    // whether this song happens to carry words -- otherwise the text
    // baseline would hop every time playback moved between a word-timed
    // source and a plain one.
    readonly property real liftHeadroom: root.liftEnabled && root.overflowMode !== "wrap"
        ? Math.ceil(root.fontSize * root.liftEm * (1 + root.liftOvershoot))
        : 0
    readonly property real lineHeight: Math.ceil(fontSize * root.lineHeightFactor) + root.liftHeadroom
    implicitHeight: overflowMode === "wrap" ? Math.min(mainText.implicitHeight, lineHeight * 2) : lineHeight
    height: implicitHeight

    readonly property int minimumPixelSize: Math.round(root.fontSize * 0.6)

    // Kirigami.Units.longDuration <= 1 is how the rest of the widget reads
    // "the user turned animations off in System Settings" (see LyricsView's
    // fade Behavior). Colour still advances word by word there, because that
    // is information rather than decoration; the lift and brightness envelopes
    // are decoration and go flat, and the line spawns no particles. Writable
    // only so that tests can turn it off: Kirigami.Units is a global the
    // suite cannot vary.
    property bool envelopesAnimate: Kirigami.Units.longDuration > 1

    // Measured at the nominal size so word mode can reproduce "fit" with one
    // shared pixel size. Text.HorizontalFit cannot be used per word: it would
    // shrink each word to its own width independently, which is a different
    // thing entirely, and Text offers no way to read back the size it settled
    // on for the line as a whole.
    TextMetrics {
        id: metrics
        // Non-visual, so absent from `children`; the tests find it by name
        // among `resources`.
        objectName: "lineMetrics"
        font.family: root.fontFamily
        font.pixelSize: root.fontSize
        font.weight: root.fontWeight
        text: root.lineText
    }

    // The font's own line height (ascent + descent + leading), measured
    // rather than assumed: it runs well past fontSize × lineHeightFactor
    // (Noto Sans CJK SC measures ~1.471em against a 1.25 line box), and
    // AlignVCenter splits that excess evenly above and below the text box --
    // the top half lands in blank space above the caps and is invisible, the
    // bottom half is real ink (descenders) that clip would otherwise cut.
    // glyphSpill is that bottom half, added on both edges of clipper below so
    // the clip rectangle is exactly as tall as the glyphs need.
    FontMetrics {
        id: fontMetrics
        font: metrics.font
    }

    readonly property real glyphSpill: Math.max(0,
        (Math.ceil(fontMetrics.height) - (root.height - root.liftHeadroom)) / 2)
    readonly property real contentTop: root.liftHeadroom + root.glyphSpill

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

    // What AnimatedLyric's particle layer needs to know about this line
    // (DESIGN.md decision 77): each word's timing, text, x and resting
    // baseline in the row; the row's position in this item, through clipper
    // and before any marquee scroll; and the font the glyphs are drawn in,
    // which the layer measures the ink and the CJK ascent with. The baseline
    // is per word because each word is its own Text centred on its own line
    // height: a word drawn in the fallback CJK font sits a pixel or two
    // lower than a Latin one. startMs is the first word's: the line's own
    // timestamp never reaches the renderer. null whenever the line spawns
    // nothing: not in word mode (wrap, or a line too long to fit) or with
    // animations off.
    //
    // Reads Repeater.count for the reason activeWordItem does, and every
    // delegate's x so that it settles once the Row has laid the words out.
    readonly property var particleLine: {
        if (!root.wordMode || !root.envelopesAnimate || wordRepeater.count !== root.words.length) {
            return null;
        }
        const words = [];
        for (let i = 0; i < wordRepeater.count; ++i) {
            const word = root.words[i];
            const item = wordRepeater.itemAt(i);
            words.push({ startMs: word.startMs, endMs: word.endMs, text: word.text,
                         x: item.x, baseline: item.baselineOffset });
        }
        return {
            startMs: root.words[0].startMs,
            text: root.lineText,
            words: words,
            x: clipper.x + (root.marqueeApplies ? 0 : wordRow.x),
            y: clipper.y + wordRow.y,
            font: Qt.font({ family: root.fontFamily, pixelSize: root.wordPixelSize,
                            weight: root.fontWeight })
        };
    }
    // The horizontal part of what this line's particles follow while it is
    // the current one: the marquee scroll that particleLine.x leaves out.
    readonly property real particleScrollOffset: root.marqueeApplies ? root.wordScrollOffset : 0

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

    // Clips horizontally at exactly [0, root.width] -- unchanged from when
    // root itself clipped, so marquee/fit/elide keep the pixel-for-pixel
    // horizontal cutoff they depend on -- but grown by glyphSpill on the top
    // and bottom edges so a glyph's full content box (including descenders)
    // fits inside the clip rectangle instead of the old fixed-height text box.
    Item {
        id: clipper
        objectName: "lineClipper"
        x: 0
        width: root.width
        y: -root.glyphSpill
        height: root.height + 2 * root.glyphSpill
        clip: true

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
            y: root.contentTop
            width: root.overflowMode === "marquee" ? implicitWidth : root.width
            height: root.height - root.liftHeadroom
            text: root.lineText
            color: root.textColor
            // Family, size, weight and colour are all the widget's own
            // configuration; the family defaults to the Plasma font. The
            // colour in particular never follows the theme, because lyrics
            // sit on the wallpaper, where a theme colour is not guaranteed
            // to be readable -- DESIGN.md decision 30.
            font.family: root.fontFamily
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
            y: root.contentTop
            height: root.height - root.liftHeadroom

            Repeater {
                id: wordRepeater
                model: root.wordMode ? root.words : []

                delegate: Item {
                    id: word
                    required property var modelData

                    width: glyph.implicitWidth
                    height: wordRow.height

                    readonly property bool started: root.positionMs >= modelData.startMs
                    readonly property bool finished: root.positionMs >= modelData.endMs

                    // Still moving: from its start until the release has settled,
                    // which runs on past endMs -- with adjacent words touching in
                    // 92.5% of real data, a median line has up to four words in
                    // flight at once and a fast passage many more. A time window
                    // rather than a threshold on the envelope's value, because the
                    // release crosses zero more than once and a value test would
                    // flip this at every crossing. Zero-length tokens (1.4% of
                    // real words, mostly trailing punctuation) never move -- the
                    // rise is released at 0, so the envelope is identically 0 --
                    // and are kept out so they hold nothing alive.
                    readonly property bool inFlight: root.envelopesAnimate && word.started
                        && modelData.endMs > modelData.startMs
                        && root.positionMs < modelData.endMs + root.liftSettleMs
                    // The halo's shorter window. By endMs + liftReleaseMs the
                    // release has crossed zero (at about 0.7 × liftReleaseMs) and
                    // the clipped glow never again exceeds about 1%, so a halo
                    // kept alive to the settle point would be an invisible
                    // MultiEffect for half its life. Still a window, not a
                    // threshold, for the reason above.
                    readonly property bool haloAlive: word.inFlight
                        && root.positionMs < modelData.endMs + root.liftReleaseMs
                    // In units of liftEm: reaches 1 + liftOvershoot at the peak and
                    // dips under 0 once during the release.
                    readonly property real envelope: word.inFlight
                        ? root.liftEnvelope(root.positionMs, modelData.startMs, modelData.endMs)
                        : 0
                    // The brightening and the halo take the envelope clipped to
                    // 0..1: the peak does not over-brighten, and the dip does not
                    // darken a word that has just been sung.
                    readonly property real glow: Math.max(0, Math.min(1, word.envelope))
                    // Colour switches for the whole word at its own start time and
                    // is not interpolated inside it; the lift and the brightening
                    // move within a word and go on moving after it -- a word
                    // already in the sung colour is still coming down.
                    readonly property color shade: word.finished
                        ? root.sungColor
                        : (word.started ? root.activeColor : root.unsungColor)
                    readonly property real lift: root.liftEnabled
                        ? root.fontSize * root.liftEm * word.envelope
                        : 0
                    // The glyph's baseline at rest, the lift left out. For
                    // particleLine.
                    baselineOffset: glyph.baselineOffset

                    // Alive for as long as the halo can be seen, so it fades with
                    // the glyph instead of vanishing at endMs while the glyph is
                    // still coming down. No longer a single instance: a median
                    // line has two or three at once, a fast passage of 20-30 ms
                    // tokens a dozen or more, and there is no cap (decision 73).
                    // Off by default, and the config page says what it costs.
                    Loader {
                        active: root.blurGlowEnabled && word.haloAlive
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
                                opacity: word.glow
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
                            ? root.brightened(word.shade, word.glow * root.brightnessStrength)
                            : word.shade
                        font.family: root.fontFamily
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
