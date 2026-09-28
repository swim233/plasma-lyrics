import QtQuick
import org.kde.kirigami as Kirigami
import io.github.swim233.lyrics
import "Spring.js" as Spring

Item {
    id: root

    property string lyricText: ""
    // The secondary lyrics the user picked -- translation or romanization.
    property string secondaryLyricText: ""
    property color textColor: "white"
    property color secondaryLyricColor: Qt.rgba(root.textColor.r, root.textColor.g,
                                                root.textColor.b, 0.68)
    property bool strokeEnabled: false
    property color strokeColor: "black"
    property string fontFamily: Kirigami.Theme.defaultFont.family
    property int fontSize: 34
    property int fontWeight: Font.Normal
    // The secondary lyrics' font as drawn; LyricBlock has the details.
    property string secondaryLyricFontFamily: root.fontFamily
    property int secondaryLyricFontSize: root.fontSize
    property int secondaryLyricFontWeight: root.fontWeight
    property bool secondaryLyricFontItalic: false
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

    // DESIGN.md decision 80: the next line, "" when none is to be shown --
    // LyricsView has already applied every condition for showing it.
    property string nextText: ""
    // 0-100, 0 for none; scales with fontSize.
    property int nextLineBlurPercent: 25
    // What tells a line arriving in sequence from a jump (decision 28):
    // LyricSource's two indices, -1 for none, and the track's fingerprint.
    // showingLyrics is LyricsView's: false while the slot shows anything
    // but the track's lyrics (idle text, a search, an error).
    property int currentLineIndex: -1
    property int nextLineIndex: -1
    property string fingerprint: ""
    property bool showingLyrics: true

    // Word particles (DESIGN.md decision 77). Off also drops every particle
    // already in the air. The colour arrives resolved, alpha already 1.
    property bool particlesEnabled: false
    property color particleColor: root.activeColor
    // A change is a new track, and drops what is in the air; the words of
    // the line being sung that are still to come spawn as usual.
    property string particleFingerprint: ""
    // Where the particles may go, in this item's coordinates. LyricsView
    // makes it cover the whole widget: the words' own clipper leaves only a
    // few pixels above the glyphs, and a particle rises up to 45 px at the
    // 34 px default size.
    property rect particleArea: Qt.rect(0, 0, root.width, root.height)
    // Until when some particle is still in the air, -Infinity when none is.
    // LyricsView keeps its frame clock running until then, so particles
    // still finish their flight after the line switches to one without words.
    readonly property real particlesAliveUntilMs: particles.particlesAliveUntilMs

    // Kirigami.Units.longDuration is 0 when the user turned animations off
    // in System Settings; every line then switches straight away, as with
    // "none". Writable only so that tests can turn it off: Kirigami.Units is
    // a global the suite cannot vary.
    property bool transitionsAnimate: Kirigami.Units.longDuration > 0
    readonly property string effectiveAnimationMode: root.transitionsAnimate
        ? root.animationMode : "none"

    // The choreography's constants (decisions 28 and 80), not configuration:
    // properties only so that tests can read them. Lengths and blurs are
    // for the 34 px default and scale with fontSize.
    readonly property real springArriveMs: 360      // to the first peak
    readonly property real springOvershoot: 0.03
    readonly property real nextLineScale: 0.75
    readonly property real growMs: 450              // size and colour, 1.25 × arrive
    readonly property real veilClearMs: 200         // the next line's blur, once promoted
    readonly property real secondaryDelayMs: 90
    readonly property real secondaryFadeMs: 450
    readonly property real exitMs: 270              // 0.75 × arrive
    readonly property real exitScale: 0.88
    readonly property real exitBlur: 8
    readonly property real emergeDelayMs: 120
    readonly property real emergeMs: 420
    readonly property real emergeRise: 12
    readonly property real emergeScale: 0.64
    readonly property real emergeBlur: 6
    readonly property real enterFadeMs: 324         // 0.9 × arrive
    readonly property real crossfadeMs: 180
    readonly property real fullNextLineBlur: 6      // σ at 100%

    readonly property real springDamping: Spring.damping(root.springOvershoot)
    readonly property real springOmega: Spring.omega(root.springArriveMs, root.springDamping)
    // About 514 ms: from here on the spring is within 1% and is snapped to
    // rest, like the lift's release.
    readonly property real springSettleMs: Spring.settleMs(root.springDamping, root.springOmega)

    readonly property real fontScale: root.fontSize / 34
    // The next line's resting blur as a standard deviation on screen.
    readonly property real nextLineBlur: root.fullNextLineBlur * root.nextLineBlurPercent / 100
        * root.fontScale
    // The most any block is blurred, in its own unscaled pixels: a next line
    // starting to emerge, or a sung line at the end of its exit, capped at
    // what LyricBlock's calibration reaches.
    readonly property real blurPadding: Math.ceil(3 * Math.min(18, Math.max(
        (root.nextLineBlur + root.emergeBlur * root.fontScale) / root.emergeScale,
        (root.exitBlur * root.fontScale) / root.exitScale)))

    // The lines on show, which change at the switch, a turn after the
    // properties above do (switchTimer).
    property string shownText: ""
    property string shownSecondaryLyric: ""
    property var shownWords: []
    property string shownNextText: ""
    // What the last switch saw, for telling the next one apart.
    property int shownLineIndex: -1
    property int shownNextLineIndex: -1
    property string shownFingerprint: ""
    property bool shownLyrics: true

    // A block of the pool, with what the pool keeps about it: its role, its
    // motion (see move()), where it stays once it is leaving and when it is
    // gone; and the moving values the delegate below binds.
    component PooledBlock: LyricBlock {
        property string role: "free"
        property var motion: ({})
        property real frozenBase: 0
        property real releaseAtMs: Infinity
        // Where the block is drawn relative to its place: the slide of the
        // push up, and what the particles follow.
        property real slideOffset: 0
        property real grow: 1
        property real veil: 0
        property real rise: 0
        property real fall: 0
    }

    // The block in each of the two places; null when the place is empty.
    property PooledBlock currentBlock: null
    property PooledBlock nextBlock: null
    // A function, not a binding over pool.count: the Repeater's own
    // countChanged handler below runs before or after such a binding is
    // brought up to date, in no fixed order.
    function blocks() {
        const out = [];
        for (let i = 0; i < pool.count; ++i) {
            out.push(pool.itemAt(i));
        }
        return out;
    }

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

    // ---- Placement (decision 80). The group is the current place, the gap
    // and the next line at its 0.75 size. It is centred in this item while
    // it fits; otherwise the current place sits at the top and only the
    // bottom spills, so the top clip below never cuts the current line.
    // Keyed on the blocks in the two places, never on the incoming text, so
    // nothing moves before the switch that is to move it. With no current
    // line the place keeps the height of the last one, so an interlude or
    // the intro leaves the next line where it is.
    readonly property real gap: Math.round(root.fontSize * 0.12)
    readonly property real lineBoxHeight: pool.count > 0 ? root.blocks()[0].lineHeight : 0
    property real lastCurrentHeight: 0
    readonly property real currentPlaceHeight: root.currentBlock ? root.currentBlock.height
        : root.lastCurrentHeight > 0 ? root.lastCurrentHeight : root.lineBoxHeight
    readonly property real groupHeight: root.currentPlaceHeight
        + (root.nextBlock ? root.gap + root.lineBoxHeight * root.nextLineScale : 0)
    readonly property real currentY: root.groupHeight <= root.height
        ? (root.height - root.groupHeight) / 2 : 0
    readonly property real nextY: root.currentY + root.currentPlaceHeight + root.gap

    // ---- Motion. Every moving value of every block is a tween over one
    // shared clock -- { from, to, t0, dur, kind } -- or a plain number at
    // rest. The clock is a NumberAnimation that runs only while some tween
    // has not ended and stops by itself after the last: no FrameAnimation or
    // timer is left running while nothing moves (decision 38's wakeups). A
    // tween started over one still running starts from wherever that one is.
    property real clockMs: 0
    property real clockEndMs: 0
    readonly property bool animating: clock.running

    function outCubic(u) {
        const v = 1 - u;
        return 1 - v * v * v;
    }
    function valueAt(tween, t) {
        if (typeof tween === "number") {
            return tween;
        }
        const local = t - tween.t0;
        if (local <= 0) {
            return tween.from;
        }
        if (local >= tween.dur) {
            return tween.to;
        }
        const p = tween.kind === "spring"
            ? Spring.step(local, root.springDamping, root.springOmega)
            : root.outCubic(local / tween.dur);
        return tween.from + (tween.to - tween.from) * p;
    }
    function now(block, key) {
        return root.valueAt(block.motion[key], root.clockMs);
    }
    // changes: { key: number, or [to, "spring"], or [to, "out", ms, delayMs] }.
    function move(block, changes) {
        const motion = Object.assign({}, block.motion);
        for (const key in changes) {
            const change = changes[key];
            if (typeof change === "number") {
                motion[key] = change;
                continue;
            }
            const kind = change[1];
            const dur = kind === "spring" ? root.springSettleMs : change[2];
            const t0 = root.clockMs + (change[3] || 0);
            motion[key] = { from: root.valueAt(motion[key], root.clockMs), to: change[0],
                            t0: t0, dur: dur, kind: kind };
            root.clockEndMs = Math.max(root.clockEndMs, t0 + dur);
        }
        block.motion = motion;
        if (root.clockEndMs > root.clockMs) {
            // Linear and restarted from where it stands: a tween added while
            // it runs changes nothing about the ones already running.
            clock.stop();
            const duration = Math.ceil(root.clockEndMs - root.clockMs);
            clock.from = root.clockMs;
            clock.to = root.clockMs + duration;
            clock.duration = duration;
            clock.start();
        }
    }
    function restMotion(role) {
        return role === "next"
            ? { slide: 0, scale: root.nextLineScale, opacity: 1, grow: 0, veil: 1, rise: 0, fall: 0, secondary: 0 }
            : { slide: 0, scale: 1, opacity: 1, grow: 1, veil: 0, rise: 0, fall: 0, secondary: 1 };
    }

    NumberAnimation {
        id: clock
        target: root
        property: "clockMs"
        easing.type: Easing.Linear
        onFinished: root.settle()
    }
    // A line that has faded out lets go as soon as it is gone, not only when
    // the last tween of its switch ends.
    onClockMsChanged: {
        for (const block of root.blocks()) {
            if (block.releaseAtMs <= root.clockMs) {
                root.release(block);
            }
        }
    }
    // Everything has arrived: every value becomes a plain number again.
    function settle() {
        for (const block of root.blocks()) {
            if (block.releaseAtMs <= root.clockMs) {
                root.release(block);
                continue;
            }
            const motion = {};
            for (const key in block.motion) {
                motion[key] = root.now(block, key);
            }
            block.motion = motion;
        }
    }

    // ---- The pool. Lines move between roles rather than being created for
    // each switch: "current", "next", "done" (a sung line leaving), "gone"
    // (a line fading out in place) and "free".

    // Both halves, not just the lyric. A block leaving at opacity 0 but
    // still `visible` keeps satisfying LyricLine's marquee arm condition with
    // whatever text it holds -- leaving the secondary lyrics behind once left
    // an infinite animation scrolling a line nobody can see, for as long as
    // the widget was up -- so a block that has left holds nothing.
    function release(block) {
        if (block === root.currentBlock) {
            root.currentBlock = null;
        }
        if (block === root.nextBlock) {
            root.nextBlock = null;
        }
        block.role = "free";
        block.releaseAtMs = Infinity;
        block.lyricText = "";
        block.secondaryLyricText = "";
        block.words = [];
        block.staticFit = false;
        block.motion = root.restMotion("current");
    }
    // A free block, or else the one that has been leaving the longest --
    // never one of `busy`, the blocks the switch in progress moves.
    function acquire(busy) {
        let oldest = null;
        for (const block of root.blocks()) {
            if (block.role === "free") {
                return block;
            }
            if ((block.role === "done" || block.role === "gone") && busy.indexOf(block) < 0
                && (!oldest || block.releaseAtMs < oldest.releaseAtMs)) {
                oldest = block;
            }
        }
        root.release(oldest);
        return oldest;
    }
    // Where a block leaving stays, taken while the places are still those it
    // is drawn in: its slide goes on from there.
    function freeze(block, role, releaseAtMs) {
        block.frozenBase = block.y - root.now(block, "slide");
        block.role = role;
        block.releaseAtMs = releaseAtMs;
        if (block === root.currentBlock) {
            root.lastCurrentHeight = block.height;
            root.currentBlock = null;
        }
        if (block === root.nextBlock) {
            root.nextBlock = null;
        }
    }
    // The shape before the text, so the text is laid out once.
    function showCurrent(block) {
        block.staticFit = false;
        block.words = root.words;
        block.secondaryLyricText = root.secondaryLyricText;
        block.lyricText = root.lyricText;
        block.role = "current";
        root.currentBlock = block;
    }
    function showNext(block) {
        block.staticFit = true;
        block.words = [];
        block.secondaryLyricText = "";
        block.lyricText = root.nextText;
        block.role = "next";
        root.nextBlock = block;
    }

    // ---- The choreography (decision 28).

    // A sung line: up one place on the spring, shrinking, blurring and
    // fading out in 270 ms. travel is how far one place is.
    function exit(block, travel) {
        root.move(block, {
            slide: [root.now(block, "slide") - travel, "spring"],
            scale: [root.now(block, "scale") * root.exitScale, "out", root.exitMs],
            opacity: [0, "out", root.exitMs],
            fall: [1, "out", root.exitMs]
        });
    }
    // A line fading out where it is.
    function drop(block) {
        root.move(block, { opacity: [0, "out", root.crossfadeMs] });
    }
    // The next line taking over the current place, from wherever it is
    // drawn right now -- a line still emerging carries on from there.
    function promote(block, fromY) {
        root.move(block, { slide: fromY - root.currentY });
        root.move(block, {
            slide: [0, "spring"],
            scale: [1, "out", root.growMs],
            grow: [1, "out", root.growMs],
            opacity: [1, "out", root.veilClearMs],
            veil: [0, "out", root.veilClearMs],
            rise: [0, "out", root.veilClearMs],
            secondary: [1, "out", root.secondaryFadeMs, root.secondaryDelayMs]
        });
    }
    // A line arriving in sequence with no next line on show to promote:
    // from one line box below, fading in, sharp.
    function enterFromBelow(block) {
        block.motion = Object.assign(root.restMotion("current"), { slide: block.lineHeight, opacity: 0 });
        root.move(block, {
            slide: [0, "spring"],
            opacity: [1, "out", root.enterFadeMs]
        });
    }
    // The new next line, 120 ms after the switch: fading in, rising 12 px,
    // growing from 0.64× and sharpening from its resting blur plus 6 px.
    function emerge(block) {
        block.motion = Object.assign(root.restMotion("next"), {
            slide: root.emergeRise * root.fontScale, scale: root.emergeScale, opacity: 0, rise: 1
        });
        root.move(block, {
            slide: [0, "out", root.emergeMs, root.emergeDelayMs],
            scale: [root.nextLineScale, "out", root.emergeMs, root.emergeDelayMs],
            opacity: [1, "out", root.emergeMs, root.emergeDelayMs],
            rise: [0, "out", root.emergeMs, root.emergeDelayMs]
        });
    }
    function fadeIn(block, role) {
        block.motion = Object.assign(root.restMotion(role), { opacity: 0 });
        root.move(block, { opacity: [1, "out", root.crossfadeMs] });
    }

    function switchLine() {
        if (!root.placeInitially()) {
            return;
        }
        const currentSame = root.shownText === root.lyricText
            && root.shownSecondaryLyric === root.secondaryLyricText
            && root.wordsKey(root.shownWords) === root.wordsKey(root.words)
            && root.shownLineIndex === root.currentLineIndex;
        const nextSame = root.shownNextText === root.nextText;
        // A line arrives in sequence when it is the one that was next, under
        // the same fingerprint, with lyrics shown before and after; the
        // current line ending in an interlude keeps the next one. Anything
        // else is a jump: a seek, a new track, an offset change, lyrics
        // giving way to other text or back. The indices come from the data
        // whether or not the next line is on show.
        const continuous = root.showingLyrics && root.shownLyrics
            && root.fingerprint === root.shownFingerprint;
        const sequential = continuous && root.currentLineIndex >= 0
            && root.currentLineIndex === root.shownNextLineIndex;
        const interlude = continuous && root.currentLineIndex < 0 && root.shownLineIndex >= 0
            && root.nextLineIndex === root.shownNextLineIndex && root.lyricText.length === 0;
        root.shownLineIndex = root.currentLineIndex;
        root.shownNextLineIndex = root.nextLineIndex;
        root.shownFingerprint = root.fingerprint;
        root.shownLyrics = root.showingLyrics;
        if (currentSame && nextSame) {
            return;
        }
        if (!currentSame) {
            // Before anything below moves: the outgoing line's particles stay
            // where its block is right now instead of following it up and out.
            particles.detach();
            root.shownText = root.lyricText;
            root.shownSecondaryLyric = root.secondaryLyricText;
            root.shownWords = root.words;
        }
        root.shownNextText = root.nextText;

        const mode = root.effectiveAnimationMode;
        if (mode === "none") {
            root.snap(!currentSame, !nextSame);
        } else if (mode === "slide" && !currentSame && sequential) {
            root.pushUp();
        } else if (mode === "slide" && !currentSame && interlude) {
            root.endInInterlude();
            if (!nextSame) {
                root.crossfade(false, true);
            }
        } else {
            root.crossfade(!currentSame, !nextSame);
        }
    }

    // "None", or animations off: straight to the new lines.
    function snap(currentChanged, nextChanged) {
        if (currentChanged && root.currentBlock) {
            root.freeze(root.currentBlock, "gone", root.clockMs);
        }
        if (nextChanged && root.nextBlock) {
            root.freeze(root.nextBlock, "gone", root.clockMs);
        }
        for (const block of root.blocks()) {
            if (block.releaseAtMs <= root.clockMs) {
                root.release(block);
            }
        }
        if (currentChanged && root.lyricText.length > 0) {
            const block = root.acquire([]);
            root.showCurrent(block);
            block.motion = root.restMotion("current");
        }
        if (nextChanged && root.nextText.length > 0) {
            const block = root.acquire([]);
            root.showNext(block);
            block.motion = root.restMotion("next");
        }
    }

    // Each place fades from its old line to its new one, where it is.
    function crossfade(currentChanged, nextChanged) {
        const oldCurrent = currentChanged ? root.currentBlock : null;
        const oldNext = nextChanged ? root.nextBlock : null;
        const busy = [oldCurrent, oldNext];
        if (oldCurrent) {
            root.freeze(oldCurrent, "gone", root.clockMs + root.crossfadeMs);
        }
        if (oldNext) {
            root.freeze(oldNext, "gone", root.clockMs + root.crossfadeMs);
        }
        if (currentChanged && root.lyricText.length > 0) {
            const block = root.acquire(busy);
            root.showCurrent(block);
            root.fadeIn(block, "current");
        }
        if (nextChanged && root.nextText.length > 0) {
            const block = root.acquire(busy);
            root.showNext(block);
            root.fadeIn(block, "next");
        }
        if (oldCurrent) {
            root.drop(oldCurrent);
        }
        if (oldNext) {
            root.drop(oldNext);
        }
    }

    // The current line ends in an interlude: it leaves like a sung line, and
    // the next one stays where it is.
    function endInInterlude() {
        const old = root.currentBlock;
        if (old) {
            const travel = old.height + root.gap;
            root.freeze(old, "done", root.clockMs + root.exitMs);
            root.exit(old, travel);
        }
    }

    // The push up: the sung line leaves upwards; the next line on show, when
    // it is the new current one, rises into its place, and otherwise the
    // new line comes from one line box below; the new next line emerges.
    // Every place is filled before anything is set moving, so the travel is
    // measured to where the new layout puts the current place.
    function pushUp() {
        const old = root.currentBlock;
        const candidate = root.nextBlock;
        const promoted = candidate && root.lyricText.length > 0
            && candidate.lyricText === root.lyricText ? candidate : null;
        const stale = candidate && !promoted ? candidate : null;
        const promotedFrom = promoted ? promoted.y : 0;
        const oldHeight = old ? old.height : 0;
        if (old) {
            root.freeze(old, "done", root.clockMs + root.exitMs);
        }
        if (stale) {
            root.freeze(stale, "gone", root.clockMs + root.crossfadeMs);
        }
        root.nextBlock = null;
        const busy = [old, stale, promoted];
        let entering = null;
        if (promoted) {
            root.showCurrent(promoted);
        } else if (root.lyricText.length > 0) {
            entering = root.acquire(busy);
            root.showCurrent(entering);
            busy.push(entering);
        }
        let emerging = null;
        if (root.nextText.length > 0) {
            emerging = root.acquire(busy);
            root.showNext(emerging);
        }
        // The two lines move together, the old one ending a gap above the
        // new one wherever the new layout puts it.
        const travel = promoted ? promotedFrom - root.currentY : oldHeight + root.gap;
        if (old) {
            root.exit(old, travel);
        }
        if (stale) {
            root.drop(stale);
        }
        if (promoted) {
            root.promote(promoted, promotedFrom);
        }
        if (entering) {
            root.enterFromBelow(entering);
        }
        if (emerging) {
            root.emerge(emerging);
        }
    }

    // The first lines go straight to their places, once the pool exists:
    // the Repeater's blocks and this item's own completion come in no fixed
    // order.
    property bool placed: false
    function placeInitially() {
        if (!root.placed && pool.count === pool.model) {
            root.placed = true;
            root.snap(true, true);
        }
        return root.placed;
    }
    Component.onCompleted: {
        root.shownText = root.lyricText;
        root.shownSecondaryLyric = root.secondaryLyricText;
        root.shownWords = root.words;
        root.shownNextText = root.nextText;
        root.shownLineIndex = root.currentLineIndex;
        root.shownNextLineIndex = root.nextLineIndex;
        root.shownFingerprint = root.fingerprint;
        root.shownLyrics = root.showingLyrics;
        root.placeInitially();
    }
    onLyricTextChanged: switchTimer.restart()
    onSecondaryLyricTextChanged: switchTimer.restart()
    onWordsChanged: switchTimer.restart()
    onNextTextChanged: switchTimer.restart()
    onCurrentLineIndexChanged: switchTimer.restart()
    onNextLineIndexChanged: switchTimer.restart()

    // Do not "fix" this delay into a synchronous call. It means the word
    // glyphs clear one event-loop turn after LyricsView's clock has already
    // stopped -- clock first, glyphs a turn later -- and that is the safe
    // ordering of the two. The reverse would be glyphs gone while the clock
    // still runs, i.e. "hidden but still ticking", which is precisely the
    // state the word-by-word toggle exists to make unreachable (DESIGN.md
    // decision 38 and the toggle's entry in decision 69). It also gathers
    // the current and the next line, which LyricSource announces with two
    // signals, into one switch.
    Timer {
        id: switchTimer
        interval: 0
        onTriggered: root.switchLine()
    }

    // Only the top edge of the lyric area is clipped, so a sung line leaving
    // upwards does not draw over the track info; the bottom spills freely
    // when the group does not fit (decision 80), and the particle layer
    // below is outside this altogether.
    Item {
        id: stage
        readonly property real margin: 10000
        x: -stage.margin
        width: root.width + 2 * stage.margin
        height: root.height + stage.margin
        clip: true

        Repeater {
            id: pool
            model: 6
            onCountChanged: root.placeInitially()

            delegate: PooledBlock {
                id: block

                motion: root.restMotion("current")
                slideOffset: root.valueAt(block.motion.slide, root.clockMs)
                grow: root.valueAt(block.motion.grow, root.clockMs)
                veil: root.valueAt(block.motion.veil, root.clockMs)
                rise: root.valueAt(block.motion.rise, root.clockMs)
                fall: root.valueAt(block.motion.fall, root.clockMs)

                x: stage.margin
                y: (block.role === "current" ? root.currentY
                    : block.role === "next" ? root.nextY
                    : block.frozenBase) + block.slideOffset
                z: block.role === "current" ? 3 : block.role === "next" ? 2 : block.role === "done" ? 1 : 0
                width: root.width
                visible: block.role !== "free"
                transformOrigin: Item.Top
                scale: root.valueAt(block.motion.scale, root.clockMs)
                opacity: root.valueAt(block.motion.opacity, root.clockMs)

                nextLineColor: Qt.rgba(root.unsungColor.r, root.unsungColor.g, root.unsungColor.b,
                                       root.unsungColor.a * 0.6)
                nextLineStrokeColor: Qt.rgba(root.strokeColor.r, root.strokeColor.g, root.strokeColor.b,
                                             root.strokeColor.a * 0.6)
                nextLineShare: 1 - block.grow
                secondaryLyricOpacity: root.valueAt(block.motion.secondary, root.clockMs)
                // Screen pixels at the block's scale, into its own.
                blurSigma: (root.nextLineBlur * block.veil
                            + root.fontScale * (root.emergeBlur * block.rise + root.exitBlur * block.fall))
                    / Math.max(0.01, block.scale)
                blurPadding: root.blurPadding

                textColor: root.textColor
                secondaryLyricColor: root.secondaryLyricColor
                strokeEnabled: root.strokeEnabled
                strokeColor: root.strokeColor
                fontFamily: root.fontFamily
                fontSize: root.fontSize
                fontWeight: root.fontWeight
                secondaryLyricFontFamily: root.secondaryLyricFontFamily
                secondaryLyricFontSize: root.secondaryLyricFontSize
                secondaryLyricFontWeight: root.secondaryLyricFontWeight
                secondaryLyricFontItalic: root.secondaryLyricFontItalic
                overflowMode: root.overflowMode
                // A block leaving goes on holding its word glyphs while it
                // fades, which looks wrong in a screenshot and is not.
                // positionMs is past the line's last word by then, so every
                // glyph is in the sung colour; the last word may still be
                // coming down -- its spring release (LyricLine, decision 73)
                // runs on for ~600 ms past endMs -- and that is the intended
                // look, the outgoing line settling as it goes. A block
                // without words has nothing that moves with the position.
                positionMs: block.words.length > 0 ? root.positionMs : 0
                unsungColor: root.unsungColor
                activeColor: root.activeColor
                sungColor: root.sungColor
                liftEnabled: root.liftEnabled
                liftEm: root.liftEm
                brightnessEnabled: root.brightnessEnabled
                brightnessStrength: root.brightnessStrength
                blurGlowEnabled: root.blurGlowEnabled
                lineHeightFactor: root.lineHeightFactor
                // Only the current line, and its line only: particles of a
                // line switched away from were detached at the switch.
                particlesWanted: block.role === "current" && root.particlesEnabled
            }
        }
    }

    // Every particle of the widget in one scene-graph node. Declared after
    // the blocks so that it draws over them. The line being sung is followed
    // as its block slides, fades and grows in and as the marquee scrolls it;
    // lines already switched away from are kept inside the layer itself,
    // since a block lets go of its words when it has left and a particle
    // lives 2050 ms.
    WordParticleLayer {
        id: particles
        objectName: "wordParticles"
        readonly property PooledBlock followed: root.currentBlock
        x: root.particleArea.x
        y: root.particleArea.y
        width: root.particleArea.width
        height: root.particleArea.height
        clip: true
        active: root.particlesEnabled
        fingerprint: root.particleFingerprint
        positionMs: root.positionMs
        fontSize: root.fontSize
        color: root.particleColor
        line: particles.followed ? particles.followed.particleLine : null
        lineOrigin: particles.followed
            ? Qt.point(stage.x + particles.followed.x - particles.x,
                       stage.y + particles.followed.y - particles.followed.slideOffset - particles.y)
            : Qt.point(0, 0)
        lineOffset: particles.followed
            ? Qt.point(particles.followed.particleScrollOffset * particles.followed.scale,
                       particles.followed.slideOffset)
            : Qt.point(0, 0)
        lineOpacity: particles.followed ? particles.followed.opacity : 1
        lineScale: particles.followed ? particles.followed.scale : 1
        lineScaleOrigin: Qt.point(root.width / 2, 0)
    }
}
