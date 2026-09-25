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

    function test_theLineBeingSungIsCapturedWithItsLastBirth() {
        const t = createView();
        verify(t.layer !== undefined);
        tryCompare(t.layer, "snapshotCount", 1);
        const live = current(t.layer);
        compare(live.startMs, 1000);
        compare(live.text, "abcd");
        verify(live.particles >= 2 && live.particles <= 6, live.particles);
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

    // Every birth point lies on the words' glyphs as laid out, mapped into
    // the layer: across the words' ink, and above their baseline by no more
    // than a CJK glyph is tall. Only relations, never pixel values -- the CI
    // container has no CJK font at all.
    function test_particlesAreBornOnTheGlyphs() {
        const t = createView({ fontSize: 40 });
        tryCompare(t.layer, "snapshotCount", 1);
        const glyphs = findAll(t.view, o => o.objectName === "lyricWord");
        compare(glyphs.length, 2);
        function inside() {
            // The delegates, which the lift does not move.
            let left = Infinity, right = -Infinity, baseline = 0;
            for (const glyph of glyphs) {
                const word = glyph.parent;
                const box = word.mapToItem(t.layer, 0, 0, word.width, word.height);
                left = Math.min(left, box.x);
                right = Math.max(right, box.x + box.width);
                baseline = word.mapToItem(t.layer, 0, word.baselineOffset).y;
            }
            const births = current(t.layer).births;
            return births.width > 0
                && births.x >= left && births.x + births.width <= right
                && births.y + births.height < baseline && births.y > baseline - 1.5 * 40;
        }
        tryVerify(inside, 2000, JSON.stringify(current(t.layer).births));
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
        const line = createTemporaryObject(lyricLineComponent, this, { words: lineA });
        tryVerify(() => line.particleLine !== null);
        compare(line.particleLine.startMs, 1000);
        compare(line.particleLine.text, "abcd");
        compare(line.particleLine.words.length, 2);
        compare(line.particleLine.words[1].text, "cd");
        tryVerify(() => line.particleLine.words[1].x > line.particleLine.words[0].x);
        line.envelopesAnimate = false;
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

    // Only the lyric's own line spawns; the second line carries no words and
    // is never read.
    function test_theSecondLineNeverSpawns() {
        const t = createView();
        t.source.currentTranslation = "translation";
        tryVerify(() => t.lyric.shownTranslation === "translation");
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

    // A switch that only changes the second line lands on the same line; the
    // copy detach() kept must not stay beside the line being sung.
    function test_aSecondLineChangeDoesNotDoubleTheParticles() {
        const t = createView();
        t.source.currentTranslation = "one";
        tryVerify(() => t.lyric.shownTranslation === "one");
        tryCompare(t.layer, "snapshotCount", 1);
        t.source.currentTranslation = "two";
        tryVerify(() => t.lyric.shownTranslation === "two");
        tryCompare(t.layer, "snapshotCount", 1);
        compare(t.layer.describeSnapshots().filter(s => s.startMs === 1000).length, 1);
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

    // A new track drops everything, the line being sung included: the line
    // switch that follows the track change must not bring it back.
    function test_aNewTrackDropsEveryParticle() {
        const t = createView();
        tryCompare(t.layer, "snapshotCount", 1);
        step(t.view, t.source, 2100);
        switchTo(t, "efgh", lineB);
        tryCompare(t.layer, "snapshotCount", 2);

        t.source.fingerprint = "track-2";
        compare(t.layer.snapshotCount, 0);
        compare(t.layer.particlesAliveUntilMs, -Infinity);
        switchToPlainLine(t, "new track");
        wait(50);
        compare(t.layer.snapshotCount, 0);
        compare(t.view.wordClockRunning, false);

        switchTo(t, "abcd", lineA);
        tryCompare(t.layer, "snapshotCount", 1);
        compare(current(t.layer).startMs, 1000);
    }
}
