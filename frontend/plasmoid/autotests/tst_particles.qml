import QtQuick
import QtTest
import io.github.swim233.lyrics
import "../package/contents/ui" as LyricsUi

// DESIGN.md decision 77, the QML half: which lines spawn particles, which
// snapshots the layer keeps, the frame clock they extend and the colour they
// take. The particles themselves are never seen here -- the offscreen
// platform runs the software backend, which draws no custom geometry node --
// so the maths is tested in tst_wordparticles and the drawing was checked by
// hand under xvfb-run with real GL.
//
// Every top-level object below is a Component, for the load-time reason
// tst_appearance.qml's header gives.
TestCase {
    name: "Particles"
    when: windowShown

    // tst_appearance.qml's stand-in for LyricSource, with a fingerprint.
    Component {
        id: fakeSourceComponent
        QtObject {
            property bool serviceAvailable: true
            property bool stale: false
            property string lyricState: "ok"
            property string playbackStatus: "Playing"
            property string trackTitle: "Title"
            property string trackArtists: "Artist"
            property string fingerprint: "track-1"
            property string currentText: "abcd"
            property string currentTranslation: ""
            property string currentRomanization: ""
            property var currentWords: []
            property var currentSyntheticWords: []
            property real positionMs: 0
            function lyricPositionMs() { return positionMs; }
        }
    }

    Component {
        id: lyricsViewComponent
        LyricsUi.LyricsView {
            width: 400
            height: 300
            animationMode: "none"
        }
    }

    Component {
        id: lyricLineComponent
        LyricsUi.LyricLine {
            width: 400
            fontSize: 40
            lineText: "abcd"
        }
    }

    // Two words; the last starts at 1400, so the line's last particle is
    // born in [1400, 1490) and goes out in [3450, 3540).
    readonly property var lineA: [
        { startMs: 1000, endMs: 1400, text: "ab" },
        { startMs: 1400, endMs: 1800, text: "cd" }
    ]
    readonly property var lineB: [
        { startMs: 2000, endMs: 2300, text: "ef" },
        { startMs: 2300, endMs: 2600, text: "gh" }
    ]

    function findAll(item, predicate, found) {
        const out = found || [];
        const kids = item ? (item.children || []) : [];
        for (let i = 0; i < kids.length; ++i) {
            if (predicate(kids[i])) {
                out.push(kids[i]);
            }
            findAll(kids[i], predicate, out);
        }
        return out;
    }
    function layerOf(view) {
        return findAll(view, o => o.objectName === "wordParticles")[0];
    }
    function lyricOf(view) {
        return findAll(view, o => o.particlesAliveUntilMs !== undefined && o.shownWords !== undefined)[0];
    }
    // Both lines of both blocks: LyricLine is the one with envelopesAnimate.
    function linesOf(view) {
        return findAll(view, o => o.envelopesAnimate !== undefined && o.particleLine !== undefined);
    }
    function current(layer) {
        return layer.describeSnapshots().filter(s => s.current)[0];
    }
    // Moves the lyric position by hand, as LyricsView's frame clock would.
    function step(view, source, ms) {
        source.positionMs = ms;
        view.syncLyricPosition();
    }
    function createView(properties) {
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentText: "abcd", currentWords: lineA, positionMs: 1500 });
        const view = createTemporaryObject(lyricsViewComponent, this,
            Object.assign({ source: source, panelMode: true }, properties || {}));
        verify(view !== null);
        return { source: source, view: view, layer: layerOf(view), lyric: lyricOf(view) };
    }
    // A switch to a line without words: an interlude, a blank line, the end
    // of the song. AnimatedLyric switches a turn later.
    function switchToPlainLine(t, text) {
        t.source.currentWords = [];
        t.source.currentText = text || "interlude";
        tryVerify(() => t.lyric.shownText === (text || "interlude"));
    }
    function switchTo(t, text, words) {
        t.source.currentWords = words;
        t.source.currentText = text;
        tryVerify(() => t.lyric.shownText === text);
    }

    function wordsOf(texts, start, duration) {
        return texts.map((text, i) => ({ startMs: start + duration * i,
                                         endMs: start + duration * (i + 1), text: text }));
    }
    function latinWords(prefix, count) {
        const out = [];
        for (let i = 0; i < count; ++i) {
            out.push(prefix + i + " ");
        }
        return out;
    }

    // "" when the layer measured the current line exactly as it is drawn:
    // each word's x and resting baseline -- the glyph's own, taken from the
    // delegate the lift does not move -- plus what the line is following
    // right now, the ink at the glyphs' own font without trailing
    // whitespace, and the CJK ascent at that font. Font metrics are asked of
    // the glyphs, never assumed, so this holds on any machine.
    function currentBlock(view) {
        const shown = lyricOf(view).shownText;
        return findAll(view, o => o.slideOffset !== undefined && o.lyricText === shown)[0];
    }
    function drawnMismatch(view, layer) {
        const line = layer.describeLine();
        const block = currentBlock(view);
        const glyphs = findAll(block, o => o.objectName === "lyricWord");
        if (!line.words || line.words.length !== glyphs.length) {
            return "measured " + (line.words ? line.words.length : "none") + " of " + glyphs.length;
        }
        const font = glyphs[0].font;
        const cjk = createTemporaryObject(textMetricsComponent, this, { font: font, text: "国" });
        const ascent = cjk.tightBoundingRect.height > 0 ? -cjk.tightBoundingRect.y
            : createTemporaryObject(fontMetricsComponent, this, { font: font }).ascent;
        if (Math.abs(line.ascent - ascent) > 0.01) {
            return "ascent " + line.ascent + " against " + ascent;
        }
        for (let i = 0; i < glyphs.length; ++i) {
            const glyph = glyphs[i];
            const drawn = glyph.parent.mapToItem(layer, 0, glyph.baselineOffset);
            const x = line.x + line.words[i].x + layer.lineOffset.x;
            const baseline = line.y + line.words[i].baseline + layer.lineOffset.y;
            if (Math.abs(x - drawn.x) > 0.01 || Math.abs(baseline - drawn.y) > 0.01) {
                return "word " + i + " measured at " + x + "," + baseline + ", drawn at " + drawn.x + "," + drawn.y;
            }
            const ink = createTemporaryObject(textMetricsComponent, this,
                { font: glyph.font, text: glyph.text.replace(/\s+$/, "") }).advanceWidth;
            if (Math.abs(line.words[i].ink - ink) > 0.01) {
                return "word " + i + " ink " + line.words[i].ink + " against " + ink;
            }
        }
        return "";
    }
    // Settled: the Row has placed every word of the current block.
    function waitForLayout(view) {
        tryVerify(() => {
            const glyphs = findAll(currentBlock(view), o => o.objectName === "lyricWord");
            return glyphs.length > 0 && glyphs.every((g, i) => g.baselineOffset > 0
                && (i === 0 || g.parent.x > glyphs[i - 1].parent.x));
        });
        wait(50);
    }
    function measuredView(properties, texts, positionMs) {
        const source = createTemporaryObject(fakeSourceComponent, this, {
            currentText: texts.join(""), currentWords: wordsOf(texts, 1000, 100),
            positionMs: positionMs === undefined ? 1050 : positionMs });
        const view = createTemporaryObject(lyricsViewComponent, this,
            Object.assign({ source: source, width: 800 }, properties));
        const t = { source: source, view: view, layer: layerOf(view), lyric: lyricOf(view) };
        tryCompare(t.layer, "snapshotCount", 1);
        waitForLayout(view);
        return t;
    }

    // Lines with the same number of words and different widths, in every
    // overflow mode word-by-word renders: measured anew each time, never
    // from the last line of the same length.
    function test_eachLineIsMeasuredAsDrawn_data() {
        return [
            { tag: "fit-panel", props: { panelMode: true, overflowMode: "fit" } },
            { tag: "fit-desktop", props: { panelMode: false, overflowMode: "fit" } },
            { tag: "elide-desktop", props: { panelMode: false, overflowMode: "elide" } },
            { tag: "marquee-panel", props: { panelMode: true, overflowMode: "marquee" } },
            { tag: "marquee-desktop", props: { panelMode: false, overflowMode: "marquee" } },
        ];
    }
    function test_eachLineIsMeasuredAsDrawn(data) {
        const narrow = latinWords("i", 10);
        const wide = latinWords("Wm", 10);
        const t = measuredView(data.props, narrow);
        compare(drawnMismatch(t.view, t.layer), "", "first line");
        step(t.view, t.source, 50000);
        switchTo(t, wide.join(""), wordsOf(wide, 40000, 100));
        waitForLayout(t.view);
        compare(drawnMismatch(t.view, t.layer), "", "a wider line of as many words");
        step(t.view, t.source, 90000);
        switchTo(t, narrow.join(""), wordsOf(narrow, 80000, 100));
        waitForLayout(t.view);
        compare(drawnMismatch(t.view, t.layer), "", "back to the narrow one");
        const cjk = "粒子从每个字上飘起来".split("");
        step(t.view, t.source, 130000);
        switchTo(t, cjk.join(""), wordsOf(cjk, 120000, 100));
        waitForLayout(t.view);
        compare(drawnMismatch(t.view, t.layer), "", "CJK");
        // Exactly as wide as the last one: the row neither grows nor moves,
        // and only the Row's own report of its layout says the words are in
        // place.
        const same = "唱到每个词时从字上飘".split("");
        step(t.view, t.source, 170000);
        switchTo(t, same.join(""), wordsOf(same, 160000, 100));
        waitForLayout(t.view);
        compare(drawnMismatch(t.view, t.layer), "", "CJK of the same width");
    }

    // "fit" shrinks the glyphs: the ink and ascent are measured at the size
    // they are drawn at, not at fontSize.
    function test_aShrunkLineIsMeasuredAtItsDrawnSize() {
        // About 1.3 times the room there is, whatever this machine's font:
        // shrunk, and still inside the 0.6 floor that keeps it word-by-word.
        const metrics = createTemporaryObject(textMetricsComponent, this,
            { font: Qt.font({ pixelSize: 34 }) });
        const texts = [];
        do {
            texts.push("Wide" + texts.length + " ");
            metrics.text = texts.join("");
        } while (metrics.advanceWidth < 1.3 * 760);
        const t = measuredView({ panelMode: false, overflowMode: "fit" }, texts);
        const glyphs = findAll(t.view, o => o.objectName === "lyricWord");
        verify(glyphs[0].font.pixelSize < t.view.fontSize, glyphs[0].font.pixelSize);
        compare(drawnMismatch(t.view, t.layer), "");
    }

    function test_aHeavyWeightIsMeasuredAsDrawn() {
        const t = measuredView({ panelMode: false, fontWeight: Font.Black }, latinWords("Wm", 8));
        compare(drawnMismatch(t.view, t.layer), "");
    }

    // The block moves after the line was captured: a translation appears and
    // re-centres it, or the widget grows.
    function test_aMovedLineIsFollowed() {
        const t = measuredView({ panelMode: false }, "粒子从每个字上飘起来".split(""));
        compare(drawnMismatch(t.view, t.layer), "", "before");
        t.source.currentTranslation = "a translation";
        tryVerify(() => t.lyric.shownSecondaryLyric === "a translation");
        waitForLayout(t.view);
        compare(drawnMismatch(t.view, t.layer), "", "after the translation");
        t.view.height = 420;
        waitForLayout(t.view);
        compare(drawnMismatch(t.view, t.layer), "", "after a resize");
    }

    // Mid slide-in the line is where its block is drawn, the slide counted
    // once.
    function test_aSlidingLineIsMeasuredWhereItIsDrawn() {
        const a = "粒子从每个字上飘起来".split("");
        const t = measuredView({ panelMode: false, animationMode: "slide" }, a);
        const b = "唱到每个词时从字上飘".split("");
        step(t.view, t.source, 50000);
        switchTo(t, b.join(""), wordsOf(b, 40000, 100));
        tryVerify(() => t.layer.lineOffset.y > 1 && drawnMismatch(t.view, t.layer) === "", 1000,
                  drawnMismatch(t.view, t.layer));
        verify(t.layer.lineOffset.y > 1);
        compare(drawnMismatch(t.view, t.layer), "");
    }

    // A line re-captured while a word is lifted still takes the resting
    // baseline: births come from the glyph, not from where the lift has it.
    function test_aLiftedWordIsMeasuredAtRest() {
        const texts = "粒子从每个字上飘起来".split("");
        // Mid-way through the fourth word: lifted and holding.
        const t = measuredView({ panelMode: false }, texts, 1350);
        const glyphs = findAll(t.view, o => o.objectName === "lyricWord");
        tryVerify(() => glyphs[3].y < -1, 1000, String(glyphs[3].y));
        // Anything that re-centres the row re-captures the line.
        t.view.width = 780;
        waitForLayout(t.view);
        verify(glyphs[3].y < -1);
        compare(drawnMismatch(t.view, t.layer), "");
    }

    // How many times the layer took a new line over one switch to a line of
    // `count` CJK words, and over the 100 frames after it.
    function capturesOf(t, count, at) {
        const texts = "唱到每个词时从字上飘起粒子一颗颗光点".repeat(20).split("").slice(0, count);
        let captures = 0;
        const counter = () => { ++captures; };
        t.layer.lineChanged.connect(counter);
        step(t.view, t.source, at + 10);
        switchTo(t, texts.join(""), wordsOf(texts, at, 20));
        waitForLayout(t.view);
        const perSwitch = captures;
        captures = 0;
        for (let i = 0; i < 100; ++i) {
            step(t.view, t.source, at + 20 + i * 16);
        }
        wait(50);
        t.layer.lineChanged.disconnect(counter);
        return { perSwitch: perSwitch, perFrames: captures };
    }

    // The line is captured a handful of times per switch -- when its words
    // or text change and once the Row has laid it out -- however many words
    // it has, and never per frame. It used to be once per word placed. Off,
    // never at all.
    function test_aSwitchCapturesTheLineAFewTimesAndAFrameNever() {
        const t = measuredView({ panelMode: false, overflowMode: "marquee" }, latinWords("w", 3));
        const few = capturesOf(t, 10, 100000);
        const many = capturesOf(t, 80, 200000);
        verify(few.perSwitch >= 1 && few.perSwitch <= 6, few.perSwitch);
        verify(many.perSwitch <= few.perSwitch + 1, many.perSwitch + " against " + few.perSwitch);
        compare(few.perFrames, 0);
        compare(many.perFrames, 0);

        t.view.wordParticles = false;
        const off = capturesOf(t, 80, 300000);
        compare(off.perSwitch, 0);
        compare(off.perFrames, 0);
        compare(t.layer.line, null);
    }

    // The lyric slot showing anything but lyrics drops every particle; a
    // pause keeps them where they are.
    function test_particlesGoWhenTheSlotStopsShowingLyrics_data() {
        return [
            { tag: "stopped", property: "playbackStatus", value: "Stopped" },
            { tag: "searching", property: "lyricState", value: "searching" },
            { tag: "not-found", property: "lyricState", value: "not-found" },
            { tag: "no-lyric", property: "lyricState", value: "no-lyric" },
            { tag: "network-error", property: "lyricState", value: "network-error" },
            { tag: "filtered", property: "lyricState", value: "filtered" },
            { tag: "no-title", property: "trackTitle", value: "" },
            { tag: "service-gone", property: "serviceAvailable", value: false },
            { tag: "stale", property: "stale", value: true },
        ];
    }
    function test_particlesGoWhenTheSlotStopsShowingLyrics(data) {
        const t = createView();
        tryCompare(t.layer, "snapshotCount", 1);
        switchToPlainLine(t);
        compare(t.layer.snapshotCount, 1);

        t.source.playbackStatus = "Paused";
        compare(t.layer.snapshotCount, 1);
        compare(t.view.wordClockRunning, false);
        t.source.playbackStatus = "Playing";

        t.source[data.property] = data.value;
        compare(t.view.showingLyrics, false);
        compare(t.layer.snapshotCount, 0);
        compare(t.layer.particlesAliveUntilMs, -Infinity);
        compare(t.view.wordClockRunning, false);
    }

    function test_theLineBeingSungIsCapturedWithItsLastBirth() {
        const t = createView();
        verify(t.layer !== undefined);
        tryCompare(t.layer, "snapshotCount", 1);
        const live = current(t.layer);
        compare(live.startMs, 1000);
        compare(live.text, "abcd");
        verify(live.births.length >= 2 && live.births.length <= 6, live.births.length);
        verify(t.layer.particlesAliveUntilMs >= 1400 + 2050, t.layer.particlesAliveUntilMs);
        verify(t.layer.particlesAliveUntilMs < 1490 + 2050, t.layer.particlesAliveUntilMs);
        compare(t.lyric.particlesAliveUntilMs, t.layer.particlesAliveUntilMs);
    }

    // The layer covers the whole widget, so particles have room to rise above
    // the line; the words' own clipper would leave them a few pixels.
    function test_theLayerCoversTheWholeView() {
        const t = createView({ panelMode: false });
        tryVerify(() => {
            const p = t.layer.mapToItem(t.view, 0, 0);
            return Math.abs(p.x) < 0.01 && Math.abs(p.y) < 0.01;
        });
        compare(t.layer.width, t.view.width);
        compare(t.layer.height, t.view.height);
        verify(t.layer.clip);
    }

    Component {
        id: fontMetricsComponent
        FontMetrics {}
    }

    // Every birth point lies on a word's glyphs as laid out, mapped into the
    // layer: across the middle 80% of that word's ink and in the top 35% of
    // the line's CJK ascent. Measured here with the glyphs' own font, never
    // compared against pixel values -- the CI container has no CJK font and
    // draws boxes of whatever size its fallback has.
    function test_particlesAreBornOnTheGlyphs() {
        // A Latin word first: it sits on a baseline of its own, apart from
        // the CJK words after it.
        const texts = ["up "].concat("粒子从每个字上飘起来一颗颗光点".split(""));
        const words = texts.map((text, i) => ({ startMs: 1000 + 30 * i, endMs: 1030 + 30 * i, text: text }));
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentText: texts.join(""), currentWords: words, positionMs: 1500 });
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: true, fontSize: 40, width: 800 });
        const layer = layerOf(view);
        compare(layer.fontSize, 40);
        tryCompare(layer, "snapshotCount", 1);
        const glyphs = findAll(view, o => o.objectName === "lyricWord");
        compare(glyphs.length, texts.length);
        const font = glyphs[0].font;
        const cjk = createTemporaryObject(textMetricsComponent, this, { font: font, text: "国" });
        const ascent = cjk.tightBoundingRect.height > 0
            ? -cjk.tightBoundingRect.y
            : createTemporaryObject(fontMetricsComponent, this, { font: font }).ascent;
        verify(ascent > 0);
        const inks = glyphs.map(glyph => createTemporaryObject(textMetricsComponent, this,
            { font: font, text: glyph.text.replace(/\s+$/, "") }).advanceWidth);
        function misplaced() {
            const bands = [];
            for (let i = 0; i < glyphs.length; ++i) {
                // Nothing is lifted in a panel, so the glyphs are at rest.
                // Each word has a baseline of its own.
                const glyph = glyphs[i];
                const left = glyph.mapToItem(layer, 0, 0).x;
                const baseline = glyph.mapToItem(layer, 0, glyph.baselineOffset).y;
                bands.push({ left: left + 0.1 * inks[i], right: left + 0.9 * inks[i],
                             top: baseline - ascent, bottom: baseline - 0.65 * ascent });
            }
            const births = current(layer).births;
            if (births.length < texts.length) {
                return "too few: " + births.length;
            }
            for (const p of births) {
                const band = bands.find(b => p.x >= b.left - 0.01 && p.x <= b.right + 0.01);
                if (!band) {
                    return "x " + p.x + " outside " + JSON.stringify(bands);
                }
                if (p.y < band.top - 0.01 || p.y > band.bottom + 0.01) {
                    return "y " + p.y + " outside " + JSON.stringify(band);
                }
            }
            return "";
        }
        // Laid out first, then checked once: before the Row has placed the
        // words, births and glyphs would agree just as well at x 0.
        tryVerify(() => glyphs.every((g, i) => g.baselineOffset > 0
            && (i === 0 || g.parent.x > glyphs[i - 1].parent.x)));
        compare(misplaced(), "");
    }

    Component {
        id: textMetricsComponent
        TextMetrics {}
    }

    // Measured without the trailing whitespace a Latin word carries: the
    // delegate is as wide as the spaces too, the ink is not.
    function test_trailingSpaceIsNotInk() {
        const spaces = " ".repeat(30);
        const source = createTemporaryObject(fakeSourceComponent, this, {
            currentText: "ab cd" + spaces, positionMs: 1500,
            currentWords: [{ startMs: 1000, endMs: 1400, text: "ab " },
                           { startMs: 1400, endMs: 1800, text: "cd" + spaces }] });
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: true, fontSize: 40 });
        const layer = layerOf(view);
        tryCompare(layer, "snapshotCount", 1);
        const glyphs = findAll(view, o => o.objectName === "lyricWord");
        compare(glyphs.length, 2);
        const last = glyphs[1].parent;
        const ink = createTemporaryObject(textMetricsComponent, this,
            { font: glyphs[1].font, text: "cd" });
        verify(last.width > 2 * ink.advanceWidth, last.width + " " + ink.advanceWidth);
        tryVerify(() => last.x > glyphs[0].parent.x);
        const right = last.mapToItem(layer, 0.9 * ink.advanceWidth, 0).x;
        verify(current(layer).births.every(p => p.x <= right + 0.01), JSON.stringify(current(layer).births));
    }

    // DESIGN.md decision 77's clock extension: switching to a line without
    // words must not freeze the particles in mid-air.
    function test_particlesKeepTheClockRunningUntilTheLastOneGoesOut() {
        const t = createView();
        tryCompare(t.layer, "snapshotCount", 1);
        const until = t.layer.particlesAliveUntilMs;
        compare(t.view.wordClockRunning, true);

        t.source.currentWords = [];
        t.source.currentText = "interlude";
        compare(t.view.effectiveWords.length, 0);
        // AnimatedLyric switches a turn later; the line is still the current
        // one until then and must hold the clock across that turn.
        compare(t.view.wordClockRunning, true);
        tryVerify(() => t.lyric.shownText === "interlude");
        compare(t.layer.snapshotCount, 1);
        verify(!current(t.layer));
        compare(t.layer.particlesAliveUntilMs, until);
        compare(t.view.wordClockRunning, true);

        step(t.view, t.source, until - 1);
        compare(t.view.wordClockRunning, true);
        compare(t.layer.snapshotCount, 1);

        // Paused in mid-flight: the clock stops, the particles stay.
        t.source.playbackStatus = "Paused";
        compare(t.view.wordClockRunning, false);
        compare(t.layer.snapshotCount, 1);
        t.source.playbackStatus = "Playing";
        compare(t.view.wordClockRunning, true);

        step(t.view, t.source, until);
        compare(t.layer.snapshotCount, 0);
        compare(t.layer.particlesAliveUntilMs, -Infinity);
        compare(t.view.wordClockRunning, false);
    }

    // A line without words before any particle ever existed does not arm
    // the clock: -Infinity makes the new clause false.
    function test_noParticlesMeansDecision38sClock() {
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentText: "plain", currentWords: [], positionMs: -5000 });
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: true });
        compare(layerOf(view).particlesAliveUntilMs, -Infinity);
        compare(view.wordClockRunning, false);
    }

    function test_particlesOffLeavesNothingAndDoesNotHoldTheClock() {
        const t = createView();
        tryCompare(t.layer, "snapshotCount", 1);
        t.view.wordParticles = false;
        compare(t.layer.active, false);
        compare(t.layer.snapshotCount, 0);
        compare(t.layer.particlesAliveUntilMs, -Infinity);
        t.source.currentWords = [];
        t.source.currentText = "interlude";
        compare(t.view.wordClockRunning, false);
        tryVerify(() => t.lyric.shownText === "interlude");
        compare(t.view.wordClockRunning, false);

        // Back on, and a line with words spawns again.
        t.view.wordParticles = true;
        switchTo(t, "abcd", lineA);
        tryCompare(t.layer, "snapshotCount", 1);

        // Off with particles already detached and in the air: those go too.
        switchToPlainLine(t);
        compare(t.layer.snapshotCount, 1);
        t.view.wordParticles = false;
        compare(t.layer.snapshotCount, 0);
        compare(t.view.wordClockRunning, false);
    }

    function test_wordByWordOffClearsTheParticlesAndTheClock() {
        const t = createView();
        tryCompare(t.layer, "snapshotCount", 1);
        switchToPlainLine(t);
        compare(t.layer.snapshotCount, 1);
        compare(t.view.wordClockRunning, true);
        t.view.wordByWord = false;
        compare(t.layer.snapshotCount, 0);
        compare(t.view.wordClockRunning, false);

        t.view.wordByWord = true;
        switchTo(t, "abcd", lineA);
        tryCompare(t.layer, "snapshotCount", 1);
        t.view.wordByWord = false;
        compare(t.layer.snapshotCount, 0);
        compare(t.view.wordClockRunning, false);
    }

    // "wrap" renders the whole line, so it has no words to rise from.
    function test_wrapSpawnsNothing() {
        const t = createView({ overflowMode: "wrap" });
        wait(50);
        compare(t.layer.snapshotCount, 0);
        switchToPlainLine(t);
        compare(t.view.wordClockRunning, false);

        t.view.overflowMode = "fit";
        switchTo(t, "abcd", lineA);
        tryCompare(t.layer, "snapshotCount", 1);
        t.view.overflowMode = "wrap";
        compare(t.layer.snapshotCount, 0);
    }

    function test_noParticlesWhileAnimationsAreOff() {
        const line = createTemporaryObject(lyricLineComponent, this,
            { words: lineA, particlesWanted: true });
        tryVerify(() => line.particleLine !== null);
        compare(line.particleLine.startMs, 1000);
        compare(line.particleLine.text, "abcd");
        compare(line.particleLine.words.length, 2);
        compare(line.particleLine.words[1].text, "cd");
        tryVerify(() => line.particleLine.words[1].item.x > line.particleLine.words[0].item.x);
        line.envelopesAnimate = false;
        compare(line.particleLine, null);
        line.envelopesAnimate = true;
        verify(line.particleLine !== null);
        // Not wanted -- a previous block, or particles off -- is null too.
        line.particlesWanted = false;
        compare(line.particleLine, null);

        const t = createView();
        tryCompare(t.layer, "snapshotCount", 1);
        for (const l of linesOf(t.view)) {
            l.envelopesAnimate = false;
        }
        compare(t.layer.snapshotCount, 0);
        switchTo(t, "efgh", lineB);
        wait(50);
        compare(t.layer.snapshotCount, 0);
    }

    // Only the lyric's own line spawns; the secondary lyrics carry no words
    // and are never read.
    function test_theSecondaryLyricsNeverSpawn() {
        const t = createView();
        t.source.currentTranslation = "translation";
        tryVerify(() => t.lyric.shownSecondaryLyric === "translation");
        tryCompare(t.layer, "snapshotCount", 1);
        compare(current(t.layer).text, "abcd");
        const withLines = linesOf(t.view).filter(l => l.particleLine !== null);
        compare(withLines.length, 1);
        compare(withLines[0].lineText, "abcd");
    }

    function test_particleColourFollowsTheCurrentWordColourAtFullOpacity() {
        const t = createView();
        t.view.wordActiveColor = "#e6336699";
        verify(Qt.colorEqual(t.view.effectiveWordParticleColor, "#336699"));
        compare(t.view.effectiveWordParticleColor.a, 1);
        verify(Qt.colorEqual(t.layer.color, "#336699"));

        // The separate colour applies only while its switch is on, and at
        // full opacity too.
        t.view.wordParticleColor = "#40ff8800";
        verify(Qt.colorEqual(t.view.effectiveWordParticleColor, "#336699"));
        t.view.wordParticleColorEnabled = true;
        verify(Qt.colorEqual(t.view.effectiveWordParticleColor, "#ff8800"));
        verify(Qt.colorEqual(t.layer.color, "#ff8800"));
        t.view.wordParticleColorEnabled = false;
        verify(Qt.colorEqual(t.layer.color, "#336699"));
    }

    // A theme switch fades the particle colour like every other colour.
    function test_particleColourFadesWithTheTheme() {
        const t = createView({ animateColors: true, colorTransitionMs: 100,
                               wordParticleColorEnabled: true });
        t.view.wordParticleColor = "#1f1b16";
        verify(!Qt.colorEqual(t.view.wordParticleColor, "#1f1b16"));
        tryVerify(() => Qt.colorEqual(t.view.effectiveWordParticleColor, "#1f1b16"), 5000);

        t.view.animateColors = false;
        t.view.wordParticleColor = "#fffaf5";
        verify(Qt.colorEqual(t.view.effectiveWordParticleColor, "#fffaf5"));
    }

    function blockShowing(view, text) {
        return findAll(view, o => o.slideOffset !== undefined && o.lyricText === text)[0];
    }

    // While a line is current its particles follow the block as it slides
    // and fades in; once switched away from they stay where the block was at
    // that moment instead of following it up and out.
    function test_aSwitchedLineStaysWhereItWas() {
        const t = createView({ animationMode: "slide", panelMode: false });
        tryCompare(t.layer, "snapshotCount", 1);
        tryVerify(() => current(t.layer).offsetY === 0 && current(t.layer).opacity === 1);

        step(t.view, t.source, 2100);
        switchTo(t, "efgh", lineB);
        tryCompare(t.layer, "snapshotCount", 2);
        const kept = t.layer.describeSnapshots().filter(s => !s.current)[0];
        compare(kept.startMs, 1000);
        compare(kept.offsetY, 0);
        compare(kept.opacity, 1);

        // The new line follows its block through the slide-in, whatever point
        // of it this runs at.
        const block = blockShowing(t.view, "efgh");
        // The outgoing block still shows its words, and only the current
        // one's line is measured.
        verify(blockShowing(t.view, "abcd") !== undefined);
        const measured = linesOf(t.view).filter(l => l.particleLine !== null);
        compare(measured.length, 1);
        compare(measured[0].lineText, "efgh");
        const live = current(t.layer);
        compare(live.startMs, 2000);
        compare(live.offsetY, block.slideOffset);
        compare(live.opacity, block.opacity);
        tryVerify(() => current(t.layer).offsetY === 0 && current(t.layer).opacity === 1, 2000);

        // The outgoing block went up and out meanwhile; the kept line did not.
        const later = t.layer.describeSnapshots().filter(s => !s.current)[0];
        compare(later.offsetY, 0);
        compare(later.opacity, 1);
    }

    // A switch mid-slide freezes the line at the offset its block had then,
    // not at the one the next line's slide-in starts from.
    function test_aLineSwitchedAwayMidSlideFreezesThere() {
        const t = createView({ animationMode: "slide", panelMode: false });
        tryCompare(t.layer, "snapshotCount", 1);
        step(t.view, t.source, 2100);
        switchTo(t, "efgh", lineB);
        const start = t.lyric.height * 0.28;
        verify(start > 0);
        tryVerify(() => current(t.layer).offsetY < 0.8 * start);
        switchToPlainLine(t);
        const frozen = t.layer.describeSnapshots().filter(s => s.startMs === 2000)[0];
        verify(frozen.offsetY < 0.8 * start, frozen.offsetY);
        wait(400);
        compare(t.layer.describeSnapshots().filter(s => s.startMs === 2000)[0].offsetY, frozen.offsetY);
    }

    // Particles turned on with the marquee already scrolled and nothing
    // moving any more -- paused -- still start from the scrolled row: they
    // follow the offset the line has, not only the next change to it.
    function test_particlesTurnedOnMidScrollFollowTheScroll() {
        const words = [];
        let text = "";
        for (let i = 0; i < 30; ++i) {
            words.push({ startMs: 1000 + i * 100, endMs: 1100 + i * 100, text: "word" + i + " " });
            text += "word" + i + " ";
        }
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentText: text, currentWords: words, positionMs: 3500, playbackStatus: "Paused" });
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: true, overflowMode: "marquee", wordParticles: false });
        const layer = layerOf(view);
        const line = linesOf(view).filter(l => l.lineText === text)[0];
        tryVerify(() => line.particleScrollOffset < -100, 2000, String(line.particleScrollOffset));
        // Settled: the scroll's own animation has finished.
        wait(400);
        const scrolled = line.particleScrollOffset;
        compare(layer.snapshotCount, 0);
        view.wordParticles = true;
        tryCompare(layer, "snapshotCount", 1);
        compare(current(layer).offsetX, scrolled);
        compare(line.particleScrollOffset, scrolled);
    }

    function test_marqueeParticlesFollowTheScroll() {
        const words = [];
        let text = "";
        for (let i = 0; i < 30; ++i) {
            words.push({ startMs: 1000 + i * 100, endMs: 1100 + i * 100, text: "word" + i + " " });
            text += "word" + i + " ";
        }
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentText: text, currentWords: words, positionMs: 3500 });
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: true, overflowMode: "marquee" });
        const layer = layerOf(view);
        const line = linesOf(view).filter(l => l.particleLine !== null)[0];
        tryVerify(() => line && line.particleScrollOffset < 0);
        tryVerify(() => current(layer).offsetX === line.particleScrollOffset);
        // The row's x in the snapshot leaves the scroll out, so it is not
        // counted twice.
        compare(line.particleLine.x, 0);
    }

    // Keyed on (first word's start, text): coming back to a line replaces
    // its older copy instead of drawing every particle twice.
    function test_returningToALineReplacesItsOlderCopy() {
        const t = createView();
        tryCompare(t.layer, "snapshotCount", 1);
        step(t.view, t.source, 2100);
        switchTo(t, "efgh", lineB);
        tryCompare(t.layer, "snapshotCount", 2);

        // Seek back into line A while B's particles are not born yet.
        step(t.view, t.source, 1500);
        switchTo(t, "abcd", lineA);
        const a = t.layer.describeSnapshots().filter(s => s.startMs === 1000);
        compare(a.length, 1);
        verify(a[0].current);
        // B lies in the future now and goes at the next step.
        step(t.view, t.source, 1510);
        compare(t.layer.snapshotCount, 1);
    }

    // A switch that only changes the secondary lyrics lands on the same
    // line; the copy detach() kept must not stay beside the line being sung.
    function test_aSecondaryLyricChangeDoesNotDoubleTheParticles() {
        const t = createView();
        t.source.currentTranslation = "one";
        tryVerify(() => t.lyric.shownSecondaryLyric === "one");
        tryCompare(t.layer, "snapshotCount", 1);
        t.source.currentTranslation = "two";
        tryVerify(() => t.lyric.shownSecondaryLyric === "two");
        tryCompare(t.layer, "snapshotCount", 1);
        compare(t.layer.describeSnapshots().filter(s => s.startMs === 1000).length, 1);
    }

    Component {
        id: layerComponent
        WordParticleLayer {
            width: 400
            height: 200
            active: true
            positionMs: 1500
            line: ({
                startMs: 1000, text: "abcd", x: 20, y: 40,
                font: Qt.font({ pixelSize: 34 }),
                words: [{ startMs: 1000, endMs: 1400, text: "ab", x: 0, baseline: 30 },
                        { startMs: 1400, endMs: 1800, text: "cd", x: 40, baseline: 30 }]
            })
        }
    }

    // The layer's own half of that: when nothing about the line changes at
    // all after detach(), the copy still goes before the next frame.
    function test_aDetachThatLandsOnTheSameLineIsUndoneBeforeTheNextFrame() {
        const layer = createTemporaryObject(layerComponent, this);
        compare(layer.snapshotCount, 1);
        layer.detach();
        compare(layer.snapshotCount, 2);
        tryCompare(layer, "snapshotCount", 1);
        verify(current(layer));
    }

    // A line from QML arrives as a JS object, built anew by every
    // evaluation of LyricLine.particleLine: one with the same content is
    // still no change, delegates included.
    function test_aLineWithTheSameContentIsNoChange() {
        const layer = createTemporaryObject(layerComponent, this);
        let changes = 0;
        const counter = () => { ++changes; };
        layer.lineChanged.connect(counter);
        const build = () => ({
            startMs: 5000, text: "efgh", x: 20, y: 40, font: Qt.font({ pixelSize: 34 }),
            words: [{ startMs: 5000, endMs: 5400, text: "ef", item: layer },
                    { startMs: 5400, endMs: 5800, text: "gh", x: 40, baseline: 30 }]
        });
        layer.line = build();
        layer.line = build();
        compare(changes, 1);
        compare(layer.describeLine().startMs, 5000);
        layer.lineChanged.disconnect(counter);
    }

    // Lines overlap -- duets and backing vocals -- and the next one takes
    // over the moment it starts, so a line can be switched away from with a
    // word still to come ("ef" at 3000 here, the switch at 2000). That word
    // must not rise later from where the line was, over whatever the slot
    // shows by then, and the clock must stop within 90 + 2050 ms of the
    // switch.
    function test_aWordStillToComeWhenTheLineGoesNeverSpawns() {
        const overlapping = [
            { startMs: 1000, endMs: 1400, text: "ab" },
            { startMs: 1400, endMs: 1800, text: "cd" },
            { startMs: 3000, endMs: 3400, text: "ef" }
        ];
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentText: "abcdef", currentWords: overlapping, positionMs: 1500 });
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: true });
        const t = { source: source, view: view, layer: layerOf(view), lyric: lyricOf(view) };
        tryCompare(t.layer, "snapshotCount", 1);
        verify(t.layer.particlesAliveUntilMs >= 3000 + 2050);
        step(view, source, 2000);
        switchToPlainLine(t, "next line");
        compare(t.layer.snapshotCount, 1);
        verify(t.layer.particlesAliveUntilMs <= 2000 + 90 + 2050, t.layer.particlesAliveUntilMs);
        verify(t.layer.describeSnapshots()[0].birthTimes.every(ms => ms < 3000));
        step(view, source, 2000 + 90 + 2050);
        compare(view.wordClockRunning, false);
        compare(t.layer.snapshotCount, 0);
    }

    // QQ often ends a line before its last word ends: the slot empties while
    // that held last note is still being sung, 50 ms into it here. The word
    // has started, so all of its particles rise, the ones its 90 ms birth
    // window puts after the switch included; and the clock still stops
    // within 90 + 2050 ms of the switch.
    function test_aHeldLastNoteKeepsAllItsParticlesWhenTheLineEnds() {
        const held = [
            { startMs: 1000, endMs: 1400, text: "ab" },
            { startMs: 1400, endMs: 2400, text: "cd" }
        ];
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentText: "abcd", currentWords: held, positionMs: 1450 });
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: true });
        const t = { source: source, view: view, layer: layerOf(view), lyric: lyricOf(view) };
        tryCompare(t.layer, "snapshotCount", 1);
        const before = current(t.layer).birthTimes;
        const lastWord = before.filter(ms => ms >= 1400);
        verify(lastWord.length > 0);
        verify(lastWord.some(ms => ms > 1450), JSON.stringify(lastWord));

        // The line ends: nothing on the slot.
        source.currentWords = [];
        source.currentText = "";
        tryVerify(() => t.lyric.shownText === "");
        compare(t.layer.snapshotCount, 1);
        const kept = t.layer.describeSnapshots()[0];
        compare(kept.birthTimes.length, before.length);
        compare(kept.birthTimes.filter(ms => ms >= 1400).length, lastWord.length);
        verify(t.layer.particlesAliveUntilMs <= 1450 + 90 + 2050, t.layer.particlesAliveUntilMs);
        compare(view.wordClockRunning, true);
        step(view, source, 1450 + 90 + 2050);
        compare(view.wordClockRunning, false);
        compare(t.layer.snapshotCount, 0);
    }

    // A line switched away from at a position where it can never show again
    // -- long after its last particle, or before its first word after a seek
    // back -- is not kept at all: kept, it would hold snapshotCount up and
    // ask for empty frames for as long as the position stood still.
    function test_aLineThatCanNoLongerShowIsNotKept() {
        const t = createView();
        tryCompare(t.layer, "snapshotCount", 1);
        step(t.view, t.source, 2100);
        switchTo(t, "efgh", lineB);
        tryCompare(t.layer, "snapshotCount", 2);
        step(t.view, t.source, 90000);
        compare(t.layer.snapshotCount, 1);
        t.source.playbackStatus = "Paused";
        switchToPlainLine(t);
        compare(t.layer.snapshotCount, 0);
        compare(t.layer.particlesAliveUntilMs, -Infinity);

        // A seek back to before the line being sung.
        t.source.playbackStatus = "Playing";
        step(t.view, t.source, 2100);
        switchTo(t, "efgh", lineB);
        tryCompare(t.layer, "snapshotCount", 1);
        step(t.view, t.source, 500);
        switchToPlainLine(t, "before it");
        compare(t.layer.snapshotCount, 0);
    }

    // A repeated line with its own timings is a line of its own.
    function test_aRepeatedLineKeepsBothCopies() {
        const t = createView();
        tryCompare(t.layer, "snapshotCount", 1);
        step(t.view, t.source, 2100);
        switchTo(t, "abcd", [
            { startMs: 2000, endMs: 2300, text: "ab" },
            { startMs: 2300, endMs: 2600, text: "cd" }
        ]);
        tryCompare(t.layer, "snapshotCount", 2);
    }

    // A new track drops everything in the air -- every kept line, and every
    // particle of the line being sung born by then -- and nothing more: the
    // words still to come on that line spawn when they start.
    function test_aNewTrackDropsWhatIsInTheAir() {
        const t = createView();
        tryCompare(t.layer, "snapshotCount", 1);
        step(t.view, t.source, 2100);
        switchTo(t, "efgh", lineB);
        tryCompare(t.layer, "snapshotCount", 2);

        // "ef" (2000) is in the air, "gh" (2300) still to come.
        t.source.fingerprint = "track-2";
        compare(t.layer.snapshotCount, 1);
        const live = current(t.layer);
        compare(live.startMs, 2000);
        verify(live.birthTimes.length > 0);
        verify(live.birthTimes.every(ms => ms >= 2300), JSON.stringify(live.birthTimes));
        verify(t.layer.particlesAliveUntilMs >= 2300 + 2050);
        // The line switch that follows keeps only the words started by then:
        // the drop left nothing of "ef", and "gh" has not started.
        switchToPlainLine(t, "new track");
        compare(t.layer.snapshotCount, 0);
        compare(t.view.wordClockRunning, false);

        switchTo(t, "abcd", lineA);
        tryCompare(t.layer, "snapshotCount", 1);
        compare(current(t.layer).startMs, 1000);
    }

    // qa-2's case: the fingerprint changes while playback stays on the same
    // line -- a track found in the cache publishes its lyrics at once, so
    // the slot never shows "Searching…". The word in flight goes; the next
    // word on the line still spawns.
    function test_aTrackChangeMidLineStillSpawnsTheLinesLaterWords() {
        const t = createView();
        step(t.view, t.source, 1050);
        tryCompare(t.layer, "snapshotCount", 1);
        t.source.fingerprint = "track-2";
        step(t.view, t.source, 1500);
        tryCompare(t.layer, "snapshotCount", 1);
        const live = current(t.layer);
        verify(live !== undefined);
        verify(live.birthTimes.every(ms => ms > 1050), JSON.stringify(live.birthTimes));
        verify(live.birthTimes.some(ms => ms >= 1400), JSON.stringify(live.birthTimes));
        verify(t.layer.particlesAliveUntilMs >= 1400 + 2050);
        compare(t.view.wordClockRunning, true);
    }
}
