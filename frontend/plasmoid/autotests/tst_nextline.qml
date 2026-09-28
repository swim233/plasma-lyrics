import QtQuick
import QtTest
import io.github.swim233.lyrics
import "../package/contents/ui" as LyricsUi

// DESIGN.md decisions 28 and 80: the next line under the current one, and
// the push up that turns it into the current line. The motion is asserted
// on the tweens themselves -- every moving value of a block is a function
// of AnimatedLyric's clock, and valueAt() evaluates it at any moment -- so
// the timings are pinned without waiting on real frames. The blur itself is
// never seen here: the offscreen platform runs the software backend, which
// runs no shader, so what is asserted is that the effect exists and what it
// is asked for; how that looks was calibrated under Xvfb with real GL.
//
// Every top-level object below is a Component, for the load-time reason
// tst_appearance.qml's header gives.
TestCase {
    name: "NextLine"
    when: windowShown

    // tst_appearance.qml's stand-in for LyricSource, with the next line,
    // both indices and a fingerprint.
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
            property string currentText: "first line"
            property string currentTranslation: ""
            property string currentRomanization: ""
            property var currentWords: []
            property var currentSyntheticWords: []
            property string nextText: "second line"
            property int currentLineIndex: 0
            property int nextLineIndex: 1
            property real positionMs: 0
            function lyricPositionMs() { return positionMs; }
        }
    }

    Component {
        id: lyricsViewComponent
        LyricsUi.LyricsView {
            width: 600
            height: 300
            panelMode: false
            showNextLine: true
            animationMode: "slide"
        }
    }

    Component {
        id: textMetricsComponent
        TextMetrics {}
    }

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
    function lyricOf(view) {
        return findAll(view, o => o.shownText !== undefined)[0];
    }
    // A block's two lines: the lyric first, then the secondary lyrics.
    function linesOf(block) {
        return findAll(block, o => o.lineText !== undefined && o.wordMode !== undefined);
    }
    function lyricLineOf(block) {
        return linesOf(block)[0];
    }
    function wholeLineTextOf(line) {
        const clipper = line.children.filter(c => c.objectName === "lineClipper")[0];
        return clipper.children.filter(c => c.fontSizeMode !== undefined && c.text === line.lineText
                                            && c.x === (line.marqueeApplies ? line.marqueeOffset : 0))[0];
    }
    function effectsUnder(item) {
        return findAll(item, o => o.blurEnabled !== undefined && o.blurMax !== undefined);
    }
    function blurLoaderOf(block) {
        return findAll(block, o => o.objectName === "nextLineBlur")[0];
    }
    function layeredUnder(item) {
        return findAll(item, o => o.layer !== undefined && o.layer.enabled);
    }
    function createView(sourceProperties, viewProperties) {
        const source = createTemporaryObject(fakeSourceComponent, this, sourceProperties || {});
        const view = createTemporaryObject(lyricsViewComponent, this,
            Object.assign({ source: source }, viewProperties || {}));
        verify(view !== null);
        const lyric = lyricOf(view);
        tryVerify(() => lyric.placed);
        // The bindings settling at creation leave a switch pending a turn;
        // run it now, or it would take its record in the middle of a test.
        wait(0);
        return { source: source, view: view, lyric: lyric };
    }
    // The line after the current one arrives, as LyricSource announces it:
    // the current line first, the next line after it, and the old next
    // index is the current one now. The switch runs at once rather than a
    // turn later, so that every tween starts at a known clock.
    function advance(t, text, next, extra) {
        const s = t.source;
        const index = s.nextLineIndex;
        s.currentLineIndex = index;
        s.currentWords = (extra && extra.words) || [];
        s.currentTranslation = (extra && extra.translation) || "";
        s.currentText = text;
        s.nextLineIndex = next === "" ? -1 : index + 1;
        s.nextText = next;
        t.lyric.switchLine();
        compare(t.lyric.shownText, t.view.effectiveText);
    }
    function tweenOf(block, key) {
        return block.motion[key];
    }
    function isTween(value) {
        return typeof value === "object" && value !== null && value.dur !== undefined;
    }
    function settled(lyric) {
        tryVerify(() => !lyric.animating, 3000);
    }

    // ---- When it is shown

    function test_nextLineShowsOnlyWhenItShould_data() {
        return [
            { tag: "shown", source: {}, view: {}, shown: true },
            { tag: "paused", source: { playbackStatus: "Paused" }, view: {}, shown: true },
            { tag: "switched off", source: {}, view: { showNextLine: false }, shown: false },
            { tag: "panel", source: {}, view: { panelMode: true }, shown: false },
            { tag: "searching", source: { lyricState: "searching" }, view: {}, shown: false },
            { tag: "not found", source: { lyricState: "not-found" }, view: {}, shown: false },
            { tag: "stopped", source: { playbackStatus: "Stopped" }, view: {}, shown: false },
            { tag: "after the last line", source: { nextText: "", nextLineIndex: -1 }, view: {}, shown: false },
        ];
    }
    function test_nextLineShowsOnlyWhenItShould(data) {
        const t = createView(data.source, data.view);
        compare(t.view.effectiveNextText, data.shown ? "second line" : "");
        compare(t.lyric.nextText, t.view.effectiveNextText);
        compare(t.lyric.nextBlock !== null, data.shown);
        compare(findAll(t.lyric, o => o.role === "next").length, data.shown ? 1 : 0);
        // No height kept for it when it is not shown.
        const expected = t.lyric.currentPlaceHeight
            + (data.shown ? t.lyric.gap + t.lyric.lineBoxHeight * 0.75 : 0);
        compare(t.lyric.groupHeight, expected);
    }

    // ---- What it looks like

    function test_theNextLineIsTheSameLineAtThreeQuartersDimmed() {
        const t = createView({}, { wordUnsungColor: "#80336699" });
        const next = t.lyric.nextBlock;
        const current = t.lyric.currentBlock;
        compare(next.scale, 0.75);
        compare(next.transformOrigin, Item.Top);
        compare(next.width, current.width);
        compare(next.x, current.x);
        const line = lyricLineOf(next);
        // Laid out as the current line is, at the same nominal size.
        compare(line.fontSize, lyricLineOf(current).fontSize);
        compare(line.lineHeight, lyricLineOf(current).lineHeight);
        compare(next.height, line.lineHeight);
        // Unsung colour, its opacity times 0.6.
        const shown = wholeLineTextOf(line).color;
        verify(Qt.colorEqual(Qt.rgba(shown.r, shown.g, shown.b, 1), "#336699"));
        fuzzyCompare(shown.a, 0x80 / 255 * 0.6, 0.002);
        // A whole line, and nothing else: no words, no secondary lyrics,
        // no particles.
        verify(line.staticFit);
        verify(!line.wordMode);
        compare(next.secondaryLyricText, "");
        compare(linesOf(next)[1].visible, false);
        verify(!next.particlesWanted);
        compare(line.particleLine, null);
    }

    // Given the current line's word timings, the next line still gets none.
    function test_theNextLineHasNoWordsLiftOrParticles() {
        const words = [{ startMs: 0, endMs: 400, text: "first " }, { startMs: 400, endMs: 800, text: "line" }];
        const t = createView({ currentWords: words, positionMs: 200 });
        verify(lyricLineOf(t.lyric.currentBlock).wordMode);
        const next = t.lyric.nextBlock;
        compare(next.words.length, 0);
        compare(findAll(next, o => o.objectName === "lyricWord").length, 0);
        compare(next.positionMs, 0);
    }

    // ---- The blur

    function test_theBlurFollowsItsStrengthAndTheFontSize_data() {
        return [
            { tag: "0% at 34 px", percent: 0, fontSize: 34, sigma: 0 },
            // 1.5 px on screen at the 0.75 scale is 2 px in the block's own.
            { tag: "25% at 34 px", percent: 25, fontSize: 34, sigma: 2 },
            { tag: "100% at 34 px", percent: 100, fontSize: 34, sigma: 8 },
            { tag: "25% at 68 px", percent: 25, fontSize: 68, sigma: 4 },
        ];
    }
    function test_theBlurFollowsItsStrengthAndTheFontSize(data) {
        const t = createView({}, { nextLineBlurPercent: data.percent, fontSize: data.fontSize });
        const next = t.lyric.nextBlock;
        fuzzyCompare(next.blurSigma, data.sigma, 1e-9);
        const loader = blurLoaderOf(next);
        if (data.sigma === 0) {
            // No effect at all: no Loader content, no MultiEffect, no layer.
            verify(!loader.active);
            compare(effectsUnder(next).length, 0);
            compare(layeredUnder(next).length, 0);
            return;
        }
        verify(loader.active);
        const effects = effectsUnder(next);
        compare(effects.length, 1);
        verify(effects[0].blurEnabled);
        compare(effects[0].blurMax, 64);
        fuzzyCompare(effects[0].blur, next.blurProduct(data.sigma) / 64, 1e-9);
        // The source is hidden from the scene while the effect draws it.
        compare(effects[0].source.opacity, 0);
        // The current line carries none.
        compare(effectsUnder(t.lyric.currentBlock).length, 0);
    }

    // A strength edited by hand out of 0-100 counts as the nearest end.
    function test_anOutOfRangeStrengthCountsAsTheNearestEnd() {
        const t = createView({}, { nextLineBlurPercent: 250 });
        compare(t.lyric.nextLineBlurPercent, 100);
        fuzzyCompare(t.lyric.nextBlock.blurSigma, 8, 1e-9);
        t.view.nextLineBlurPercent = -20;
        compare(t.lyric.nextLineBlurPercent, 0);
        compare(t.lyric.nextBlock.blurSigma, 0);
        verify(!blurLoaderOf(t.lyric.nextBlock).active);
    }

    // The table LyricBlock turns a standard deviation into MultiEffect's
    // blur × blurMax with: the calibrated points themselves, linear between
    // them, 0 for none and capped at 64.
    function test_theBlurCalibrationIsInterpolated() {
        const t = createView();
        const block = t.lyric.nextBlock;
        compare(block.blurProduct(0), 0);
        // 25% and 100% at the default size, 1.5 and 6 px on screen.
        compare(block.blurProduct(2), 14.5);
        compare(block.blurProduct(8), 36.5);
        compare(block.blurProduct(1.5), 9.75);
        compare(block.blurProduct(1.75), 11.75);
        fuzzyCompare(block.blurProduct(1.625), (9.75 + 11.75) / 2, 1e-9);
        compare(block.blurProduct(40), 64);
        let last = 0;
        for (let sigma = 0.1; sigma < 18; sigma += 0.1) {
            const product = block.blurProduct(sigma);
            verify(product > last, sigma);
            last = product;
        }
    }

    // ---- The push up, in sequence

    function test_theNextLineRisesIntoTheCurrentPlace() {
        const t = createView({}, { nextLineBlurPercent: 25 });
        const lyric = t.lyric;
        const old = lyric.currentBlock;
        const next = lyric.nextBlock;
        const from = next.y;
        advance(t, "second line", "third line", { translation: "a translation" });

        // The very block that was the next line is the current one now,
        // drawn where it was and carrying on from there.
        verify(lyric.currentBlock === next);
        compare(next.role, "current");
        compare(next.words.length, 0);
        compare(next.secondaryLyricText, "a translation");
        verify(!next.staticFit);
        fuzzyCompare(next.y, from, 1e-9);
        const t0 = lyric.clockMs;

        // Position on the spring: at its first peak at 360 ms, 3% past the
        // place, and exactly in place once settled, about 514 ms in.
        const slide = tweenOf(next, "slide");
        compare(slide.kind, "spring");
        fuzzyCompare(slide.from, from - lyric.currentY, 1e-9);
        compare(slide.to, 0);
        fuzzyCompare(lyric.springSettleMs, 514, 1);
        fuzzyCompare(lyric.valueAt(slide, t0 + 360), -0.03 * slide.from, 1e-6);
        compare(lyric.valueAt(slide, t0 + lyric.springSettleMs), 0);
        // Size and colour in 450 ms, easing out, never past the target.
        for (const key of ["scale", "grow"]) {
            const tween = tweenOf(next, key);
            compare(tween.kind, "out");
            compare(tween.dur, 450);
            compare(tween.t0, t0);
        }
        compare(tweenOf(next, "scale").from, 0.75);
        compare(tweenOf(next, "scale").to, 1);
        compare(tweenOf(next, "grow").from, 0);
        fuzzyCompare(lyric.valueAt(tweenOf(next, "scale"), t0 + 225), 0.75 + 0.25 * (1 - 0.125), 1e-9);
        // The resting blur clears in 200 ms.
        compare(tweenOf(next, "veil").dur, 200);
        compare(lyric.valueAt(tweenOf(next, "veil"), t0 + 200), 0);
        // The secondary lyrics from 90 ms, over 450 ms.
        const secondary = tweenOf(next, "secondary");
        compare(secondary.from, 0);
        compare(secondary.t0, t0 + 90);
        compare(secondary.dur, 450);
        compare(lyric.valueAt(secondary, t0 + 90), 0);

        // The sung line: up one place on the same spring, the two keeping
        // their distance, and shrinking to 0.88×, blurring to 8 px and
        // fading out in 270 ms.
        compare(old.role, "done");
        const exitSlide = tweenOf(old, "slide");
        compare(exitSlide.kind, "spring");
        fuzzyCompare(exitSlide.to - exitSlide.from, lyric.currentY - from, 1e-9);
        compare(tweenOf(old, "scale").to, 0.88);
        compare(tweenOf(old, "scale").dur, 270);
        compare(tweenOf(old, "opacity").to, 0);
        compare(tweenOf(old, "opacity").dur, 270);
        compare(tweenOf(old, "fall").to, 1);
        compare(tweenOf(old, "fall").dur, 270);
        compare(old.releaseAtMs, t0 + 270);

        // The new next line: from 120 ms, 420 ms to fade in, rise 12 px,
        // grow from 0.64× and sharpen from its resting blur plus 6 px.
        const emerging = lyric.nextBlock;
        verify(emerging !== null && emerging !== old && emerging !== next);
        compare(emerging.lyricText, "third line");
        for (const key of ["slide", "scale", "opacity", "rise"]) {
            const tween = tweenOf(emerging, key);
            compare(tween.t0, t0 + 120);
            compare(tween.dur, 420);
            compare(tween.kind, "out");
        }
        compare(tweenOf(emerging, "slide").from, 12);
        compare(tweenOf(emerging, "slide").to, 0);
        compare(tweenOf(emerging, "scale").from, 0.64);
        compare(tweenOf(emerging, "scale").to, 0.75);
        compare(tweenOf(emerging, "opacity").from, 0);
        compare(tweenOf(emerging, "rise").from, 1);
        // Invisible until it starts.
        compare(lyric.valueAt(tweenOf(emerging, "opacity"), t0 + 119), 0);
        fuzzyCompare(emerging.blurSigma, (1.5 + 6) / 0.64, 1e-9);
    }

    // At 8 px on the sung line and 6 px extra on the new next line, like the
    // resting blur, the lengths follow the font size.
    function test_theChoreographyScalesWithTheFontSize() {
        const t = createView({}, { fontSize: 68 });
        const old = t.lyric.currentBlock;
        advance(t, "second line", "third line");
        const emerging = t.lyric.nextBlock;
        compare(tweenOf(emerging, "slide").from, 24);
        // 25% at 68 px is 3 px on screen, plus 12 px as it starts to emerge.
        fuzzyCompare(emerging.blurSigma, (3 + 12) / 0.64, 1e-9);
        // The sung line just before it is released: 16 px on screen.
        t.lyric.clockMs = old.releaseAtMs - 0.001;
        compare(old.role, "done");
        fuzzyCompare(old.blurSigma * old.scale, 16, 1e-6);
    }

    // With the next line not on show there is nothing to promote: the new
    // line comes up from one line box below, fading in over 324 ms, sharp.
    function test_withoutANextLineTheLineComesFromBelow_data() {
        return [
            { tag: "switched off", view: { showNextLine: false } },
            { tag: "panel", view: { panelMode: true } },
        ];
    }
    function test_withoutANextLineTheLineComesFromBelow(data) {
        const t = createView({}, data.view);
        const old = t.lyric.currentBlock;
        verify(t.lyric.nextBlock === null);
        // Still told apart by the indices, which the data always carries.
        advance(t, "second line", "third line");
        const block = t.lyric.currentBlock;
        verify(block !== old);
        compare(old.role, "done");
        const slide = tweenOf(block, "slide");
        compare(slide.kind, "spring");
        compare(slide.from, block.lineHeight);
        compare(tweenOf(block, "opacity").from, 0);
        compare(tweenOf(block, "opacity").dur, 324);
        compare(block.blurSigma, 0);
        verify(!blurLoaderOf(block).active);
        verify(t.lyric.nextBlock === null);
    }

    // A line arriving before the next one on show has finished emerging
    // carries on from wherever it is.
    function test_aLineStillEmergingIsPromotedFromWhereItIs() {
        const t = createView();
        advance(t, "second line", "third line");
        const emerging = t.lyric.nextBlock;
        // 300 ms into the switch: 180 ms into the emerge.
        t.lyric.clockMs = tweenOf(emerging, "scale").t0 + 180;
        const scale = emerging.scale;
        const y = emerging.y;
        verify(scale > 0.64 && scale < 0.75, scale);
        advance(t, "third line", "fourth line");
        verify(t.lyric.currentBlock === emerging);
        fuzzyCompare(tweenOf(emerging, "scale").from, scale, 1e-9);
        fuzzyCompare(emerging.y, y, 1e-9);
    }

    // ---- Sequence or jump

    function test_aJumpFadesBothPlacesWhereTheyAre_data() {
        return [
            { tag: "seek forward", current: 7 },
            { tag: "seek back", current: 0 },
            { tag: "new track", fingerprint: "track-2", current: 0 },
            // The next index, but not the next line's text.
            { tag: "next index, other text", current: 1 },
            { tag: "offset change", current: 3 },
        ];
    }
    function test_aJumpFadesBothPlacesWhereTheyAre(data) {
        const t = createView();
        const oldCurrent = t.lyric.currentBlock;
        const oldNext = t.lyric.nextBlock;
        const currentY = oldCurrent.y;
        const nextY = oldNext.y;
        const s = t.source;
        if (data.fingerprint) {
            s.fingerprint = data.fingerprint;
        }
        s.currentLineIndex = data.current;
        s.currentText = "elsewhere";
        s.nextLineIndex = data.current + 1;
        s.nextText = "after elsewhere";
        t.lyric.switchLine();
        const t0 = t.lyric.clockMs;
        // The old lines stay put and fade out in 180 ms.
        for (const block of [oldCurrent, oldNext]) {
            compare(block.role, "gone");
            compare(tweenOf(block, "opacity").to, 0);
            compare(tweenOf(block, "opacity").dur, 180);
            verify(!isTween(tweenOf(block, "slide")));
            compare(tweenOf(block, "scale"), block === oldNext ? 0.75 : 1);
            compare(block.releaseAtMs, t0 + 180);
        }
        compare(oldCurrent.y, currentY);
        compare(oldNext.y, nextY);
        // The new ones fade in at their places, at rest otherwise.
        const current = t.lyric.currentBlock;
        const next = t.lyric.nextBlock;
        compare(current.lyricText, "elsewhere");
        compare(next.lyricText, "after elsewhere");
        for (const block of [current, next]) {
            compare(tweenOf(block, "opacity").from, 0);
            compare(tweenOf(block, "opacity").dur, 180);
            compare(tweenOf(block, "slide"), 0);
        }
        compare(current.scale, 1);
        compare(next.scale, 0.75);
        // The next line is blurred all the while.
        verify(blurLoaderOf(next).active);
    }

    // A player filling in its metadata changes the fingerprint and nothing
    // else, and no line signal comes: the next switch is still in sequence.
    function test_aFingerprintChangeAloneKeepsTheSequence() {
        const t = createView();
        const next = t.lyric.nextBlock;
        t.source.fingerprint = "track-1 with an album";
        wait(0);
        advance(t, "second line", "third line");
        verify(t.lyric.currentBlock === next);
        compare(tweenOf(next, "slide").kind, "spring");
    }

    // Another copy of the same song's lyrics with one more line at the top:
    // every index moves up by one, so the new current index is the old next
    // one, but its text is not the old next line's. That is a jump.
    function test_aNewCopyOneLineOffIsAJump() {
        const t = createView();
        const current = t.lyric.currentBlock;
        const next = t.lyric.nextBlock;
        const s = t.source;
        s.currentLineIndex = 1;
        s.currentTranslation = "a translation";
        s.nextLineIndex = 2;
        t.lyric.switchLine();
        compare(s.currentText, "first line");
        compare(current.role, "gone");
        verify(t.lyric.currentBlock !== next);
        compare(tweenOf(t.lyric.currentBlock, "opacity").dur, 180);
        verify(!isTween(tweenOf(t.lyric.currentBlock, "slide")));
        // The next line reads the same and stays where it is.
        verify(t.lyric.nextBlock === next);
        compare(next.role, "next");
    }

    // The intro seeking into a long interlude moves only the next line; the
    // line after it still arrives in sequence, on show or not.
    function test_theIntroSeekingIntoAnInterludeKeepsTheSequence_data() {
        return [
            { tag: "next line on show", view: {}, promoted: true },
            { tag: "panel", view: { panelMode: true }, promoted: false },
        ];
    }
    function test_theIntroSeekingIntoAnInterludeKeepsTheSequence(data) {
        const t = createView({ currentText: "", currentLineIndex: -1, nextLineIndex: 0, nextText: "a" }, data.view);
        verify(t.lyric.currentBlock === null);
        compare(t.lyric.nextBlock !== null, data.promoted);
        // Only the next line changes, as it does when LyricSource sends
        // nextLineChanged alone; the switch comes a turn later.
        t.source.nextLineIndex = 4;
        t.source.nextText = "e";
        tryCompare(t.lyric, "shownNextLineIndex", 4);
        compare(t.lyric.shownNextLineText, "e");
        const next = t.lyric.nextBlock;
        if (data.promoted) {
            compare(next.lyricText, "e");
        }
        advance(t, "e", "f");
        const block = t.lyric.currentBlock;
        compare(block.lyricText, "e");
        compare(tweenOf(block, "slide").kind, "spring");
        if (data.promoted) {
            verify(block === next);
        } else {
            compare(tweenOf(block, "slide").from, block.lineHeight);
        }
    }

    // Lyrics giving way to other text, and back, are jumps -- the way back
    // included, although the line then shown is the next one recorded.
    function test_lyricsAndOtherTextFadeIntoEachOther() {
        const t = createView();
        const current = t.lyric.currentBlock;
        t.source.lyricState = "searching";
        t.lyric.switchLine();
        compare(current.role, "gone");
        verify(t.lyric.nextBlock === null);
        t.source.lyricState = "ok";
        t.source.currentLineIndex = 1;
        t.source.currentText = "second line";
        t.source.nextLineIndex = 2;
        t.source.nextText = "third line";
        t.lyric.switchLine();
        compare(tweenOf(t.lyric.currentBlock, "opacity").dur, 180);
        verify(!isTween(tweenOf(t.lyric.currentBlock, "slide")));
    }

    // The current line ending in a long interlude leaves as a sung line; the
    // next one stays exactly where it is, and is promoted when it starts.
    function test_anInterludeLeavesTheNextLineAlone() {
        const t = createView();
        const old = t.lyric.currentBlock;
        const next = t.lyric.nextBlock;
        const y = next.y;
        const motion = next.motion;
        t.source.currentLineIndex = -1;
        t.source.currentText = "";
        t.lyric.switchLine();
        verify(t.lyric.currentBlock === null);
        compare(old.role, "done");
        compare(tweenOf(old, "slide").kind, "spring");
        fuzzyCompare(tweenOf(old, "slide").to, -(old.height + t.lyric.gap), 1e-9);
        verify(t.lyric.nextBlock === next);
        compare(next.motion, motion);
        compare(next.y, y);
        settled(t.lyric);
        compare(next.y, y);

        t.source.currentLineIndex = 1;
        t.source.currentText = "second line";
        t.source.nextLineIndex = 2;
        t.source.nextText = "third line";
        t.lyric.switchLine();
        verify(t.lyric.currentBlock === next);
        compare(tweenOf(next, "slide").kind, "spring");
    }

    // ---- The next line's own overflow

    function test_aLongNextLineOnlyShrinksAndElides_data() {
        return [
            { tag: "fit", overflowMode: "fit" },
            { tag: "elide", overflowMode: "elide" },
            { tag: "wrap", overflowMode: "wrap" },
            { tag: "marquee", overflowMode: "marquee" },
        ];
    }
    function test_aLongNextLineOnlyShrinksAndElides(data) {
        // About 1.3 times the room: shrunk, and still above the floor.
        const metrics = createTemporaryObject(textMetricsComponent, this, { font: Qt.font({ pixelSize: 34 }) });
        let text = "";
        do {
            text += "Longer ";
            metrics.text = text;
        } while (metrics.advanceWidth < 1.3 * 560);
        const t = createView({ nextText: text }, { overflowMode: data.overflowMode });
        const line = lyricLineOf(t.lyric.nextBlock);
        const whole = wholeLineTextOf(line);
        compare(whole.fontSizeMode, Text.HorizontalFit);
        compare(whole.wrapMode, Text.NoWrap);
        compare(whole.maximumLineCount, 1);
        compare(whole.elide, Text.ElideRight);
        verify(!line.marqueeApplies);
        verify(!line.marqueeWanted);
        compare(line.height, line.lineHeight);
        tryVerify(() => whole.fontInfo.pixelSize < 34, 1000, String(whole.fontInfo.pixelSize));
        verify(whole.fontInfo.pixelSize >= line.minimumPixelSize);

        // Far too long for the floor: elided, still one line.
        t.source.nextText = text.repeat(4);
        t.lyric.switchLine();
        const longer = lyricLineOf(t.lyric.nextBlock);
        const longerText = wholeLineTextOf(longer);
        tryVerify(() => longerText.truncated);
        tryCompare(longerText.fontInfo, "pixelSize", longer.minimumPixelSize);
        compare(longer.height, longer.lineHeight);
    }

    // The next line and the words it turns into share one size: the words
    // take the size "fit" gives the whole line, so the line does not change
    // size as it becomes the current one.
    function test_aShrunkNextLineKeepsItsSizeAsItsWordsArrive() {
        const texts = [];
        const metrics = createTemporaryObject(textMetricsComponent, this, { font: Qt.font({ pixelSize: 34 }) });
        do {
            texts.push("Word" + texts.length + " ");
            metrics.text = texts.join("");
        } while (metrics.advanceWidth < 1.25 * 560);
        const text = texts.join("");
        const t = createView({ nextText: text });
        const next = t.lyric.nextBlock;
        const whole = wholeLineTextOf(lyricLineOf(next));
        tryVerify(() => whole.fontInfo.pixelSize < 34);
        const size = whole.fontInfo.pixelSize;
        const words = texts.map((w, i) => ({ startMs: 1000 + 100 * i, endMs: 1100 + 100 * i, text: w }));
        advance(t, text, "", { words: words });
        verify(t.lyric.currentBlock === next);
        const line = lyricLineOf(next);
        verify(line.wordMode);
        tryVerify(() => findAll(next, o => o.objectName === "lyricWord").length === words.length);
        compare(line.wordPixelSize, size);
        const glyphs = findAll(next, o => o.objectName === "lyricWord");
        for (let i = 0; i < glyphs.length; ++i) {
            compare(glyphs[i].font.pixelSize, size);
        }
    }

    // ---- Placement

    // Item.visible is ancestor-combined and reads false throughout this
    // suite, so a block's height never counts its secondary lyrics here;
    // the placement is the same arithmetic either way.
    function test_theGroupIsCentredWhileItFits() {
        const t = createView();
        const lyric = t.lyric;
        const current = lyric.currentBlock;
        const next = lyric.nextBlock;
        compare(lyric.gap, 4);
        const group = current.height + 4 + next.height * 0.75;
        verify(group < lyric.height);
        fuzzyCompare(current.y, (lyric.height - group) / 2, 1e-9);
        fuzzyCompare(next.y, current.y + current.height + 4, 1e-9);
        // The next line's drawn bottom is as far from the bottom as the
        // current line's top is from the top.
        fuzzyCompare(lyric.height - (next.y + next.height * 0.75), current.y, 1e-9);
    }

    function test_aGroupTooTallSitsAtTheTopAndSpillsDown() {
        const t = createView({}, { height: 120 });
        const lyric = t.lyric;
        const current = lyric.currentBlock;
        const next = lyric.nextBlock;
        verify(current.height + lyric.gap + next.height * 0.75 > lyric.height);
        compare(current.y, 0);
        compare(next.y, current.height + lyric.gap);
        verify(next.y + next.height * 0.75 > lyric.height);
    }

    // With the next line off the current one alone follows the same rule:
    // centred while it fits, at the top when it does not, never above.
    function test_theCurrentLineAloneFollowsTheSameRule() {
        const t = createView({}, { showNextLine: false });
        const lyric = t.lyric;
        const current = lyric.currentBlock;
        fuzzyCompare(current.y, (lyric.height - current.height) / 2, 1e-9);
        t.view.height = 80;
        verify(current.height > lyric.height);
        compare(current.y, 0);
    }

    // ---- The clip

    function test_onlyTheTopOfTheLyricAreaIsClipped() {
        const t = createView();
        const lyric = t.lyric;
        const stage = lyric.currentBlock.parent;
        verify(stage.clip);
        const topLeft = stage.mapToItem(lyric, 0, 0);
        compare(topLeft.y, 0);
        verify(topLeft.x < -1000);
        verify(stage.width > lyric.width + 2000);
        verify(stage.height > lyric.height + 1000);
        // The particles are outside it.
        const layer = findAll(lyric, o => o.objectName === "wordParticles")[0];
        verify(layer.parent === lyric);
        verify(findAll(stage, o => o === layer).length === 0);
    }

    // ---- The three modes, and animations off

    function test_fadeCrossesBothPlacesInPlace() {
        const t = createView({}, { animationMode: "fade" });
        const oldCurrent = t.lyric.currentBlock;
        const oldNext = t.lyric.nextBlock;
        advance(t, "second line", "third line");
        compare(oldCurrent.role, "gone");
        compare(oldNext.role, "gone");
        verify(t.lyric.currentBlock !== oldNext);
        compare(tweenOf(t.lyric.currentBlock, "opacity").dur, 180);
        compare(tweenOf(t.lyric.nextBlock, "opacity").dur, 180);
        verify(!isTween(tweenOf(t.lyric.currentBlock, "slide")));
        compare(t.lyric.nextBlock.scale, 0.75);
        verify(blurLoaderOf(t.lyric.nextBlock).active);
    }

    function test_noneAndAnimationsOffSwitchAtOnce_data() {
        return [
            { tag: "none", view: { animationMode: "none" }, off: false },
            { tag: "animations off", view: { animationMode: "slide" }, off: true },
        ];
    }
    function test_noneAndAnimationsOffSwitchAtOnce(data) {
        const t = createView({}, data.view);
        if (data.off) {
            t.lyric.transitionsAnimate = false;
        }
        compare(t.lyric.effectiveAnimationMode, "none");
        advance(t, "second line", "third line");
        verify(!t.lyric.animating);
        compare(t.lyric.currentBlock.lyricText, "second line");
        compare(t.lyric.nextBlock.lyricText, "third line");
        compare(t.lyric.currentBlock.opacity, 1);
        compare(t.lyric.currentBlock.scale, 1);
        compare(t.lyric.nextBlock.scale, 0.75);
        compare(t.lyric.nextBlock.opacity, 1);
        // Everything else is released, and the next line stays blurred.
        for (const block of t.lyric.blocks()) {
            if (block !== t.lyric.currentBlock && block !== t.lyric.nextBlock) {
                compare(block.role, "free");
                compare(block.lyricText, "");
            }
        }
        verify(blurLoaderOf(t.lyric.nextBlock).active);
    }

    // ---- Nothing left behind

    function test_nothingRunsOnceTheSwitchIsOver() {
        const words = [{ startMs: 0, endMs: 300, text: "second " }, { startMs: 300, endMs: 600, text: "line" }];
        const t = createView({}, { wordBlurGlow: false });
        advance(t, "second line", "third line", { words: words, translation: "a translation" });
        verify(t.lyric.animating);
        // Mid-way the promoted line is still blurred.
        verify(blurLoaderOf(t.lyric.currentBlock).active);
        settled(t.lyric);
        const current = t.lyric.currentBlock;
        compare(current.lyricText, "second line");
        compare(current.blurSigma, 0);
        verify(!blurLoaderOf(current).active);
        compare(effectsUnder(current).length, 0);
        compare(layeredUnder(current).length, 0);
        compare(current.scale, 1);
        compare(current.nextLineShare, 0);
        compare(current.secondaryLyricOpacity, 1);
        compare(current.slideOffset, 0);
        // Every value is at rest, not a finished tween.
        for (const key in current.motion) {
            verify(!isTween(current.motion[key]), key);
        }
        // The sung line has let go of everything.
        const others = t.lyric.blocks().filter(b => b !== current && b !== t.lyric.nextBlock);
        compare(others.length, t.lyric.blocks().length - 2);
        for (const block of others) {
            compare(block.role, "free");
            compare(block.lyricText, "");
            compare(block.secondaryLyricText, "");
            compare(block.words.length, 0);
        }
        // And the next line is still blurred, at rest.
        const next = t.lyric.nextBlock;
        compare(next.scale, 0.75);
        verify(blurLoaderOf(next).active);
    }
}
