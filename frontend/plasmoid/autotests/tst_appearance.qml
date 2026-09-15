import QtQuick
import QtQuick.Layouts
import QtTest
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg
import "../package/contents/ui" as LyricsUi
import "../package/contents/ui/config" as LyricsConfig
import "../package/contents/ui/TextPolicy.js" as TextPolicy

// A QML binding loop is never a QtTest failure: it is only a warning, so
// this binary's own exit code is 0 and its own Totals line reports 0 failed
// regardless of when the loop fires or whether ctest is involved at all --
// confirmed by running the binary directly, with a runtime binding loop
// planted in a test function, printing a QWARN line to the terminal while
// still exiting 0. The only thing that can fail this test over a binding
// loop is frontend/plasmoid/CMakeLists.txt's
// FAIL_REGULAR_EXPRESSION "Binding loop", which greps ctest's captured
// output text for that string -- it catches nothing itself, it just makes
// ctest treat a matching line as a failure.
//
// That text-scan has its own blind spot: QTestLog only starts tagging
// warnings with a QWARN prefix once the first test function begins running,
// so a binding loop that fires while loading the FIRST QML document in this
// file (before any test function has started) prints with no QWARN prefix.
// Under ctest, stderr is a pipe rather than a tty, and Qt's default handler
// routes an untagged warning through sd_journal_send() into the real journal
// instead of ctest's captured output -- confirmed with journalctl. The regex
// never sees it, and this specific loop escapes even ctest, not just the
// bare binary. Warnings from documents loaded after that point are tagged
// and are caught.
//
// This is safe today only because every top-level object below is a
// Component (its contents are lazily instantiated, so loading them here
// emits nothing) -- the moment a top-level object that is NOT a Component is
// added, whatever it does at load time reopens this blind spot.
TestCase {
    name: "Appearance"
    when: windowShown

    // KSvg.ImageSet normally resolves the running Plasma session's theme via
    // KConfig; QUICK_TEST_MAIN bootstraps a bare QGuiApplication with no such
    // session, so it falls back to a nonexistent test-specific path and every
    // KSvg.FrameSvgItem's `margins` reads all zero. "default" is guaranteed
    // to exist wherever plasma-workspace itself is installed (a build-time
    // dependency already, via ECM/Plasma frameworks), so pointing basePath at
    // it directly gives test_selfDrawnPlateMarginsFoldIntoSizeOnlyWhenSelfDrawn
    // real, nonzero margins to check the folding arithmetic against, rather
    // than a test that would pass just as well with the folding deleted.
    function initTestCase() {
        KSvg.ImageSet.basePath = "/usr/share/plasma/desktoptheme";
    }

    Component {
        id: lyricLineComponent
        LyricsUi.LyricLine {
            width: 320
            lineText: "A deliberately long lyric used to exercise overflow"
        }
    }

    // A LyricLine inside a window that is actually shown: Item.visible is
    // ancestor-combined, so this is the only way anything in this suite can
    // observe a true `visible` at all.
    Component {
        id: windowedLineComponent
        Window {
            width: 400
            height: 200
            visible: true
            property alias line: innerLine
            LyricsUi.LyricLine {
                id: innerLine
                width: 400
                overflowMode: "marquee"
            }
        }
    }

    Component {
        id: wordLineComponent
        LyricsUi.LyricLine {
            width: 400
            fontSize: 40
            lineText: "abcdef"
            unsungColor: "#40ffffff"
            activeColor: "#ffffffff"
            sungColor: "#a0ffffff"
        }
    }

    Component {
        id: animatedLyricComponent
        LyricsUi.AnimatedLyric {
            width: 320
            height: 120
            lyricText: "first"
            translationText: "translation one"
            animationMode: "none"
        }
    }

    // DESIGN.md decision 40 split the old combined ConfigAppearance page
    // into one tab per form factor, so what used to be "two sections on one
    // page" is now "two separate page instances, one AppearanceSection each".
    Component {
        id: configDesktopAppearanceComponent
        LyricsConfig.ConfigDesktopAppearance {}
    }

    Component {
        id: configPanelAppearanceComponent
        LyricsConfig.ConfigPanelAppearance {}
    }

    Component {
        id: configTextComponent
        LyricsConfig.ConfigText {}
    }

    Component {
        id: configBackendComponent
        LyricsConfig.ConfigBackend {}
    }

    Component {
        id: restartFeedbackComponent
        LyricsConfig.RestartFeedback {
            succeeded: false
            failed: false
            errorText: ""
        }
    }

    Component {
        id: textConfigurationComponent
        QtObject {
            property string idleText: ""
            property bool idleTextUseDefault: true
            property string notFoundText: ""
            property bool notFoundTextUseDefault: true
            property string noLyricText: ""
            property bool noLyricTextUseDefault: true
            property string networkErrorText: ""
            property bool networkErrorTextUseDefault: true
            property bool emptyTextUseDefault: true
            property int textConfigVersion: 0
        }
    }

    Component {
        id: colorFieldComponent
        LyricsConfig.ColorField {
            value: "#99000000"
        }
    }

    Component {
        id: trackInfoComponent
        LyricsUi.TrackInfo {
            width: 320
            title: "Title"
            artists: "Artist"
            layoutMode: "single"
        }
    }

    // A minimal stand-in for LyricSource: LyricsView only ever reads these
    // properties off `source`, so a plain QtObject with the same names is
    // enough to drive it without pulling in the real snapshot machinery.
    Component {
        id: fakeSourceComponent
        QtObject {
            property bool serviceAvailable: true
            property bool stale: false
            property string lyricState: "ok"
            property string playbackStatus: "Playing"
            property string trackTitle: "Title"
            property string trackArtists: "Artist"
            property string currentText: "la la la"
            property string currentTranslation: ""
            property string currentRomanization: ""
            property var currentWords: []
            // LyricsView pulls the position per frame rather than binding to
            // it, so the stand-in needs the same invokable the real source
            // exposes; driving this by hand is also how the word tests step
            // time without waiting on a real animation.
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

    // The outline is drawn as eight offset copies of the same Text, so most
    // font assertions have to look at every Text under the line, not just
    // one. LyricLine.qml wraps its whole-line Text and stroke copies in an
    // inner `clipper` Item (objectName "lineClipper", one level below the
    // line itself), so this explicitly steps through that one wrapper --
    // not a full recursive descent -- and stops there: the word glyphs sit
    // a Row and a delegate Item further down inside clipper, and this must
    // not reach them (see wordTextsOf below, which is what does).
    function textChildrenOf(item) {
        const out = [];
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i];
            if (child.font !== undefined && child.text !== undefined) {
                out.push(child);
            } else if (child.objectName === "lineClipper") {
                for (let j = 0; j < child.children.length; ++j) {
                    const grandchild = child.children[j];
                    if (grandchild.font !== undefined && grandchild.text !== undefined) {
                        out.push(grandchild);
                    }
                }
            }
        }
        return out;
    }

    function test_overflowModes() {
        const line = createTemporaryObject(lyricLineComponent, this);
        verify(line !== null);
        compare(line.overflowMode, "fit");
        line.overflowMode = "wrap";
        verify(line.implicitHeight > 0);
        line.overflowMode = "marquee";
        compare(line.height, line.lineHeight);
    }

    // The marquee scrolls by animating LyricLine's own marqueeOffset, and the
    // Text's x is a binding over it, so every way of leaving marquee mode
    // lands back at 0 without anyone having to write x. Asserting on x rather
    // than on marqueeOffset is the point: a regression that goes back to
    // animating mainText.x directly would still leave marqueeOffset looking
    // right here while stranding the visible line.
    //
    // These drive marqueeOffset by hand instead of letting the animation run.
    // It cannot run: marqueeRunning requires `visible`, which is
    // ancestor-combined and false throughout this suite (see
    // test_trackInfoStaysUpThroughBlankLyricStates). marqueeApplies, which is
    // what the x binding reads, deliberately excludes `visible` so that these
    // two tests have something to observe at all.
    function test_marqueeOffsetClearsWhenLeavingMarqueeMode() {
        const line = createTemporaryObject(lyricLineComponent, this,
            { overflowMode: "marquee" });
        verify(line !== null);
        verify(line.marqueeApplies);
        const texts = textChildrenOf(line);
        compare(texts.length, 1);
        line.marqueeOffset = -100;
        compare(texts[0].x, -100);
        line.overflowMode = "fit";
        compare(texts[0].x, 0);
        line.overflowMode = "wrap";
        compare(texts[0].x, 0);
    }

    function test_marqueeOffsetClearsWhenTheLineChanges() {
        const line = createTemporaryObject(lyricLineComponent, this,
            { overflowMode: "marquee" });
        verify(line !== null);
        const texts = textChildrenOf(line);
        line.marqueeOffset = -100;
        compare(texts[0].x, -100);
        line.lineText = "A different and also deliberately long lyric line";
        compare(texts[0].x, 0);
    }

    // The faded-out block keeps both of its lines alive and `visible`, so a
    // translation left behind there goes on driving an infinite marquee that
    // nobody can see. Passes on either animation path: with animations off,
    // switchLine releases the block synchronously instead.
    function test_transitionReleasesBothHalvesOfThePreviousBlock() {
        const lyric = createTemporaryObject(animatedLyricComponent, this,
            { animationMode: "fade" });
        verify(lyric !== null);
        tryVerify(() => lyric.shownText === "first");
        lyric.lyricText = "second";
        lyric.translationText = "translation two";
        tryVerify(() => lyric.shownText === "second");
        tryVerify(() => lyric.previousText === "" && lyric.previousTranslation === "");
    }

    function test_outlineIsOptIn() {
        const line = createTemporaryObject(lyricLineComponent, this);
        compare(line.strokeEnabled, false);
        compare(line.directions.length, 8);
    }

    function test_lineAndTranslationSwitchTogether() {
        const lyric = createTemporaryObject(animatedLyricComponent, this);
        verify(lyric !== null);
        compare(lyric.shownText, "first");
        lyric.lyricText = "second";
        lyric.translationText = "translation two";
        tryCompare(lyric, "shownText", "second");
        compare(lyric.shownTranslation, "translation two");
    }

    function test_fontFamilyFollowsPlasmaButWeightDoesNot() {
        const line = createTemporaryObject(lyricLineComponent, this);
        const texts = textChildrenOf(line);
        compare(texts.length, 1);
        // DESIGN.md decision 30: the family tracks the Plasma font setting
        // while size, weight and colour stay with the widget's own config.
        compare(texts[0].font.family, Kirigami.Theme.defaultFont.family);
        compare(line.fontWeight, Font.Normal);
    }

    function test_fontWeightReachesEveryOutlineCopy() {
        const line = createTemporaryObject(lyricLineComponent, this);
        line.strokeEnabled = true;
        line.fontWeight = Font.Black;
        // The copies bind `font: mainText.font` rather than rebuilding a font
        // of their own, so a new font property has to reach all nine Texts
        // without being threaded through the Repeater by hand.
        tryVerify(() => textChildrenOf(line).length === 9);
        const texts = textChildrenOf(line);
        for (let i = 0; i < texts.length; ++i) {
            compare(texts[i].font.weight, Font.Black);
        }
    }

    function test_fontWeightPropagatesThroughTheBlock() {
        const lyric = createTemporaryObject(animatedLyricComponent, this);
        lyric.fontWeight = Font.Light;
        // main.qml -> LyricsView -> AnimatedLyric -> LyricBlock -> LyricLine.
        // Only the last hop applies it, so a missed forward is invisible until
        // the widget is running.
        for (let i = 0; i < lyric.children.length; ++i) {
            const block = lyric.children[i];
            if (block.fontWeight !== undefined) {
                compare(block.fontWeight, Font.Light);
            }
        }
    }

    function test_colorFieldKeepsSwatchAndHexInStep() {
        const field = createTemporaryObject(colorFieldComponent, this);
        verify(field !== null);
        // Declaration order inside the row: the swatch, then the hex field.
        const swatch = field.children[0];
        const hexField = field.children[1];
        compare(hexField.text, "#99000000");
        compare(swatch.color.toString(), "#99000000");

        // Accepting the colour dialog assigns selectedColor imperatively, which
        // destroys any declarative binding on it for good. Reproduce that, then
        // check the row still follows a later change to the configured value.
        swatch.color = "#ff0000";
        field.value = "#cc112233";
        compare(swatch.color.toString(), "#cc112233");
        compare(hexField.text, "#cc112233");
        // Alpha has to survive the trip, or the translucent defaults turn
        // opaque the first time someone opens the picker.
        compare(swatch.color.a.toFixed(4), (0xcc / 255).toFixed(4));
    }

    function test_sectionEditsReachTheirOwnConfigProperties() {
        // Decision 40 put desktop and panel on separate tabs, so what used
        // to be "two sections on one page" is now two page instances, each
        // carrying exactly one AppearanceSection. Crossing the two only
        // shows up as "the panel setting moved the desktop widget".
        const desktopPage = createTemporaryObject(configDesktopAppearanceComponent, this);
        const panelPage = createTemporaryObject(configPanelAppearanceComponent, this);
        verify(desktopPage !== null);
        verify(panelPage !== null);
        const desktopSections = findAll(desktopPage, o => typeof o.textColorEdited === "function");
        const panelSections = findAll(panelPage, o => typeof o.textColorEdited === "function");
        compare(desktopSections.length, 1);
        compare(panelSections.length, 1);
        const desktop = desktopSections[0];
        const panel = panelSections[0];

        desktop.solidColorEdited("#80112233");
        desktop.textColorEdited("#123456");
        desktop.strokeColorEdited("#40445566");
        desktop.fontWeightEdited(Font.Black);
        compare(desktopPage.cfg_desktopSolidColor, "#80112233");
        compare(desktopPage.cfg_desktopTextColor, "#123456");
        compare(desktopPage.cfg_desktopStrokeColor, "#40445566");
        compare(desktopPage.cfg_desktopFontWeight, Font.Black);

        panel.textColorEdited("#abcdef");
        panel.fontWeightEdited(Font.Light);
        compare(panelPage.cfg_panelTextColor, "#abcdef");
        compare(panelPage.cfg_panelFontWeight, Font.Light);
        // Editing the panel page's own properties above must never reach
        // into the desktop page -- they are two separate object instances
        // now, not two sections sharing one.
        compare(desktopPage.cfg_desktopTextColor, "#123456");
        compare(desktopPage.cfg_desktopFontWeight, Font.Black);
    }

    function test_configValueReachesTheColorRow() {
        const desktopPage = createTemporaryObject(configDesktopAppearanceComponent, this);
        const panelPage = createTemporaryObject(configPanelAppearanceComponent, this);
        const desktopSections = findAll(desktopPage, o => typeof o.textColorEdited === "function");
        const panelSections = findAll(panelPage, o => typeof o.textColorEdited === "function");
        desktopPage.cfg_desktopTextColor = "#0f0f0f";
        // Background, text, outline, second line, the three word-state
        // colours, track-info text, track-info outline -- in that order down
        // the form. The track-info section took this from 3 rows to 5, and
        // word-by-word from 5 to 9.
        const rows = findAll(desktopSections[0], o => typeof o.edited === "function");
        compare(rows.length, 9);
        compare(rows[1].value, "#0f0f0f");

        // Same crosstalk bug test_sectionEditsReachTheirOwnConfigProperties
        // guards against, but for the new track-info keys, and now across
        // two separate page instances rather than two sections of one page.
        desktopPage.cfg_desktopTrackInfoColor = "#111111";
        panelPage.cfg_panelTrackInfoColor = "#222222";
        compare(desktopSections[0].trackInfoColor, "#111111");
        compare(panelSections[0].trackInfoColor, "#222222");
        panelSections[0].trackInfoColorEdited("#333333");
        compare(panelPage.cfg_panelTrackInfoColor, "#333333");
        compare(desktopPage.cfg_desktopTrackInfoColor, "#111111");
    }

    function test_trackInfoOffKeepsLyricCentered() {
        const source = createTemporaryObject(fakeSourceComponent, this);
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, showTrackInfo: false });
        verify(view !== null);
        const column = view.children[2];
        const trackInfo = column.children[0];
        const lyric = column.children[1];
        tryCompare(trackInfo, "height", 0);
        // An off/empty track row must not steal any of the vertical space
        // AnimatedLyric centers its text within. A sub-pixel gap between the
        // two is layout rounding, not a real bug, so this allows a 1px slop
        // rather than an exact compare.
        tryVerify(() => Math.abs(lyric.height - column.height) <= 1);
    }

    function test_trackInfoEmptyTitleKeepsLyricCentered() {
        const source = createTemporaryObject(fakeSourceComponent, this, { trackTitle: "" });
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, showTrackInfo: true });
        verify(view !== null);
        const column = view.children[2];
        const trackInfo = column.children[0];
        const lyric = column.children[1];
        tryCompare(trackInfo, "height", 0);
        tryVerify(() => Math.abs(lyric.height - column.height) <= 1);
    }

    function test_singleModeEmptyArtistsOmitsSeparator() {
        const info = createTemporaryObject(trackInfoComponent, this, { artists: "" });
        verify(info !== null);
        const titleLine = info.children[0];
        const texts = textChildrenOf(titleLine);
        compare(texts.length, 1);
        compare(texts[0].text, "Title");
        verify(texts[0].text.indexOf("—") === -1);
    }

    function test_layoutTogglingChangesImplicitHeight() {
        const info = createTemporaryObject(trackInfoComponent, this);
        verify(info !== null);
        const singleHeight = info.implicitHeight;
        info.layoutMode = "double";
        verify(info.implicitHeight > singleHeight);
        info.layoutMode = "single";
        compare(info.implicitHeight, singleHeight);
    }

    function test_elideOverflowKeepsFixedFontSize() {
        const line = createTemporaryObject(lyricLineComponent, this);
        line.overflowMode = "elide";
        const texts = textChildrenOf(line);
        compare(texts.length, 1);
        compare(texts[0].fontSizeMode, Text.FixedSize);
        compare(texts[0].elide, Text.ElideRight);
    }

    function test_artistLineAlphaIsDerivedFromConfiguredColor() {
        const info = createTemporaryObject(trackInfoComponent, this, { layoutMode: "double" });
        // Alpha 0x80/255 ~= 0.502 is neither 1.0 nor 0.75, so a flat 0.75
        // substituted for `textColor.a * 0.75` in TrackInfo.qml is
        // distinguishable from the real derived value (~0.377) here -- an
        // alpha of 1.0 would make the two indistinguishable and defeat the
        // point of this test.
        info.textColor = "#80ffffff";
        const artistLine = info.children[1];
        const texts = textChildrenOf(artistLine);
        compare(texts.length, 1);
        fuzzyCompare(texts[0].color.a, info.textColor.a * 0.75, 0.005);
    }

    function test_trackInfoStaysUpThroughBlankLyricStates() {
        // DESIGN.md decision 39: the row's whole point is staying up while
        // the lyric area is otherwise empty. Height, not `visible` -- Item's
        // visible getter is ancestor-combined and reads false here for every
        // state, since the TestCase root itself is not shown.
        const states = ["searching", "not-found", "filtered", "no-lyric", "network-error"];
        for (let i = 0; i < states.length; ++i) {
            const source = createTemporaryObject(fakeSourceComponent, this, { lyricState: states[i] });
            const view = createTemporaryObject(lyricsViewComponent, this,
                { source: source, showTrackInfo: true });
            verify(view !== null);
            const column = view.children[2];
            const trackInfo = column.children[0];
            tryVerify(() => trackInfo.height > 0);
        }
    }

    function test_nonLyricStatesUseTheirIndependentText() {
        const source = createTemporaryObject(fakeSourceComponent, this);
        const view = createTemporaryObject(lyricsViewComponent, this, {
            source: source,
            notFoundText: "missing",
            noLyricText: "instrumental",
            networkErrorText: "offline"
        });
        verify(view !== null);

        source.lyricState = "not-found";
        compare(view.effectiveText, "missing");
        source.lyricState = "no-lyric";
        compare(view.effectiveText, "instrumental");
        source.lyricState = "network-error";
        compare(view.effectiveText, "offline");
    }


    // ---- word-by-word ----

    readonly property var twoWords: [
        { startMs: 0, endMs: 1000, text: "abc" },
        { startMs: 1000, endMs: 2000, text: "def" }
    ]

    function wordTextsOf(line) {
        // Not textChildrenOf(): the word glyphs sit a Row and a delegate Item
        // below clipper (itself one level below the line), where that helper
        // deliberately does not reach. Matching on objectName rather than on
        // shape, because the whole-line Text and its eight stroke copies are
        // Texts with the same properties.
        return findAll(line, o => o.objectName === "lyricWord");
    }

    // The companion to the assertion above: without this, `!marqueeWanted` in
    // word mode would be satisfied by a property that is simply always false.
    function test_wholeLineMarqueeStillWantsToRun() {
        const wordTimings = [];
        let text = "";
        for (let i = 0; i < 40; ++i) { text += "word" + i + " "; }
        const line = createTemporaryObject(wordLineComponent, this,
            { lineText: text, overflowMode: "marquee" });
        tryVerify(() => line.marqueeApplies);
        tryVerify(() => line.marqueeWanted);
        // ... and it stops wanting to the moment word timings arrive. The
        // tryVerify calls below are load-bearing, and there are two separate
        // reasons a naive version of this test passes for the wrong reason --
        // both measured, and they apply at different moments:
        //
        //   1. A Row lays its children out on the next polish, but only when
        //      it is asked to relayout -- so this depends on the operation,
        //      which is why two people measuring it disagreed. Measured both:
        //        creating the object with `words` in the initial property map
        //          -> laid out during completion, contentWidth 5854,
        //             marqueeApplies already true on the creating turn;
        //        assigning `words` to an object that already exists (what this
        //          test does) -> contentWidth 0, marqueeApplies false until a
        //          polish runs, so the assertion at the end would hold without
        //          `!wordMode` doing any of the work.
        //   2. tryVerify evaluates its predicate synchronously first and
        //      returns without pumping the event loop if it is already true.
        //      So `tryVerify(() => line.marqueeApplies)` is useless as a
        //      settling device on the creating turn: the 0-interval
        //      restartPulse never fires and `restarting` stays true, which
        //      makes marqueeWanted false for a second, unrelated reason.
        //      Waiting on marqueeWanted above works precisely because it is
        //      false at first and therefore does pump.
        for (let i = 0; i < 40; ++i) { wordTimings.push({ startMs: i*100, endMs: (i+1)*100, text: "word" + i + " " }); }
        line.words = wordTimings;
        tryVerify(() => line.wordMode);
        tryVerify(() => line.marqueeApplies);
        verify(!line.marqueeWanted);
    }

    // `visible` is the clause that made the whole condition unobservable, so
    // it needs a case where it is genuinely true. Without a shown window every
    // item here reads visible=false and marqueeRunning can never be anything
    // but false, however broken the rest of the condition is.
    function test_marqueeRunningNeedsAVisibleItem() {
        let text = "";
        for (let i = 0; i < 40; ++i) { text += "word" + i + " "; }
        const win = createTemporaryObject(windowedLineComponent, this);
        verify(win !== null);
        const line = win.line;
        line.lineText = text;
        tryVerify(() => line.visible);
        tryVerify(() => line.marqueeWanted);
        tryVerify(() => line.marqueeRunning);

        // Hiding the item stops the animation even though everything else
        // still wants it to run.
        line.visible = false;
        verify(line.marqueeWanted);
        verify(!line.marqueeRunning);
    }

    // `restarting` holds the sweep off for one event-loop turn so a new line
    // scrolls from its start rather than continuing the previous one's offset.
    function test_aNewLineHoldsTheSweepOffForOneTurn() {
        let text = "";
        for (let i = 0; i < 40; ++i) { text += "word" + i + " "; }
        const line = createTemporaryObject(wordLineComponent, this,
            { lineText: text, overflowMode: "marquee" });
        tryVerify(() => line.marqueeWanted);
        line.lineText = text + " again";
        verify(line.restarting);
        verify(!line.marqueeWanted);
        tryVerify(() => line.marqueeWanted);
    }

    function test_wordSequenceDrivesPerWordState() {
        const line = createTemporaryObject(wordLineComponent, this,
            { words: twoWords, positionMs: 500 });
        verify(line !== null);
        verify(line.wordMode);
        const texts = wordTextsOf(line);
        compare(texts.length, 2);
        compare(texts[0].text, "abc");
        compare(texts[1].text, "def");

        // Colour switches for a whole word at its own start time.
        compare(texts[0].color.toString(), "#ffffff");
        compare(texts[1].color.toString(), "#40ffffff");
        compare(line.activeWordIndex, 0);

        line.positionMs = 1500;
        compare(texts[0].color.toString(), "#a0ffffff");
        compare(texts[1].color.toString(), "#ffffff");
        compare(line.activeWordIndex, 1);

        line.positionMs = 2500;
        compare(texts[0].color.toString(), "#a0ffffff");
        compare(texts[1].color.toString(), "#a0ffffff");
    }

    function test_lineWithoutWordDataFallsBackToTheWholeLine() {
        const line = createTemporaryObject(wordLineComponent, this, { positionMs: 500 });
        verify(line !== null);
        verify(!line.wordMode);
        compare(wordTextsOf(line).length, 0);
        // The whole-line Text is the one that renders instead.
        const texts = textChildrenOf(line);
        compare(texts.length, 1);
        compare(texts[0].text, "abcdef");
        compare(line.activeWordIndex, -1);
    }

    // "wrap" has no single horizontal run for the words to occupy, so it stays
    // on the whole-line path even when word timings are available.
    function test_wrapOverflowKeepsTheWholeLinePath() {
        const line = createTemporaryObject(wordLineComponent, this,
            { words: twoWords, positionMs: 500, overflowMode: "wrap" });
        verify(!line.wordMode);
        compare(wordTextsOf(line).length, 0);
    }

    function test_switchingBetweenWordAndWholeLineLeavesNoResidualState() {
        const line = createTemporaryObject(wordLineComponent, this,
            { words: twoWords, positionMs: 1500 });
        verify(line.wordMode);

        // A track with no word timings: back to one whole line, with no word
        // glyphs left behind.
        line.words = [];
        line.lineText = "plain line";
        verify(!line.wordMode);
        compare(wordTextsOf(line).length, 0);
        compare(textChildrenOf(line)[0].text, "plain line");

        // And back again, at the start of the new track: the first word must
        // read unsung rather than inheriting the previous line's progress.
        line.lineText = "abcdef";
        line.words = twoWords;
        line.positionMs = 0;
        verify(line.wordMode);
        const texts = wordTextsOf(line);
        compare(texts.length, 2);
        compare(texts[0].color.toString(), "#ffffff");
        compare(texts[1].color.toString(), "#40ffffff");
    }

    function test_liftReservesItsOwnHeadroomAndOnlyWhenEnabled() {
        const line = createTemporaryObject(wordLineComponent, this,
            { words: twoWords, positionMs: 500 });
        const plainHeight = line.height;
        compare(line.liftHeadroom, 0);

        line.liftEnabled = true;
        // The item clips, so without this the lift would simply be cut off --
        // at fontSize 40 the 1.25 line box leaves under 2px above the glyphs,
        // and the spring's peak sits liftOvershoot above the settled lift, so
        // the reserve is sized for the peak: ceil(40 × 0.14 × 1.1) = 7 where
        // the settled lift alone would round to 6 (decision 73).
        compare(line.liftHeadroom, Math.ceil(40 * line.liftEm * (1 + line.liftOvershoot)));
        compare(line.liftHeadroom, 7);
        compare(line.height, plainHeight + line.liftHeadroom);

        // Reserved by the setting alone. A track that carries no word timings
        // must not change the line height, or the baseline would hop every
        // time playback crossed between two kinds of source.
        line.words = [];
        verify(!line.wordMode);
        compare(line.liftHeadroom, Math.ceil(40 * line.liftEm * (1 + line.liftOvershoot)));
        compare(line.height, plainHeight + line.liftHeadroom);
    }

    // Finds the inner Item that carries the vertical clip (see LyricLine.qml)
    // by its objectName, since it has no id visible from outside the file.
    function clipperOf(line) {
        const found = findAll(line, o => o.objectName === "lineClipper");
        return found.length > 0 ? found[0] : null;
    }

    // The bug this guards against: a font whose measured line height runs
    // past fontSize × lineHeightFactor (Noto Sans CJK SC does, by a wide
    // margin; the plain Sans Serif this suite actually renders with does
    // too, at roughly 1.38em) has its descenders clipped off, because
    // AlignVCenter splits the overflow evenly above and below the
    // fixed-height text box and only the top half is blank space. Nothing
    // before this test asserted "glyphs are never clipped" at all, which is
    // how the bug shipped.
    //
    // Grounded in mainText.contentHeight -- Qt's own measurement of the
    // rendered glyph box, read off the real Text item via textChildrenOf()
    // -- rather than in LyricLine's internal FontMetrics, which is not
    // exposed as a property. This is the stronger check: it verifies against
    // what Qt actually rendered, not against the same measurement the
    // implementation used to size glyphSpill, so a mismatch between the two
    // would still be caught.
    //
    // qa-1 caught this test passing vacuously on unfixed HEAD's shape (no
    // clipper at all, glyphSpill forced to 0) because the line above -- the
    // suite's default lineText at width 320 -- is long enough that "fit"
    // (the default overflowMode) shrinks mainText to minimumPixelSize
    // (round(40 × 0.6) = 24) to make it fit; contentHeight shrinks right
    // along with it, so the fixed-height box always looked roomy enough
    // regardless of whether glyphSpill did anything. Fixed by using a short
    // line in a box wide enough that "fit" never engages. The configured
    // font size is checked for every factor; an additional 50% fixture
    // proves that overflowing content is exercised even on platforms whose
    // default font fits inside both production line heights.
    function test_glyphContentNeverClipsAtAnyLineHeight() {
        // The deliberately tight box guarantees an overflow case even when
        // the platform font fits naturally at 100% or 125% (e.g. Debian).
        const factors = [0.5, 1.0, 1.25];
        for (let i = 0; i < factors.length; ++i) {
            const line = createTemporaryObject(lyricLineComponent, this,
                { fontSize: 40, lineHeightFactor: factors[i], lineText: "abcgy", width: 400 });
            verify(line !== null);
            const clipper = clipperOf(line);
            verify(clipper !== null);
            const texts = textChildrenOf(line);
            compare(texts.length, 1);
            const mainText = texts[0];
            // "abcgy" rather than a descender-free string: self-documents
            // what this guards (g/y have descenders), though geometrically
            // contentHeight comes from the font/pixel size, not the specific
            // characters, so any non-empty string at this size would do.
            //
            // Premise 1: the line at width 400 must not have been shrunk by
            // "fit" -- otherwise contentHeight shrinks too and the
            // assertions below stop testing anything (qa-1's finding).
            compare(mainText.font.pixelSize, line.fontSize);
            // Prove the tight fixture really exercises clipping; production
            // line heights may legitimately need no spill on a smaller font.
            if (factors[i] === 0.5) {
                verify(mainText.contentHeight > mainText.height + 3);
                verify(line.glyphSpill > 0);
            }
            // mainText is AlignVCenter within its own [y, y + height) box, so
            // its actual rendered ink is centred inside that box too.
            const inkTop = mainText.y + (mainText.height - mainText.contentHeight) / 2;
            const inkBottom = inkTop + mainText.contentHeight;
            // Both in clipper's own local frame, which is exactly the frame
            // its clip rectangle applies in ([0, clipper.height)).
            // 1.5px slop: mainText.contentHeight (QTextLayout's bounding
            // rect) and the FontMetrics height glyphSpill is sized from can
            // legitimately round a little differently for the same font.
            verify(inkTop >= -1.5, `factor ${factors[i]}: inkTop ${inkTop}`);
            verify(inkBottom <= clipper.height + 1.5,
                `factor ${factors[i]}: inkBottom ${inkBottom} clipper.height ${clipper.height}`);
        }
    }

    // Pins the geometry contract between clipper and glyphSpill -- clipper.y
    // and clipper.height are wired directly off root.glyphSpill, so a
    // refactor that breaks that wiring (e.g. a stray hardcoded margin, or an
    // off-by-one in which side gets the spill) is caught here. This does NOT
    // by itself prove glyphSpill's formula matches real glyph ink: it
    // compares the implementation to itself, so a wrong formula and this
    // assertion would agree with each other and both pass.
    // test_glyphContentNeverClipsAtAnyLineHeight is what checks the formula
    // against Qt's own rendered measurement instead.
    function test_clipperGeometryTracksGlyphSpill() {
        const line = createTemporaryObject(lyricLineComponent, this,
            { fontSize: 40, lineHeightFactor: 0.5 });
        verify(line !== null);
        const clipper = clipperOf(line);
        verify(clipper !== null);
        compare(clipper.x, 0);
        compare(clipper.width, line.width);
        compare(clipper.y, -line.glyphSpill);
        compare(clipper.height, line.height + 2 * line.glyphSpill);
        // A deliberately tight fixture exercises nonzero margins without
        // assuming the platform's default font overflows a 125% line box.
        // The content test above independently checks the rendered bounds.
        verify(line.glyphSpill > 0);
    }

    // "wrap" was called out as not needing a special case: root.height is
    // already the natural (unclamped, or clamped to a generous 2 × lineHeight)
    // wrapped height there, so glyphSpill's own Math.max(0, …) floor should
    // land on 0 without any extra code -- this just confirms that.
    function test_wrapOverflowNeedsNoGlyphSpill() {
        const line = createTemporaryObject(lyricLineComponent, this,
            { overflowMode: "wrap", fontSize: 40 });
        verify(line !== null);
        compare(line.glyphSpill, 0);
    }

    // Regression guard for the two invariants the fix must not disturb: the
    // widget's own outer height (which LyricBlock/LyricsView lay out around)
    // stays exactly lineHeight, and the clipper's horizontal extent stays
    // pinned to [0, root.width] -- the same rectangle root itself used to
    // clip at -- so marquee/fit/elide keep their pixel-for-pixel cutoff.
    function test_clipperLeavesImplicitHeightAndHorizontalClipUnchanged() {
        const line = createTemporaryObject(lyricLineComponent, this, { fontSize: 40 });
        verify(line !== null);
        compare(line.implicitHeight, line.lineHeight);
        compare(line.height, line.lineHeight);
        const clipper = clipperOf(line);
        verify(clipper !== null);
        compare(clipper.x, 0);
        compare(clipper.width, line.width);
    }

    function test_liftNeverAppliesInAPanel() {
        const source = createTemporaryObject(fakeSourceComponent, this);
        const desktop = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: false, wordLift: true });
        const panel = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: true, wordLift: true });
        const lifted = o => o.liftEnabled !== undefined && o.liftEnabled;
        verify(findAll(desktop, lifted).length > 0);
        // A panel sets the widget height itself, so a lifted word would only
        // be clipped. There is no panel key for it either -- the row on that
        // config tab is disabled and says so.
        compare(findAll(panel, lifted).length, 0);
    }

    function test_wordMarqueeKeepsTheCurrentWordVisible() {
        const wordTimings = [];
        let text = "";
        for (let i = 0; i < 40; ++i) {
            wordTimings.push({ startMs: i * 100, endMs: (i + 1) * 100, text: "word" + i + " " });
            text += "word" + i + " ";
        }
        const line = createTemporaryObject(wordLineComponent, this, {
            lineText: text, words: wordTimings, overflowMode: "marquee", positionMs: 0
        });
        verify(line !== null);
        // A Row lays its children out on the next polish, so the width this
        // reads is not yet there on the turn the object was created.
        tryVerify(() => line.marqueeApplies);
        // The sweep animation must stay out of it: two things writing the
        // horizontal position would fight over it. Asserted on marqueeWanted,
        // not marqueeRunning -- the latter contains `visible`, which is false
        // for every item in this suite, so it would read false here however
        // broken the rest of the condition was.
        verify(!line.marqueeWanted);

        line.positionMs = 3500;
        tryVerify(() => line.activeWordItem !== null);
        const item = line.activeWordItem;
        // A Behavior smooths the scroll, so this waits for it to settle rather
        // than reading a value still in flight.
        tryVerify(() => {
            const visibleLeft = -line.wordScrollOffset;
            return item.x >= visibleLeft - 1
                && item.x + item.width <= visibleLeft + line.width + 1;
        });
    }

    // The existing whole-line regression, repeated for the word path: leaving
    // marquee mode has to land back at zero there too.
    function test_wordScrollOffsetClearsWhenLeavingMarqueeMode() {
        const wordTimings = [];
        let text = "";
        for (let i = 0; i < 40; ++i) {
            wordTimings.push({ startMs: i * 100, endMs: (i + 1) * 100, text: "word" + i + " " });
            text += "word" + i + " ";
        }
        const line = createTemporaryObject(wordLineComponent, this, {
            lineText: text, words: wordTimings, overflowMode: "marquee", positionMs: 3500
        });
        tryVerify(() => line.wordScrollOffset < 0);
        line.overflowMode = "fit";
        tryCompare(line, "wordScrollOffset", 0);
    }

    function test_secondLineSelectorPicksTranslationRomanizationOrNothing() {
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentTranslation: "translated", currentRomanization: "romanized" });
        const view = createTemporaryObject(lyricsViewComponent, this, { source: source });
        verify(view !== null);

        compare(view.effectiveSecondLine, "translated");
        view.secondLineSource = "romanization";
        compare(view.effectiveSecondLine, "romanized");
        view.showTranslation = false;
        compare(view.effectiveSecondLine, "");
    }

    // Every source but one carries no romanization at all, so this is the
    // normal case rather than a transient one: the row goes empty instead of
    // quietly falling back to the translation, which would make the setting
    // mean different things on different tracks.
    function test_romanizationMissingLeavesTheSecondLineEmpty() {
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentTranslation: "translated", currentRomanization: "" });
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, secondLineSource: "romanization" });
        compare(view.effectiveSecondLine, "");
    }

    function test_secondLineColorFollowsTheLyricUnlessOverridden() {
        const source = createTemporaryObject(fakeSourceComponent, this);
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, textColor: "#ff3366" });
        // Unset, it keeps the derived alpha the second line has always used.
        fuzzyCompare(view.effectiveSecondLineColor.r, 1, 0.01);
        fuzzyCompare(view.effectiveSecondLineColor.a, 0.68, 0.01);

        view.secondLineColorEnabled = true;
        view.secondLineColor = "#8000ff00";
        compare(view.effectiveSecondLineColor.toString(), "#8000ff00");
    }

    // The setting exists to restore DESIGN.md decision 38's wakeup profile, so
    // "off" has to mean the model is empty and the clock is stopped -- not
    // merely that nothing is visible. Anything less and the widget still wakes
    // 60 times a second for a song it is rendering as one whole line.
    function test_wordByWordOffEmptiesTheModelAndStopsTheClock() {
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentWords: twoWords });
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: true });
        verify(view !== null);
        compare(view.effectiveWords.length, 2);
        compare(view.wordClockRunning, true);
        tryVerify(() => findAll(view, o => o.objectName === "lyricWord").length === 2);

        view.wordByWord = false;
        compare(view.effectiveWords.length, 0);
        compare(view.wordClockRunning, false);
        tryVerify(() => findAll(view, o => o.objectName === "lyricWord").length === 0);

        // Turning the effects off instead is deliberately NOT equivalent: the
        // colours and envelopes go quiet but the clock keeps running, which is
        // the reason this setting had to exist separately.
        view.wordByWord = true;
        view.wordLift = false;
        view.wordBrightness = false;
        view.wordBlurGlow = false;
        compare(view.wordClockRunning, true);
    }

    // wordClockRunning's second conjunct: a paused (or otherwise non-Playing)
    // track must not keep the frame-driven clock awake, even with words on
    // screen. Pins this independently of the first conjunct, which
    // test_wordByWordOffEmptiesTheModelAndStopsTheClock already owns.
    function test_pausedPlaybackStopsTheWordClock() {
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentWords: twoWords, playbackStatus: "Playing" });
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: true });
        verify(view !== null);
        compare(view.effectiveWords.length, 2);
        compare(view.wordClockRunning, true);

        source.playbackStatus = "Paused";
        // Mutation check: deleting the playbackStatus conjunct from
        // wordClockRunning leaves this comparing true against true, and only
        // this test goes red.
        compare(view.wordClockRunning, false);

        source.playbackStatus = "Playing";
        compare(view.wordClockRunning, true);
    }

    // wordClockRunning's third conjunct: with the widget hidden on the
    // desktop (not in a panel, and shouldBeVisible false) the clock must not
    // run even though words are loaded and playback is active. Pins this
    // independently of the other two conjuncts.
    function test_hiddenDesktopWidgetStopsTheWordClock() {
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentWords: twoWords, playbackStatus: "Playing" });
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: false, shouldBeVisible: true });
        verify(view !== null);
        compare(view.effectiveWords.length, 2);
        compare(view.wordClockRunning, true);

        view.shouldBeVisible = false;
        // Mutation check: deleting the (panelMode || shouldBeVisible)
        // conjunct from wordClockRunning leaves this comparing true against
        // true, and only this test goes red.
        compare(view.wordClockRunning, false);

        view.shouldBeVisible = true;
        compare(view.wordClockRunning, true);
    }

    // A line that cannot be made to fit even at the minimum pixel size has no
    // way to show its current word in "fit" or "elide": the row stays wider
    // than the item, x pins to 0, and clip cuts the tail off -- the sung word
    // simply vanishes, with no ellipsis and no scrolling to bring it back.
    // The whole-line path shrinks and elides instead, so word mode has to step
    // aside rather than lose the one word that matters.
    function test_aLineTooLongToFitFallsBackToTheWholeLine() {
        const words = [];
        let text = "";
        // Sixty, where twenty-two is already over the threshold on a machine
        // with a CJK font installed: the fallback is decided on
        // `metrics.width * minimumPixelSize / fontSize <= width` (LyricLine's
        // wordMode), so at width 300 and fontSize 34 the line has to measure
        // wider than 510px. A container with no CJK font -- which is what the
        // Debian CI job is, it carries DejaVu and nothing else -- draws every
        // one of these as a narrow .notdef box instead of a full-width glyph,
        // and twenty-two of those measure ~440px, short of the threshold, so
        // the line still fit and this test failed there and nowhere else. The
        // count is deliberately generous rather than tuned: what is being
        // asserted is "no pixel size can make this fit", which must not turn
        // on which fonts the machine running the suite happens to have.
        for (let i = 0; i < 60; ++i) {
            words.push({ startMs: i * 200, endMs: (i + 1) * 200, text: "字" });
            text += "字";
        }
        const modes = ["fit", "elide"];
        for (let m = 0; m < modes.length; ++m) {
            const line = createTemporaryObject(wordLineComponent, this, {
                width: 300, fontSize: 34, lineText: text, words: words,
                overflowMode: modes[m], positionMs: 3800
            });
            verify(line !== null);
            verify(!line.wordMode);
            compare(wordTextsOf(line).length, 0);
            compare(textChildrenOf(line)[0].text, text);
        }

        // A line that does fit once shrunk keeps word-by-word: the fallback is
        // for the impossible case only, not for every line that needs shrinking.
        const shortWords = [{ startMs: 0, endMs: 500, text: "字" },
                            { startMs: 500, endMs: 1000, text: "字" }];
        const fits = createTemporaryObject(wordLineComponent, this, {
            width: 300, fontSize: 34, lineText: "字字", words: shortWords,
            overflowMode: "fit", positionMs: 100
        });
        verify(fits.wordMode);

        // marquee is the escape hatch: it scrolls, so the current word is
        // always reachable however long the line is.
        const scrolls = createTemporaryObject(wordLineComponent, this, {
            width: 300, fontSize: 34, lineText: text, words: words,
            overflowMode: "marquee", positionMs: 3800
        });
        verify(scrolls.wordMode);
    }

    function test_wordTimingsOnlyApplyToTheLyricItself() {
        const source = createTemporaryObject(fakeSourceComponent, this,
            { currentWords: twoWords });
        const view = createTemporaryObject(lyricsViewComponent, this, { source: source });
        compare(view.effectiveWords.length, 2);

        // "Searching…", the idle text and the custom empty-state messages all
        // land in the same slot; handing them the line's word timings would
        // highlight fragments of them.
        source.lyricState = "searching";
        compare(view.effectiveWords.length, 0);
        source.lyricState = "ok";
        source.playbackStatus = "Stopped";
        compare(view.effectiveWords.length, 0);
    }

    function test_blurredGlowIsOffByDefaultAndFollowsEachWordUntilItSettles() {
        const line = createTemporaryObject(wordLineComponent, this,
            { words: twoWords, positionMs: 500 });
        const loaders = findAll(line, o => o.active !== undefined && o.sourceComponent !== undefined);
        compare(loaders.length, 2);
        // Default off: no MultiEffect anywhere, which is the "pure brightening,
        // zero shaders" resting state.
        compare(loaders.filter(l => l.active).length, 0);

        line.blurGlowEnabled = true;
        // One halo: the first word is being sung, the second has not started.
        compare(loaders.filter(l => l.active).length, 1);
        // Two: the second word is being sung while the first is still coming
        // down. The halo lives while the glyph moves visibly, not only while
        // the word is sung, so it fades with the glyph instead of vanishing
        // at endMs with the glyph still in the air (decision 73).
        line.positionMs = 1200;
        compare(loaders.filter(l => l.active).length, 2);
        // Back to one at endMs + liftReleaseMs: the halo's window closes
        // there, before the glyph has settled, because the release crossed
        // zero at ~210 ms and the clipped glow never exceeds ~1% afterwards.
        // The glyph itself is still in flight at that point.
        line.positionMs = 1000 + line.liftReleaseMs;
        compare(loaders.filter(l => l.active).length, 1);
        verify(line.liftEnvelope(1000 + line.liftReleaseMs, 0, 1000) !== 0);
        // ...and none once the last word's window has closed too. The window
        // is time-based and closes for good; a threshold on the envelope's
        // value would re-open it at each zero crossing of the release.
        line.positionMs = 2000 + line.liftReleaseMs;
        compare(loaders.filter(l => l.active).length, 0);

        // A zero-length token never moves, so it never gets a halo either:
        // 1.4% of real words are zero-length (trailing punctuation), and each
        // would otherwise hold an invisible MultiEffect for the whole window.
        line.words = [{ startMs: 0, endMs: 0, text: "…" }, { startMs: 0, endMs: 1000, text: "abc" }];
        line.positionMs = 100;
        const rebuilt = findAll(line, o => o.active !== undefined && o.sourceComponent !== undefined);
        compare(rebuilt.length, 2);
        compare(rebuilt.filter(l => l.active).length, 1);
    }

    function test_brighteningMovesTheColorOfTheWordBeingSung() {
        const line = createTemporaryObject(wordLineComponent, this, {
            words: twoWords, positionMs: 500, activeColor: "#b0ffffff",
            brightnessEnabled: false
        });
        const texts = wordTextsOf(line);
        compare(texts[0].color.toString(), "#b0ffffff");

        line.brightnessEnabled = true;
        // Qt.lighter() would be inert here -- these colours already sit at HSV
        // value 1.0 -- so the brightening has to move the alpha too. The
        // target is computed, not eyeballed: at positionMs 500 the spring has
        // settled (within 2% of 1 after about 255 ms at ζ 0.59), the envelope
        // is clipped to 1, strength 0.6, so 0.690196 + (1 - 0.690196) × 0.6.
        // An earlier "> 0.69" here was worthless -- 0xb0/255 = 0.690196 is
        // already greater than it, so the assertion held with the entire
        // brightening mechanism deleted.
        fuzzyCompare(texts[0].color.a, 0.876, 0.01);
        // At the peak the envelope is 1.1 and the brightening clips it to 1.
        // Unclipped it would read 0.690196 + 0.309804 × 0.66 = 0.8947, which
        // the tolerance here rejects; the 500 ms probe above cannot tell the
        // two apart because the envelope has settled to 1 there.
        line.positionMs = line.liftArriveMs;
        fuzzyCompare(texts[0].color.a, 0.876, 0.005);
        // Held, not a hump: still fully bright just before the word ends. The
        // sin(p·π) envelope this replaced was back at the base colour here.
        line.positionMs = 999;
        fuzzyCompare(texts[0].color.a, 0.876, 0.01);
        // The release dips under the baseline at endMs + liftReleaseMs; the
        // brightening clips the envelope to 0 there, so a word already in the
        // sung colour is exactly the sung colour, never darker than it.
        line.positionMs = 1000 + line.liftReleaseMs;
        compare(texts[0].color.toString(), "#a0ffffff");
        // And once settled it stays there.
        line.positionMs = 1000 + line.liftSettleMs + 1;
        compare(texts[0].color.toString(), "#a0ffffff");
    }

    // The bounds below are derived from the three constants, not eyeballed:
    // 10% overshoot gives ζ ≈ 0.59, the first peak lands at liftArriveMs by
    // construction, the system is within 2% of rest after -ln(0.02)/(ζω) ≈
    // 255 ms, and the release reaches its lowest point -- liftOvershoot of the
    // released height under the baseline -- liftReleaseMs after endMs.
    function test_liftEnvelopeIsATimeBasedSpringNotAProgressCurve() {
        const line = createTemporaryObject(wordLineComponent, this, { words: twoWords });
        // Decision 73's constants, pinned directly: every other assertion in
        // this file reads them back through the component, so a changed
        // constant would move the probe points along with it and pass.
        compare(line.liftArriveMs, 150);
        compare(line.liftOvershoot, 0.10);
        compare(line.liftReleaseMs, 300);
        fuzzyCompare(line.liftSettleMs, 628, 1);
        compare(line.liftEnvelope(-1, 0, 1000), 0);
        compare(line.liftEnvelope(0, 0, 1000), 0);
        fuzzyCompare(line.liftEnvelope(line.liftArriveMs, 0, 1000), 1 + line.liftOvershoot, 0.005);
        // Settled and held for as long as the word is sung.
        fuzzyCompare(line.liftEnvelope(500, 0, 1000), 1, 0.02);
        fuzzyCompare(line.liftEnvelope(999, 0, 1000), 1, 0.02);
        // Time-based: the same instant reads the same on a five-second word.
        // A progress-based curve would put 150 ms of a 5 s word at 3%.
        compare(line.liftEnvelope(150, 0, 5000), line.liftEnvelope(150, 0, 1000));
        // Release: lowest point liftReleaseMs after endMs, exactly 0 once
        // settled -- not merely small, because a sub-pixel y that keeps
        // changing would re-render every sung word on every frame.
        fuzzyCompare(line.liftEnvelope(1000 + line.liftReleaseMs, 0, 1000), -line.liftOvershoot, 0.01);
        verify(Math.abs(line.liftEnvelope(1000 + line.liftSettleMs - 1, 0, 1000)) < 0.012);
        compare(line.liftEnvelope(1000 + line.liftSettleMs, 0, 1000), 0);
        // A word shorter than the arrive time is released part-way up, from
        // wherever it got to, and dips by its own share of the overshoot.
        // 50 ms into the rise the closed form gives
        // 1 − e^(−ζω·0.05)·(cos(ω_d·0.05) + (ζ/√(1−ζ²))·sin(ω_d·0.05)) with
        // ζ 0.5912, ω 25.97 rad/s, ω_d 20.95 rad/s, i.e. 0.4734. This is the
        // one probe on the curve's mid-rise shape: at the peak, on the plateau
        // and at the release's lowest point the sine term is zero or the
        // exponential negligible, so a wrong ζ/√(1−ζ²) coefficient shows up
        // only here (with the coefficient dropped it reads 0.530).
        const short50 = line.liftEnvelope(50, 0, 50);
        fuzzyCompare(short50, 0.4734, 0.01);
        fuzzyCompare(line.liftEnvelope(50 + line.liftReleaseMs, 0, 50), -short50 * line.liftOvershoot, 0.005);

        // And the glyph follows it: at fontSize 40, liftEm 0.14 the peak is
        // 40 × 0.14 × 1.1 = 6.16 px up, the release's lowest point 0.56 px
        // down, and rest is exactly 0.
        line.liftEnabled = true;
        line.positionMs = line.liftArriveMs;
        const texts = wordTextsOf(line);
        fuzzyCompare(texts[0].y, -40 * line.liftEm * (1 + line.liftOvershoot), 0.05);
        line.positionMs = 1000 + line.liftReleaseMs;
        fuzzyCompare(texts[0].y, 40 * line.liftEm * line.liftOvershoot, 0.05);
        line.positionMs = 1000 + line.liftSettleMs;
        compare(texts[0].y, 0);
    }

    function test_wholeLineStrokeIsUnchangedAndWordStrokeUsesTextOutline() {
        const whole = createTemporaryObject(wordLineComponent, this, { strokeEnabled: true });
        // Unchanged: eight offset copies plus the line itself.
        tryVerify(() => textChildrenOf(whole).length === 9);

        const word = createTemporaryObject(wordLineComponent, this,
            { words: twoWords, positionMs: 500, strokeEnabled: true });
        // Per word it is Text's own outline instead. Eight copies per word was
        // measured at 9.5 ms to rebuild on a line change against 2.7 ms for
        // this, and the spike lands exactly on the line change.
        compare(textChildrenOf(word).length, 1);
        const glyphs = wordTextsOf(word);
        compare(glyphs.length, 2);
        for (let i = 0; i < glyphs.length; ++i) {
            compare(glyphs[i].style, Text.Outline);
        }
    }

    function test_aRepeatedLineWithNewWordTimingsStillSwitches() {
        const first = [{ startMs: 0, endMs: 100, text: "a" }];
        const second = [{ startMs: 5000, endMs: 5100, text: "a" }];
        const lyric = createTemporaryObject(animatedLyricComponent, this,
            { lyricText: "chorus", translationText: "", words: first });
        verify(lyric !== null);
        tryCompare(lyric, "shownText", "chorus");
        tryVerify(() => lyric.shownWords.length === 1 && lyric.shownWords[0].startMs === 0);

        // A refrain repeats the same text with new timings. switchLine() bails
        // out early when nothing changed, and text alone is not enough to tell
        // these two apart -- without the word check the highlight would carry
        // the previous line's progress into the new one.
        lyric.words = second;
        tryVerify(() => lyric.shownWords.length === 1 && lyric.shownWords[0].startMs === 5000);
    }

    // This does NOT pin the binding loop -- reverting these bindings alone
    // does not reproduce it (the trigger is reading liftSupported in the lift
    // row's `visible`; see the comment there). What it does pin is the thing
    // that made the row wrong in the first place: the panel page has no lift
    // keys at all, so every lift control there must be off and stay off. A
    // later edit that binds wordLift to something real on this page would be
    // storing into a key that does not exist.
    function test_thePanelPageDeclaresNoLift() {
        const panelPage = createTemporaryObject(configPanelAppearanceComponent, this);
        const panel = findAll(panelPage, o => typeof o.wordActiveColorEdited === "function")[0];
        verify(panel !== undefined);
        compare(panel.liftSupported, false);
        compare(panel.wordLift, false);
        compare(panel.wordLiftRowVisible, false);

        const desktopPage = createTemporaryObject(configDesktopAppearanceComponent, this);
        const desktop = findAll(desktopPage, o => typeof o.wordActiveColorEdited === "function")[0];
        compare(desktop.liftSupported, true);
    }

    function test_wordSettingsReachTheirOwnConfigProperties() {
        const desktopPage = createTemporaryObject(configDesktopAppearanceComponent, this);
        const panelPage = createTemporaryObject(configPanelAppearanceComponent, this);
        const desktop = findAll(desktopPage, o => typeof o.wordActiveColorEdited === "function")[0];
        const panel = findAll(panelPage, o => typeof o.wordActiveColorEdited === "function")[0];
        verify(desktop !== undefined);
        verify(panel !== undefined);

        desktop.wordByWordEdited(false);
        desktop.wordUnsungColorEdited("#11223344");
        desktop.wordActiveColorEdited("#55667788");
        desktop.wordSungColorEdited("#99aabbcc");
        desktop.wordLiftEdited(false);
        desktop.wordLiftPercentEdited(22);
        desktop.wordBrightnessPercentEdited(85);
        desktop.wordBlurGlowEdited(true);
        desktop.lineHeightPercentEdited(160);
        desktop.secondLineSourceEdited("romanization");
        desktop.secondLineColorEnabledEdited(true);
        desktop.secondLineColorEdited("#80ff0000");
        compare(desktopPage.cfg_desktopWordByWord, false);
        compare(desktopPage.cfg_desktopWordUnsungColor, "#11223344");
        compare(desktopPage.cfg_desktopWordActiveColor, "#55667788");
        compare(desktopPage.cfg_desktopWordSungColor, "#99aabbcc");
        compare(desktopPage.cfg_desktopWordLift, false);
        compare(desktopPage.cfg_desktopWordLiftPercent, 22);
        compare(desktopPage.cfg_desktopWordBrightnessPercent, 85);
        compare(desktopPage.cfg_desktopWordBlurGlow, true);
        compare(desktopPage.cfg_desktopLineHeight, 160);
        compare(desktopPage.cfg_desktopSecondLineSource, "romanization");
        compare(desktopPage.cfg_desktopSecondLineColorEnabled, true);
        compare(desktopPage.cfg_desktopSecondLineColor, "#80ff0000");

        // The panel tab carries no lift keys at all -- decision 40's precedent
        // for panelHideAnimationMs -- so its row stands disabled instead.
        compare(panel.liftSupported, false);
        compare(desktop.liftSupported, true);

        panel.wordActiveColorEdited("#cafebabe");
        compare(panelPage.cfg_panelWordActiveColor, "#cafebabe");
        // Same crosstalk guard the other appearance tests apply: the two tabs
        // are separate page instances and must not reach into each other.
        compare(desktopPage.cfg_desktopWordActiveColor, "#55667788");
    }

    // DESIGN.md decision 69/73: below 125% line height, the previous line's
    // descenders can reach into the next line's ink -- the desktop form
    // factor's stored lineHeightPercent is therefore clamped up to 125% at
    // render time, in LyricsView rather than main.qml (a PlasmoidItem the
    // suite cannot instantiate, per CLAUDE.md). The panel keeps the 100%
    // floor because its height is not this widget's to grow.
    function test_lineHeightPercentClampsToLineHeightMinPercent() {
        const source = createTemporaryObject(fakeSourceComponent, this);

        // A stored value below the desktop floor is clamped up.
        const desktop = createTemporaryObject(lyricsViewComponent, this,
            { source: source, lineHeightPercent: 100, lineHeightMinPercent: 125 });
        verify(desktop !== null);
        const desktopLyric = findAll(desktop, o => o.shownText !== undefined)[0];
        verify(desktopLyric !== undefined);
        compare(desktopLyric.lineHeightFactor, 1.25);

        // lineHeightMinPercent left at its default (100, main.qml's panel
        // instance never sets it): an equally low stored value passes
        // through unclamped.
        const panel = createTemporaryObject(lyricsViewComponent, this,
            { source: source, lineHeightPercent: 100 });
        verify(panel !== null);
        const panelLyric = findAll(panel, o => o.shownText !== undefined)[0];
        verify(panelLyric !== undefined);
        compare(panelLyric.lineHeightFactor, 1.0);

        // And a stored value already above the floor is not pulled down to
        // it -- this only ever raises, never overrides.
        const desktopAbove = createTemporaryObject(lyricsViewComponent, this,
            { source: source, lineHeightPercent: 160, lineHeightMinPercent: 125 });
        const desktopAboveLyric = findAll(desktopAbove, o => o.shownText !== undefined)[0];
        verify(desktopAboveLyric !== undefined);
        compare(desktopAboveLyric.lineHeightFactor, 1.6);
    }

    // The SpinBox itself carries the same 125 floor on the desktop config
    // page, so a user cannot even ask for less than the render-time clamp
    // already enforces. AppearanceSection is shared by both form factors
    // (AppearanceSection.qml's lineHeightMin default is the panel's 100),
    // so this also guards against the desktop page's override leaking into
    // the panel page, which is a separate component instance.
    function test_lineHeightSpinBoxFloorDiffersByFormFactor() {
        const desktopPage = createTemporaryObject(configDesktopAppearanceComponent, this);
        const panelPage = createTemporaryObject(configPanelAppearanceComponent, this);
        const desktopSpinBox = findAll(desktopPage, o => o.objectName === "lineHeightSpinBox")[0];
        const panelSpinBox = findAll(panelPage, o => o.objectName === "lineHeightSpinBox")[0];
        verify(desktopSpinBox !== undefined);
        verify(panelSpinBox !== undefined);
        compare(desktopSpinBox.from, 125);
        compare(panelSpinBox.from, 100);
    }

    function test_textConfigurationUsesOneEmptyFallbackSwitch() {
        const page = createTemporaryObject(configTextComponent, this);
        verify(page !== null);

        const cases = [
            { text: "cfg_idleText",
              state: "idle", viewText: "idleText", custom: "resting" },
            { text: "cfg_notFoundText",
              state: "not-found", viewText: "notFoundText", custom: "missing" },
            { text: "cfg_noLyricText",
              state: "no-lyric", viewText: "noLyricText", custom: "instrumental" },
            { text: "cfg_networkErrorText",
              state: "network-error", viewText: "networkErrorText", custom: "offline" }
        ];
        const fields = findAll(page, o => typeof o.textEdited === "function"
            && o.placeholderText !== undefined);
        compare(fields.length, 4);
        const switches = findAll(page,
            o => o.objectName === "emptyTextUseDefaultCheckBox");
        compare(switches.length, 1);
        compare(page.cfg_emptyTextUseDefault, true);

        for (let i = 0; i < cases.length; ++i) {
            const current = cases[i];
            const field = fields[i];

            field.text = current.custom;
            field.textEdited();
            compare(page[current.text], current.custom);
            compare(page.cfg_emptyTextUseDefault, true);
            compare(TextPolicy.effectiveText(page.cfg_emptyTextUseDefault,
                                             page[current.text], "localized default"),
                    current.custom);

            field.text = "";
            field.textEdited();
            compare(page[current.text], "");
            compare(TextPolicy.effectiveText(page.cfg_emptyTextUseDefault,
                                             page[current.text], "localized default"),
                    "localized default");
        }

        switches[0].checked = false;
        switches[0].toggled();
        compare(page.cfg_emptyTextUseDefault, false);

        // With the one switch off, every empty field renders empty. A custom
        // nonempty value remains authoritative regardless of the switch.
        for (let i = 0; i < cases.length; ++i) {
            const current = cases[i];
            const effective = TextPolicy.effectiveText(page.cfg_emptyTextUseDefault,
                                                        page[current.text], "localized default");
            compare(effective, "");
            const sourceProperties = current.state === "idle"
                ? { playbackStatus: "Stopped" }
                : { lyricState: current.state };
            const source = createTemporaryObject(fakeSourceComponent, this,
                sourceProperties);
            const props = { source: source };
            props[current.viewText] = effective;
            const view = createTemporaryObject(lyricsViewComponent, this, props);
            verify(view !== null);
            compare(view.effectiveText, "");
        }
        compare(TextPolicy.effectiveText(page.cfg_emptyTextUseDefault,
                                         "custom", "localized default"),
                "custom");
    }

    function test_textConfigurationMigration() {
        const upgraded = createTemporaryObject(textConfigurationComponent, this,
            { notFoundText: "legacy custom text" });
        verify(upgraded !== null);
        compare(TextPolicy.migrateConfiguration(upgraded), true);
        compare(upgraded.notFoundTextUseDefault, false);
        compare(upgraded.emptyTextUseDefault, true);
        compare(upgraded.textConfigVersion, 2);
        compare(TextPolicy.effectiveText(upgraded.emptyTextUseDefault,
                                         upgraded.notFoundText, "localized default"),
                "legacy custom text");

        // Once migrated, a later user choice must not be overwritten.
        upgraded.emptyTextUseDefault = false;
        compare(TextPolicy.migrateConfiguration(upgraded), false);
        compare(upgraded.emptyTextUseDefault, false);

        const mixed = createTemporaryObject(textConfigurationComponent, this, {
            idleTextUseDefault: false,
            notFoundTextUseDefault: false,
            noLyricTextUseDefault: false,
            networkErrorTextUseDefault: true,
            textConfigVersion: 1
        });
        verify(mixed !== null);
        compare(TextPolicy.migrateConfiguration(mixed), true);
        compare(mixed.emptyTextUseDefault, true);
        compare(mixed.textConfigVersion, 2);

        const allDisabled = createTemporaryObject(textConfigurationComponent, this, {
            idleTextUseDefault: false,
            notFoundTextUseDefault: false,
            noLyricTextUseDefault: false,
            networkErrorTextUseDefault: false,
            textConfigVersion: 1
        });
        verify(allDisabled !== null);
        compare(TextPolicy.migrateConfiguration(allDisabled), true);
        compare(allDisabled.emptyTextUseDefault, false);
        compare(allDisabled.textConfigVersion, 2);

        const fresh = createTemporaryObject(textConfigurationComponent, this);
        verify(fresh !== null);
        compare(TextPolicy.migrateConfiguration(fresh), true);
        compare(fresh.emptyTextUseDefault, true);
        compare(fresh.textConfigVersion, 2);
    }

    function test_restartFeedbackAndCleanRetryButton() {
        const feedback = createTemporaryObject(restartFeedbackComponent, this);
        verify(feedback !== null);
        compare(feedback.shouldShow, false);
        feedback.succeeded = true;
        compare(feedback.shouldShow, true);
        compare(feedback.type, Kirigami.MessageType.Positive);
        verify(feedback.text.length > 0);
        feedback.succeeded = false;
        feedback.failed = true;
        feedback.errorText = "exit 23";
        compare(feedback.shouldShow, true);
        compare(feedback.type, Kirigami.MessageType.Error);
        verify(feedback.text.indexOf("exit 23") >= 0);

        const page = createTemporaryObject(configBackendComponent, this);
        verify(page !== null);
        const restartButtons = findAll(page,
            o => o.objectName === "restartServiceButton");
        compare(restartButtons.length, 1);
        // BackendConfig starts clean after load.  A failed restart must still
        // leave this action available without manufacturing another edit.
        compare(restartButtons[0].enabled, true);
    }

    function test_trackInfoOutlineReachesEveryCopy() {
        const info = createTemporaryObject(trackInfoComponent, this);
        info.strokeEnabled = true;
        info.fontWeight = Font.Black;
        info.textColor = "#112233";
        info.strokeColor = "#ffcc00";
        const titleLine = info.children[0];
        tryVerify(() => textChildrenOf(titleLine).length === 9);
        const texts = textChildrenOf(titleLine);
        for (let i = 0; i < texts.length; ++i) {
            compare(texts[i].font.weight, Font.Black);
        }
        const strokeCount = texts.filter(t => t.color.toString() === info.strokeColor.toString()).length;
        const mainCount = texts.filter(t => t.color.toString() === info.textColor.toString()).length;
        compare(strokeCount, 8);
        compare(mainCount, 1);
    }

    // DESIGN.md decision 40 names the self-drawn plate as the highest
    // silent-regression-risk part of that refactor: LyricsView.qml now
    // draws "ksvg" itself instead of leaving it to the shell, and folds the
    // plate's own margins into implicit/minimum size so the on-screen
    // footprint stays put. Two things could regress invisibly with no test
    // here: the plate painting somewhere it should not (or not painting
    // where it should), and the margin arithmetic double-counting or
    // dropping the plate's contribution.
    function test_selfDrawnPlateOnlyOnDesktopKsvg() {
        // Deliberately does not also assert on the KSvg.FrameSvgItem's own
        // `.visible` (which is bound in LyricsView.qml as a one-line
        // `visible: root.selfDrawnPlate`, so selfDrawnPlate's own
        // correctness is what actually matters): Item.visible is
        // ancestor-combined, and this TestCase's root item reads `visible
        // === false` even once windowShown fires -- the same reason
        // test_trackInfoStaysUpThroughBlankLyricStates below checks height
        // rather than visible. Confirmed directly: every dynamically
        // created item's `.visible` reads false here regardless of its own
        // binding, so it cannot distinguish correct from broken.
        //
        // ownsPlate is the third dimension and the only one that says
        // "true" together with ksvg+desktop. main.qml hands the plate over
        // only for the duration of a fade: on a theme that ships blurred-*
        // elements the shell's version blurs the wallpaper behind the frame
        // and ours cannot, so it goes straight back once the fade is done.
        const modes = ["none", "ksvg", "solid"];
        for (let m = 0; m < modes.length; ++m) {
            for (let p = 0; p < 2; ++p) {
                for (let a = 0; a < 2; ++a) {
                    const plateMode = modes[m];
                    const panelMode = p === 1;
                    const ownsPlate = a === 1;
                    const expected = plateMode === "ksvg" && !panelMode && ownsPlate;
                    const source = createTemporaryObject(fakeSourceComponent, this);
                    const view = createTemporaryObject(lyricsViewComponent, this, {
                        source: source, plateMode: plateMode, panelMode: panelMode,
                        ownsPlate: ownsPlate });
                    verify(view !== null);
                    const label = `plateMode=${plateMode} panelMode=${panelMode}`
                        + ` ownsPlate=${ownsPlate}`;
                    compare(view.selfDrawnPlate, expected, label);
                }
            }
        }
    }

    function test_selfDrawnPlateMarginsFoldIntoSizeOnlyWhenSelfDrawn() {
        const modes = ["none", "ksvg", "solid"];
        const panelModes = [false, true];
        for (let p = 0; p < panelModes.length; ++p) {
            for (let m = 0; m < modes.length; ++m) {
              for (let a = 0; a < 2; ++a) {
                const panelMode = panelModes[p];
                const plateMode = modes[m];
                const ownsPlate = a === 1;
                const source = createTemporaryObject(fakeSourceComponent, this);
                const view = createTemporaryObject(lyricsViewComponent, this, {
                    source: source, plateMode: plateMode, panelMode: panelMode,
                    ownsPlate: ownsPlate });
                verify(view !== null);
                const label = `plateMode=${plateMode} panelMode=${panelMode}`
                    + ` ownsPlate=${ownsPlate}`;
                const plate = view.children[view.children.length - 1];
                const trackInfo = view.children[2].children[0];

                // Reads the plate's own actual margins rather than
                // hardcoding pixel values (theme-dependent) -- what is under
                // test is whether the `selfDrawnPlate ? ... : 0` gating
                // applies them at all, not their magnitude. A regression
                // that folds them in unconditionally would still show up as
                // a mismatch on the "none"/"solid" and panel rows below,
                // where the gate must contribute zero.
                const marginW = view.selfDrawnPlate ? plate.margins.horizontal : 0;
                const marginH = view.selfDrawnPlate ? plate.margins.vertical : 0;
                const baseWidth = panelMode ? Kirigami.Units.gridUnit * 14 : Kirigami.Units.gridUnit * 28;
                const baseHeight = panelMode ? Kirigami.Units.gridUnit * 2 : Kirigami.Units.gridUnit * 7.5;
                const baseMinWidth = panelMode
                    ? Math.min(Kirigami.Units.gridUnit * 8, view.panelWidth) : Kirigami.Units.gridUnit * 18;
                const baseMinHeight = trackInfo.implicitHeight + Kirigami.Units.gridUnit * 2;

                compare(view.implicitWidth, baseWidth + marginW, label);
                compare(view.implicitHeight, baseHeight + marginH, label);
                compare(view.Layout.minimumWidth, baseMinWidth + marginW, label);
                compare(view.Layout.minimumHeight, baseMinHeight + marginH, label);

                // On desktop with "ksvg", the plate must actually contribute
                // something real -- otherwise this test would pass equally
                // well against a version that always adds zero.
                if (plateMode === "ksvg" && !panelMode && ownsPlate) {
                    verify(marginW > 0, label);
                    verify(marginH > 0, label);
                }
              }
            }
        }
    }

    function test_panelWidthDrivesPreferredWidthOnlyInThePanel() {
        // AppletQuickItem forwards Layout.preferredWidth up to the applet
        // container, but only in the panel: the desktop path must keep -1
        // (the QQuickLayouts "unset" sentinel), or GridLayoutManager would
        // grow every existing hand-shrunk desktop widget on next login.
        const source = createTemporaryObject(fakeSourceComponent, this);
        const panelView = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: true, panelWidth: 400 });
        verify(panelView !== null);
        compare(panelView.Layout.preferredWidth, panelView.panelWidth);

        const desktopView = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: false, panelWidth: 400 });
        verify(desktopView !== null);
        compare(desktopView.Layout.preferredWidth, -1);

        // A panelWidth below the gridUnit*8 floor must actually win the
        // Math.min -- otherwise this would pass equally well against a
        // version that never applied it. selfDrawnPlate is always false
        // here (it requires !panelMode), so no plate margin to add.
        const narrowView = createTemporaryObject(lyricsViewComponent, this,
            { source: source, panelMode: true, panelWidth: 100 });
        verify(narrowView !== null);
        compare(narrowView.Layout.minimumWidth, Math.min(Kirigami.Units.gridUnit * 8, narrowView.panelWidth));
    }
}
