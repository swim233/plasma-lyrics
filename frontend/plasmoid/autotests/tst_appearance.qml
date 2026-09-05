import QtQuick
import QtQuick.Layouts
import QtTest
import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg
import "../package/contents/ui" as LyricsUi
import "../package/contents/ui/config" as LyricsConfig

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
    // font assertions have to look at every Text under the line, not just one.
    function textChildrenOf(item) {
        const out = [];
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i];
            if (child.font !== undefined && child.text !== undefined) {
                out.push(child);
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
        // Background, text, outline, track-info text, track-info outline --
        // in that order down the form. The track-info section added its own
        // text/outline colour pair, so this went from 3 rows to 5.
        const rows = findAll(desktopSections[0], o => typeof o.edited === "function");
        compare(rows.length, 5);
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
        const states = ["searching", "not-found", "filtered", "no-lyric"];
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
                const baseMinWidth = panelMode ? Kirigami.Units.gridUnit * 8 : Kirigami.Units.gridUnit * 18;
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
}
