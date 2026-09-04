import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import "../package/contents/ui" as LyricsUi
import "../package/contents/ui/config" as LyricsConfig

TestCase {
    name: "Appearance"
    when: windowShown

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

    Component {
        id: configAppearanceComponent
        LyricsConfig.ConfigAppearance {}
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
        const page = createTemporaryObject(configAppearanceComponent, this);
        verify(page !== null);
        const sections = findAll(page, o => typeof o.textColorEdited === "function");
        // Desktop first, then panel -- decision 31 gives them separate kcfg
        // entries, and crossing the two only shows up as "the panel setting
        // moved the desktop widget".
        compare(sections.length, 2);
        const desktop = sections[0];
        const panel = sections[1];

        desktop.solidColorEdited("#80112233");
        desktop.textColorEdited("#123456");
        desktop.strokeColorEdited("#40445566");
        desktop.fontWeightEdited(Font.Black);
        compare(page.cfg_desktopSolidColor, "#80112233");
        compare(page.cfg_desktopTextColor, "#123456");
        compare(page.cfg_desktopStrokeColor, "#40445566");
        compare(page.cfg_desktopFontWeight, Font.Black);

        panel.textColorEdited("#abcdef");
        panel.fontWeightEdited(Font.Light);
        compare(page.cfg_panelTextColor, "#abcdef");
        compare(page.cfg_panelFontWeight, Font.Light);
        compare(page.cfg_desktopTextColor, "#123456");
        compare(page.cfg_desktopFontWeight, Font.Black);
    }

    function test_configValueReachesTheColorRow() {
        const page = createTemporaryObject(configAppearanceComponent, this);
        const sections = findAll(page, o => typeof o.textColorEdited === "function");
        page.cfg_desktopTextColor = "#0f0f0f";
        // Background, text, outline, track-info text, track-info outline --
        // in that order down the form. The track-info section added its own
        // text/outline colour pair, so this went from 3 rows to 5.
        const rows = findAll(sections[0], o => typeof o.edited === "function");
        compare(rows.length, 5);
        compare(rows[1].value, "#0f0f0f");

        // Same crosstalk bug test_sectionEditsReachTheirOwnConfigProperties
        // guards against, but for the new track-info keys.
        page.cfg_desktopTrackInfoColor = "#111111";
        page.cfg_panelTrackInfoColor = "#222222";
        compare(sections[0].trackInfoColor, "#111111");
        compare(sections[1].trackInfoColor, "#222222");
        sections[1].trackInfoColorEdited("#333333");
        compare(page.cfg_panelTrackInfoColor, "#333333");
        compare(page.cfg_desktopTrackInfoColor, "#111111");
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
}
