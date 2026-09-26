import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtTest
import org.kde.kirigami as Kirigami
import io.github.swim233.lyrics
import "../package/contents/ui/config" as LyricsConfig
import "../package/contents/ui/FontPolicy.js" as FontPolicy
import "../package/contents/ui/ThemePolicy.js" as ThemePolicy

// The font pickers and weight rows on the appearance pages. Everything below
// runs against a stand-in catalog, so what is asserted does not depend on the
// fonts installed on the machine running the suite; only the page tests at
// the end use the real FontCatalog, because the pages hard-wire it.
//
// Every top-level declaration is a Component, for the reason
// tst_appearance.qml's header gives: an object instantiated while this file
// loads would be outside the "Binding loop" check in CMakeLists.txt.
TestCase {
    name: "FontPicker"
    when: windowShown

    Component {
        id: fakeCatalogComponent
        QtObject {
            id: catalog

            // Set by each test to Kirigami.Theme.defaultFont.family, which
            // FontPolicy falls back to and which weights() must know.
            property string systemFamily
            property var listed: ["Alpha Sans", "Beta Serif", "Gamma Mono", "Single Face"]
            // A name the family is stored under in another locale.
            property var aliases: ({ "阿尔法黑体": "Alpha Sans" })
            property var alternates: ({ "Alpha Sans": ["阿尔法黑体"], "Beta Serif": ["Beta Antiqua", "ベータ明朝"] })
            property var faces: ({
                "Alpha Sans": [
                    { weight: 300, styleName: "Light" },
                    { weight: 400, styleName: "Regular" },
                    { weight: 700, styleName: "Bold" }
                ],
                "Beta Serif": [
                    { weight: 300, styleName: "Light" },
                    { weight: 316, styleName: "DemiLight" },
                    { weight: 400, styleName: "Regular" },
                    { weight: 800, styleName: "ExtraBold" }
                ],
                "Gamma Mono": [
                    { weight: 400, styleName: "Regular" },
                    { weight: 700, styleName: "Bold" }
                ],
                "Single Face": [
                    { weight: 400, styleName: "Book" }
                ]
            })
            // Deliberately not families() order, so the tests can tell the
            // picker kept the catalog's ranking rather than re-sorting it.
            property var ranked: ({ "a": ["Gamma Mono", "Alpha Sans", "Beta Serif", "Single Face"] })

            function families() {
                return catalog.listed;
            }
            function alternateNames(family) {
                return catalog.alternates[family] || [];
            }
            function search(query) {
                const needle = query.trim();
                if (needle.length === 0) {
                    return catalog.listed;
                }
                if (catalog.ranked[needle] !== undefined) {
                    return catalog.ranked[needle];
                }
                return catalog.listed.filter(family => family.toLowerCase().includes(needle.toLowerCase()));
            }
            function resolveFamily(stored) {
                if (catalog.listed.includes(stored)) {
                    return stored;
                }
                return catalog.aliases[stored] || "";
            }
            // Every standard step, unless a test sets this to [] to stand
            // for a Plasma font set to one of Qt's generic names, whose faces
            // the real catalog does not know.
            property var systemFaces: {
                const all = [];
                for (let weight = 100; weight <= 900; weight += 100) {
                    all.push({ weight: weight, styleName: "Face" + weight });
                }
                return all;
            }
            function weights(family) {
                if (family === catalog.systemFamily) {
                    return catalog.systemFaces;
                }
                return catalog.faces[family] || [];
            }
            // FontCatalog::snapWeight()'s documented rule.
            function snapWeight(available, target) {
                const present = available.map(face => face.weight);
                if (present.length === 0 || present.includes(target)) {
                    return target;
                }
                const below = present.filter(weight => weight < target);
                const above = present.filter(weight => weight > target);
                const heaviestBelow = below.length > 0 ? Math.max(...below) : -1;
                const lightestAbove = above.length > 0 ? Math.min(...above) : -1;
                if (target >= 400 && target <= 500) {
                    const upTo500 = above.filter(weight => weight <= 500);
                    if (upTo500.length > 0) {
                        return Math.min(...upTo500);
                    }
                    return heaviestBelow >= 0 ? heaviestBelow : lightestAbove;
                }
                if (target < 400) {
                    return heaviestBelow >= 0 ? heaviestBelow : lightestAbove;
                }
                return lightestAbove >= 0 ? lightestAbove : heaviestBelow;
            }
        }
    }

    // An AppearanceSection wired the way the pages wire theirs: plain
    // properties standing in for the cfg_ values, the effective families
    // computed from them with FontPolicy, and every edit written back and
    // logged. In a shown window, so the popups open and the keyboard reaches
    // their search fields.
    Component {
        id: harnessComponent
        Window {
            id: harness

            required property var catalog
            property string fontFamily: ""
            property int fontWeight: 700
            property bool trackInfoFontSameAsLyrics: true
            property string trackInfoFontFamily: ""
            property int trackInfoFontWeight: 400
            // Whether the sections show the set in effect (DESIGN.md
            // decision 76); false stands for the other tab of the page.
            property bool setInEffect: true
            // Every edit the section emitted, in order, as "key=value".
            property var edits: []

            readonly property string lyricFamily: FontPolicy.lyricFamily(harness.catalog,
                harness.fontFamily, Kirigami.Theme.defaultFont.family)
            readonly property string trackInfoFamily: FontPolicy.trackInfoFamily(harness.catalog,
                harness.trackInfoFontSameAsLyrics, harness.trackInfoFontFamily,
                harness.lyricFamily, Kirigami.Theme.defaultFont.family)
            property alias section: section
            property alias trackInfoSection: trackInfoSection

            function log(entry) {
                harness.edits = harness.edits.concat([entry]);
            }

            width: Kirigami.Units.gridUnit * 45
            height: Kirigami.Units.gridUnit * 60
            visible: true

            QtObject { id: stubFontSize; property int value: 34 }
            QtObject { id: stubTrackInfoFontSize; property int value: 18 }

            // The two sections one above the other, as on the pages.
            ColumnLayout {
                width: Kirigami.Units.gridUnit * 39

                LyricsConfig.AppearanceSection {
                    id: section
                    fontCatalog: harness.catalog
                    fontSizeControl: stubFontSize
                    wordByWord: false

                    fontFamily: harness.fontFamily
                    lyricEffectiveFamily: harness.lyricFamily
                    fontWeight: harness.fontWeight
                    trackInfoFontSameAsLyrics: harness.trackInfoFontSameAsLyrics
                    trackInfoFontWeight: harness.trackInfoFontWeight
                    setInEffect: harness.setInEffect

                    onFontFamilyEdited: value => {
                        harness.log("fontFamily=" + value);
                        harness.fontFamily = value;
                    }
                    onFontWeightEdited: value => {
                        harness.log("fontWeight=" + value);
                        harness.fontWeight = value;
                    }
                    onTrackInfoFontWeightEdited: value => {
                        harness.log("trackInfoFontWeight=" + value);
                        harness.trackInfoFontWeight = value;
                    }
                }

                LyricsConfig.TrackInfoSection {
                    id: trackInfoSection
                    twinFormLayouts: [section]
                    fontCatalog: harness.catalog
                    trackInfoFontSizeControl: stubTrackInfoFontSize
                    showTrackInfo: true

                    lyricEffectiveFamily: harness.lyricFamily
                    lyricSetInEffect: harness.setInEffect
                    trackInfoFontSameAsLyrics: harness.trackInfoFontSameAsLyrics
                    trackInfoFontFamily: harness.trackInfoFontFamily
                    trackInfoEffectiveFamily: harness.trackInfoFamily
                    trackInfoFontWeight: harness.trackInfoFontWeight

                    onTrackInfoFontSameAsLyricsEdited: value => {
                        harness.log("trackInfoFontSameAsLyrics=" + value);
                        harness.trackInfoFontSameAsLyrics = value;
                    }
                    onTrackInfoFontFamilyEdited: value => {
                        harness.log("trackInfoFontFamily=" + value);
                        harness.trackInfoFontFamily = value;
                    }
                    onTrackInfoFontWeightEdited: value => {
                        harness.log("trackInfoFontWeight=" + value);
                        harness.trackInfoFontWeight = value;
                    }
                }
            }
        }
    }

    Component {
        id: configDesktopAppearanceComponent
        LyricsConfig.ConfigDesktopAppearance {}
    }

    Component {
        id: configPanelAppearanceComponent
        LyricsConfig.ConfigPanelAppearance {}
    }

    function systemFamily() {
        return Kirigami.Theme.defaultFont.family;
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

    function named(root, objectName) {
        const found = findAll(root, o => o.objectName === objectName);
        verify(found.length === 1, objectName);
        return found[0];
    }

    // Not a temporary object: those are destroyed in creation order, so the
    // catalog would go before the harness that still binds to it. Parented
    // to this TestCase instead, it outlives every harness.
    function makeCatalog(properties) {
        const props = Object.assign({ systemFamily: systemFamily() }, properties || {});
        const catalog = fakeCatalogComponent.createObject(this, props);
        verify(catalog !== null);
        return catalog;
    }

    function makeHarness(properties, catalogProperties) {
        const props = Object.assign({ catalog: makeCatalog(catalogProperties) }, properties || {});
        const win = createTemporaryObject(harnessComponent, this, props);
        verify(win !== null);
        tryVerify(() => win.active);
        return win;
    }

    // Waits until `item` stops moving. FormLayout lays its rows out from
    // zero-interval timers rather than synchronously, so a click right after
    // the window is shown can land where a row was before that. Measured in
    // the Debian 13 CI container (Qt 6.8.2): in the suite's first test the
    // lyric picker was still at its 120 px implicit width and y 0 when
    // clicked, where it settles 252 px wide at y 62, and the click missed.
    function settle(item) {
        let last = "";
        tryVerify(() => {
            const corner = item.mapToItem(null, 0, 0);
            const now = [corner.x, corner.y, item.width, item.height].join(",");
            const unchanged = now === last;
            last = now;
            return unchanged;
        });
    }

    function openPicker(picker) {
        settle(picker);
        mouseClick(picker);
        tryVerify(() => picker.popup.opened);
        tryVerify(() => picker.searchField.activeFocus);
    }

    // The list is positioned on the current entry after the popup opens,
    // so a row can still move under the pointer; the same wait applies.
    function clickRow(picker, index) {
        const row = picker.entryList.itemAtIndex(index);
        verify(row !== null, "row " + index);
        settle(row);
        mouseClick(row);
    }

    function typeText(text) {
        for (let i = 0; i < text.length; ++i) {
            keyClick(text[i]);
        }
    }

    function kinds(picker) {
        return picker.entryList.entries.map(entry => entry.kind);
    }

    function families(picker) {
        return picker.entryList.entries.filter(entry => entry.kind === "font").map(entry => entry.family);
    }

    function highlighted(picker) {
        return picker.entryList.entries[picker.entryList.currentIndex];
    }

    // The closed box's text: currentLabel, drawn by the box's own field in
    // the box's font, with displayText left empty so that no style paints a
    // second copy in the UI font. The field shows currentLabel elided to its
    // width, and how much fits depends on the fonts of the machine running
    // this (CI's containers have few), so the shown text is only checked to
    // be the label or a cut-down form of it. Qt ends an elided string with
    // U+2026, or with three dots when the font has no such glyph.
    function compareLabel(picker, label) {
        compare(picker.currentLabel, label);
        compare(picker.displayText, "");
        compare(picker.Accessible.name, label);
        const closed = named(picker, "closedLabel");
        compare(closed.font.family, picker.font.family);
        const shown = closed.text;
        if (shown === label) {
            return;
        }
        const kept = shown.replace(/(\u2026|\.\.\.)$/, "");
        verify(kept.length < shown.length && label.startsWith(kept),
            "\"" + shown + "\" is not \"" + label + "\" elided");
    }

    function test_closedPickerShowsTheCurrentChoiceInItsOwnFamily() {
        const win = makeHarness({ fontFamily: "Beta Serif", trackInfoFontSameAsLyrics: true });
        const lyric = named(win.section, "lyricFontPicker");
        const trackInfo = named(win.trackInfoSection, "trackInfoFontPicker");
        compareLabel(lyric, "Beta Serif");
        compare(lyric.font.family, "Beta Serif");
        // Indented like the text of a plain combo box of the same style.
        const weight = named(win.section, "lyricWeightComboBox");
        compare(lyric.leftPadding + named(lyric, "closedLabel").leftPadding,
            weight.leftPadding + weight.contentItem.leftPadding);
        // "Same as lyrics" is drawn in what it stands for.
        compareLabel(trackInfo, "Same as lyrics");
        compare(trackInfo.font.family, "Beta Serif");

        win.fontFamily = "";
        win.trackInfoFontSameAsLyrics = false;
        win.trackInfoFontFamily = "Gamma Mono";
        compareLabel(lyric, "Follow system font (" + systemFamily() + ")");
        compare(lyric.font.family, systemFamily());
        compareLabel(trackInfo, "Gamma Mono");
        compare(trackInfo.font.family, "Gamma Mono");

        // A name stored under another locale shows as this one lists it.
        win.fontFamily = "阿尔法黑体";
        compareLabel(lyric, "Alpha Sans");
        compare(lyric.font.family, "Alpha Sans");
        compare(win.edits, []);
    }

    function test_pinnedEntriesOnlyWhileTheQueryIsEmpty() {
        const win = makeHarness({ fontFamily: "", trackInfoFontSameAsLyrics: true });
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        compare(kinds(lyric), ["system", "font", "font", "font", "font"]);
        compare(lyric.entryList.entries[0].text, "Follow system font (" + systemFamily() + ")");
        compare(lyric.entryList.entries[0].renderFamily, systemFamily());
        compare(families(lyric), win.catalog.families());
        typeText("a");
        compare(kinds(lyric), ["font", "font", "font", "font"]);
        // A whitespace-only query lists everything, pinned entries included.
        keyClick(Qt.Key_Backspace);
        typeText(" ");
        compare(kinds(lyric)[0], "system");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !lyric.popup.visible);

        const trackInfo = named(win.trackInfoSection, "trackInfoFontPicker");
        openPicker(trackInfo);
        compare(kinds(trackInfo).slice(0, 3), ["same", "system", "font"]);
        compare(trackInfo.entryList.entries[0].text, "Same as lyrics");
        typeText("gamma");
        compare(kinds(trackInfo), ["font"]);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !trackInfo.popup.visible);
        compare(win.edits, []);
    }

    function test_theStoredValueIsHighlightedOnOpen() {
        const win = makeHarness({
            fontFamily: "Gamma Mono",
            trackInfoFontSameAsLyrics: false,
            trackInfoFontFamily: ""
        });
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        compare(highlighted(lyric).family, "Gamma Mono");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !lyric.popup.visible);

        const trackInfo = named(win.trackInfoSection, "trackInfoFontPicker");
        openPicker(trackInfo);
        compare(highlighted(trackInfo).kind, "system");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !trackInfo.popup.visible);

        win.trackInfoFontSameAsLyrics = true;
        openPicker(trackInfo);
        compare(highlighted(trackInfo).kind, "same");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !trackInfo.popup.visible);

        win.fontFamily = "阿尔法黑体";
        openPicker(lyric);
        compare(highlighted(lyric).kind, "font");
        compare(highlighted(lyric).family, "Alpha Sans");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !lyric.popup.visible);
        compare(win.edits, []);
    }

    function test_aMissingFamilyIsItsOwnEntryAndPickingItChangesNothing() {
        const win = makeHarness({
            fontFamily: "Gone Sans",
            trackInfoFontSameAsLyrics: false,
            trackInfoFontFamily: "Gone Mono"
        });
        const lyric = named(win.section, "lyricFontPicker");
        compareLabel(lyric, "Gone Sans (not installed)");
        compare(lyric.font.family, systemFamily());
        openPicker(lyric);
        compare(kinds(lyric).slice(0, 3), ["system", "missing", "font"]);
        compare(lyric.entryList.entries[1].text, "Gone Sans (not installed)");
        compare(highlighted(lyric).kind, "missing");
        keyClick(Qt.Key_Return);
        tryVerify(() => !lyric.popup.visible);
        compare(win.fontFamily, "Gone Sans");

        const trackInfo = named(win.trackInfoSection, "trackInfoFontPicker");
        openPicker(trackInfo);
        compare(kinds(trackInfo).slice(0, 4), ["same", "system", "missing", "font"]);
        compare(highlighted(trackInfo).text, "Gone Mono (not installed)");
        clickRow(trackInfo, 2);
        tryVerify(() => !trackInfo.popup.visible);
        compare(win.trackInfoFontFamily, "Gone Mono");
        compare(win.edits, []);

        // With "Same as lyrics" on, the stored family is not the one in
        // effect, so it is not offered as an entry at all.
        win.trackInfoFontSameAsLyrics = true;
        openPicker(trackInfo);
        compare(kinds(trackInfo).indexOf("missing"), -1);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !trackInfo.popup.visible);
    }

    function test_searchResultsKeepTheCatalogOrderAndEnterPicksTheFirst() {
        const win = makeHarness({ fontFamily: "Beta Serif" });
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        typeText("a");
        compare(families(lyric), win.catalog.ranked["a"]);
        compare(lyric.entryList.currentIndex, 0);
        keyClick(Qt.Key_Return);
        tryVerify(() => !lyric.popup.visible);
        compare(win.fontFamily, "Gamma Mono");
    }

    function test_arrowKeysMoveTheHighlightWhileTheFieldKeepsFocus() {
        const win = makeHarness({ fontFamily: "" });
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        compare(lyric.entryList.currentIndex, 0);
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Down);
        compare(highlighted(lyric).family, "Beta Serif");
        verify(lyric.searchField.activeFocus);
        keyClick(Qt.Key_Up);
        compare(highlighted(lyric).family, "Alpha Sans");
        // The highlight stops at either end rather than wrapping.
        keyClick(Qt.Key_Up);
        keyClick(Qt.Key_Up);
        compare(lyric.entryList.currentIndex, 0);
        for (let i = 0; i < 10; ++i) {
            keyClick(Qt.Key_Down);
        }
        compare(lyric.entryList.currentIndex, lyric.entryList.count - 1);
        keyClick(Qt.Key_Enter);
        tryVerify(() => !lyric.popup.visible);
        compare(win.fontFamily, "Single Face");
    }

    function test_escapeClosesWithoutAnyChange() {
        const win = makeHarness({ fontFamily: "Alpha Sans", fontWeight: 300 });
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        typeText("be");
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !lyric.popup.visible);
        compare(win.fontFamily, "Alpha Sans");
        compare(win.fontWeight, 300);
        compare(win.edits, []);
    }

    function test_everyOpenStartsFromAnEmptyQuery() {
        const win = makeHarness({ fontFamily: "Gamma Mono" });
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        typeText("zzz");
        compare(lyric.entryList.count, 0);
        verify(named(lyric.popup.contentItem, "noMatchMessage").visible);
        // Enter with nothing listed picks nothing.
        keyClick(Qt.Key_Return);
        compare(win.edits, []);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !lyric.popup.visible);

        openPicker(lyric);
        compare(lyric.searchField.text, "");
        compare(kinds(lyric)[0], "system");
        compare(highlighted(lyric).family, "Gamma Mono");
        verify(!named(lyric.popup.contentItem, "noMatchMessage").visible);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !lyric.popup.visible);
    }

    function test_rowsShowTheOtherNamesOfAFamily() {
        const win = makeHarness({ fontFamily: "Beta Serif" });
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        const index = lyric.entryList.currentIndex;
        const row = lyric.entryList.itemAtIndex(index);
        verify(row !== null);
        // The row's own text is not drawn (its contentItem is), but it is
        // what a screen reader reads out.
        compare(row.text, "Beta Serif");
        const texts = findAll(row, o => o.text !== undefined && o.font !== undefined).map(o => o.text);
        verify(texts.includes("Beta Serif"), texts);
        verify(texts.includes("Beta Antiqua, ベータ明朝"), texts);
        // The closed box is set to the chosen family; the popup, which sits
        // in the window's overlay, takes the window's font instead, so only
        // the rows are drawn in their own families. Not compared with
        // Kirigami.Theme.defaultFont: which font the window gets is up to the
        // style, and CI falls back to Fusion.
        compare(lyric.font.family, "Beta Serif");
        verify(lyric.searchField.font.family !== "Beta Serif");
        keyClick(Qt.Key_Escape);
        tryVerify(() => !lyric.popup.visible);
    }

    function test_rowsAreCreatedOnlyForWhatIsInView() {
        const many = [];
        for (let i = 0; i < 300; ++i) {
            many.push("Family " + String(i).padStart(3, "0"));
        }
        const win = makeHarness({ fontFamily: "" }, { listed: many });
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        compare(lyric.entryList.count, 301);
        // Rows beyond the viewport are incubated asynchronously; give that
        // time to run, so this counts what the view settles on.
        wait(200);
        let created = 0;
        let rowHeight = Infinity;
        const rows = lyric.entryList.contentItem.children;
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].modelData !== undefined) {
                ++created;
                rowHeight = Math.min(rowHeight, rows[i].height);
            }
        }
        // Row heights follow the fonts on the machine, so the bound is
        // worked out from the ones laid out here: the rows that fit in the
        // viewport plus ListView's default 320 px cache on either side,
        // with a few to spare for rows straddling an edge.
        verify(created > 0 && rowHeight > 0);
        const bound = Math.ceil((lyric.entryList.height + 2 * 320) / rowHeight) + 4;
        verify(created <= bound, "created " + created + " of 301 rows, bound " + bound);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !lyric.popup.visible);
    }

    function test_lyricPickerKeyMapping() {
        const win = makeHarness({ fontFamily: "Gone Sans", fontWeight: 400, trackInfoFontSameAsLyrics: false });
        const lyric = named(win.section, "lyricFontPicker");

        openPicker(lyric);
        clickRow(lyric, 0);
        tryVerify(() => !lyric.popup.visible);
        // "Gone Sans" rendered in the Plasma font already, so the family in
        // effect did not change and neither does the weight.
        compare(win.edits, ["fontFamily="]);

        win.edits = [];
        openPicker(lyric);
        typeText("gamma");
        keyClick(Qt.Key_Return);
        tryVerify(() => !lyric.popup.visible);
        compare(win.edits, ["fontFamily=Gamma Mono", "fontWeight=400"]);
    }

    function test_trackInfoPickerKeyMapping() {
        const win = makeHarness({
            fontFamily: "Alpha Sans",
            trackInfoFontSameAsLyrics: false,
            trackInfoFontFamily: "Beta Serif",
            trackInfoFontWeight: 400
        });
        const trackInfo = named(win.trackInfoSection, "trackInfoFontPicker");

        // "Same as lyrics" leaves the stored track-info family alone, so
        // turning it off again brings that family back.
        openPicker(trackInfo);
        clickRow(trackInfo, 0);
        tryVerify(() => !trackInfo.popup.visible);
        compare(win.edits, ["trackInfoFontSameAsLyrics=true", "trackInfoFontWeight=400"]);
        compare(win.trackInfoFontFamily, "Beta Serif");

        win.edits = [];
        openPicker(trackInfo);
        clickRow(trackInfo, 1);
        tryVerify(() => !trackInfo.popup.visible);
        compare(win.edits, [
            "trackInfoFontSameAsLyrics=false",
            "trackInfoFontFamily=",
            "trackInfoFontWeight=400"
        ]);

        win.edits = [];
        openPicker(trackInfo);
        typeText("gamma");
        keyClick(Qt.Key_Return);
        tryVerify(() => !trackInfo.popup.visible);
        compare(win.edits, [
            "trackInfoFontSameAsLyrics=false",
            "trackInfoFontFamily=Gamma Mono",
            "trackInfoFontWeight=400"
        ]);
        compare(win.fontFamily, "Alpha Sans");
    }

    function test_weightLabelsUseStandardNamesOrTheFacesOwnName() {
        const win = makeHarness({ fontFamily: "Beta Serif", fontWeight: 400 });
        const weight = named(win.section, "lyricWeightComboBox");
        compare(weight.model, ["Light", "DemiLight", "Regular", "Extra bold"]);
        verify(weight.enabled);

        win.fontFamily = "";
        compare(weight.model, [
            "Thin", "Extra light", "Light", "Regular", "Medium",
            "Demi bold", "Bold", "Extra bold", "Black"
        ]);
    }

    function test_aSingleFaceFamilyDisablesTheWeightRow() {
        const win = makeHarness({
            fontFamily: "Single Face",
            fontWeight: 700,
            trackInfoFontSameAsLyrics: true,
            trackInfoFontWeight: 700
        });
        const lyricWeight = named(win.section, "lyricWeightComboBox");
        const trackInfoWeight = named(win.trackInfoSection, "trackInfoWeightComboBox");
        compare(lyricWeight.count, 1);
        compare(lyricWeight.currentText, "Regular");
        verify(!lyricWeight.enabled);
        // Track info follows the lyric family here, so the same applies.
        compare(trackInfoWeight.count, 1);
        verify(!trackInfoWeight.enabled);
        compare(win.edits, []);
    }

    function test_theWeightRowShowsTheSnappedWeightWithoutWritingIt() {
        const win = makeHarness({
            fontFamily: "Beta Serif",
            fontWeight: 700,
            trackInfoFontSameAsLyrics: false,
            trackInfoFontFamily: "Alpha Sans",
            trackInfoFontWeight: 500
        });
        const lyricWeight = named(win.section, "lyricWeightComboBox");
        const trackInfoWeight = named(win.trackInfoSection, "trackInfoWeightComboBox");
        // 700 is not a face of Beta Serif; the lightest heavier one is.
        compare(lyricWeight.currentText, "Extra bold");
        // 500 prefers the heaviest lighter face when none up to 500 exists.
        compare(trackInfoWeight.currentText, "Regular");
        compare(win.fontWeight, 700);
        compare(win.trackInfoFontWeight, 500);

        // Opening and dismissing a picker writes nothing either.
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        keyClick(Qt.Key_Escape);
        tryVerify(() => !lyric.popup.visible);
        compare(win.edits, []);

        // Choosing a weight writes the face's own weight.
        lyricWeight.activated(1);
        compare(win.edits, ["fontWeight=316"]);
    }

    function test_pickingALyricFamilySnapsBothWeightsWhenTrackInfoFollows() {
        const win = makeHarness({
            fontFamily: "",
            fontWeight: 700,
            trackInfoFontSameAsLyrics: true,
            trackInfoFontWeight: 200
        });
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        typeText("beta");
        keyClick(Qt.Key_Return);
        tryVerify(() => !lyric.popup.visible);
        // Snapped from the stored weights, 700 and 200, onto Beta Serif's
        // 300/316/400/800.
        compare(win.edits, ["fontFamily=Beta Serif", "fontWeight=800", "trackInfoFontWeight=300"]);
        compare(named(win.section, "lyricWeightComboBox").currentText, "Extra bold");
        compare(named(win.trackInfoSection, "trackInfoWeightComboBox").currentText, "Light");
    }

    function test_pickingALyricFamilyLeavesAnIndependentTrackInfoWeightAlone() {
        const win = makeHarness({
            fontFamily: "Alpha Sans",
            fontWeight: 700,
            trackInfoFontSameAsLyrics: false,
            trackInfoFontFamily: "",
            trackInfoFontWeight: 200
        });
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        typeText("beta");
        keyClick(Qt.Key_Return);
        tryVerify(() => !lyric.popup.visible);
        compare(win.edits, ["fontFamily=Beta Serif", "fontWeight=800"]);
    }

    function test_aPickThatKeepsTheFamilyInEffectLeavesTheWeightAlone() {
        const win = makeHarness({
            fontFamily: "阿尔法黑体",
            fontWeight: 500,
            trackInfoFontSameAsLyrics: true,
            trackInfoFontWeight: 500
        });
        const lyric = named(win.section, "lyricFontPicker");
        // The stored name is Alpha Sans under another locale: picking Alpha
        // Sans rewrites the name but renders the same family.
        openPicker(lyric);
        keyClick(Qt.Key_Return);
        tryVerify(() => !lyric.popup.visible);
        compare(win.edits, ["fontFamily=Alpha Sans"]);
        compare(win.fontWeight, 500);

        // Same for the track-info picker: turning "Same as lyrics" off in
        // favour of the family the lyrics already use.
        win.edits = [];
        const trackInfo = named(win.trackInfoSection, "trackInfoFontPicker");
        openPicker(trackInfo);
        typeText("alpha");
        keyClick(Qt.Key_Return);
        tryVerify(() => !trackInfo.popup.visible);
        compare(win.edits, ["trackInfoFontSameAsLyrics=false", "trackInfoFontFamily=Alpha Sans"]);
        compare(win.trackInfoFontWeight, 500);
    }

    function test_trackInfoPicksSnapOnlyTheTrackInfoWeight() {
        const win = makeHarness({
            fontFamily: "Alpha Sans",
            fontWeight: 300,
            trackInfoFontSameAsLyrics: false,
            trackInfoFontFamily: "Beta Serif",
            trackInfoFontWeight: 316
        });
        const trackInfo = named(win.trackInfoSection, "trackInfoFontPicker");
        // Back to the lyric family: 316 is not an Alpha Sans face.
        openPicker(trackInfo);
        clickRow(trackInfo, 0);
        tryVerify(() => !trackInfo.popup.visible);
        compare(win.edits, ["trackInfoFontSameAsLyrics=true", "trackInfoFontWeight=300"]);
        compare(win.fontWeight, 300);
    }

    // The track-info weight is one key for both sets, and the track info
    // renders in the lyric font of the set in effect only (DESIGN.md
    // decision 76). A lyric pick on the other set's tab therefore writes
    // that set's lyric weight and leaves the track-info weight as stored.
    function test_aLyricPickOnTheSetNotInEffectLeavesTheTrackInfoWeight() {
        const win = makeHarness({
            fontFamily: "",
            fontWeight: 700,
            trackInfoFontSameAsLyrics: true,
            trackInfoFontWeight: 200,
            setInEffect: false
        });
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        typeText("beta");
        keyClick(Qt.Key_Return);
        tryVerify(() => !lyric.popup.visible);
        compare(win.edits, ["fontFamily=Beta Serif", "fontWeight=800"]);
        compare(win.trackInfoFontWeight, 200);
    }

    // The same for "Same as lyrics" in the track-info picker: on the other
    // set's tab it moves the track info onto a lyric font it will not
    // render in. A family of its own is what it renders in on either tab,
    // so that pick still writes the weight.
    function test_sameAsLyricsOnTheSetNotInEffectLeavesTheTrackInfoWeight() {
        const win = makeHarness({
            fontFamily: "Alpha Sans",
            fontWeight: 300,
            trackInfoFontSameAsLyrics: false,
            trackInfoFontFamily: "Beta Serif",
            trackInfoFontWeight: 316,
            setInEffect: false
        });
        const trackInfo = named(win.trackInfoSection, "trackInfoFontPicker");
        openPicker(trackInfo);
        clickRow(trackInfo, 0);
        tryVerify(() => !trackInfo.popup.visible);
        compare(win.edits, ["trackInfoFontSameAsLyrics=true"]);
        compare(win.trackInfoFontWeight, 316);

        win.edits = [];
        openPicker(trackInfo);
        typeText("gamma");
        keyClick(Qt.Key_Return);
        tryVerify(() => !trackInfo.popup.visible);
        compare(win.edits, ["trackInfoFontSameAsLyrics=false", "trackInfoFontFamily=Gamma Mono",
            "trackInfoFontWeight=400"]);
    }

    function test_aFamilyWithUnknownFacesOffersTheSixSteps() {
        const win = makeHarness({
            fontFamily: "",
            fontWeight: 700,
            trackInfoFontSameAsLyrics: true,
            trackInfoFontWeight: 800
        }, { systemFaces: [] });
        const lyricWeight = named(win.section, "lyricWeightComboBox");
        const trackInfoWeight = named(win.trackInfoSection, "trackInfoWeightComboBox");
        compare(lyricWeight.model, ["Light", "Regular", "Medium", "Demi bold", "Bold", "Black"]);
        verify(lyricWeight.enabled);
        compare(lyricWeight.currentText, "Bold");
        // A stored weight that is none of the six is listed in its place,
        // so the row never reads as a weight other than the one drawn.
        compare(trackInfoWeight.model, ["Light", "Regular", "Medium", "Demi bold", "Bold", "Extra bold", "Black"]);
        compare(trackInfoWeight.currentText, "Extra bold");
        win.fontWeight = 450;
        compare(lyricWeight.model, ["Light", "Regular", "450", "Medium", "Demi bold", "Bold", "Black"]);
        compare(lyricWeight.currentText, "450");
        compare(win.edits, []);

        lyricWeight.activated(3);
        compare(win.edits, ["fontWeight=500"]);
        compare(lyricWeight.model, ["Light", "Regular", "Medium", "Demi bold", "Bold", "Black"]);
        compare(lyricWeight.currentText, "Medium");
    }

    function test_movingOntoAFamilyWithUnknownFacesKeepsTheStoredWeights() {
        const win = makeHarness({
            fontFamily: "Beta Serif",
            fontWeight: 700,
            trackInfoFontSameAsLyrics: true,
            trackInfoFontWeight: 316
        }, { systemFaces: [] });
        const lyric = named(win.section, "lyricFontPicker");
        openPicker(lyric);
        clickRow(lyric, 0);
        tryVerify(() => !lyric.popup.visible);
        // The family changed, so both weights are written, but snapped onto
        // an empty list they are what was stored.
        compare(win.edits, ["fontFamily=", "fontWeight=700", "trackInfoFontWeight=316"]);
        compare(named(win.section, "lyricWeightComboBox").currentText, "Bold");
        compare(named(win.trackInfoSection, "trackInfoWeightComboBox").currentText, "316");
    }

    function test_theFontControlsCannotWidenThePage() {
        // The page sizes itself to its widest row (AppearanceSection's
        // formDescription comment has the measurements). Neither picker
        // nor weight row may be the one that sets it, whatever the names.
        const shortName = makeHarness({ fontFamily: "Alpha Sans" });
        wait(100);
        const longFamily = "A Family Whose Name Goes On ".repeat(8).trim();
        const longName = makeHarness({ fontFamily: longFamily },
            { listed: ["Alpha Sans", longFamily], faces: ({ [longFamily]: [
                { weight: 400, styleName: "Regular" },
                { weight: 450, styleName: "An Unusually Long Style Name For A Single Face ".repeat(4) }
            ] }) });
        wait(100);
        compare(longName.section.implicitWidth, shortName.section.implicitWidth);
        compare(longName.trackInfoSection.implicitWidth, shortName.trackInfoSection.implicitWidth);
        const controls = [
            named(longName.section, "lyricFontPicker"),
            named(longName.trackInfoSection, "trackInfoFontPicker"),
            named(longName.section, "lyricWeightComboBox"),
            named(longName.trackInfoSection, "trackInfoWeightComboBox")
        ];
        for (const control of controls) {
            verify(control.Layout.maximumWidth < Infinity, control.objectName);
            verify(control.width <= control.Layout.maximumWidth, control.objectName);
        }
        const lyric = named(longName.section, "lyricFontPicker");
        compareLabel(lyric, longFamily);
        // Over 200 characters fit in 20 gridUnits in no font at all.
        const closed = named(lyric, "closedLabel");
        verify(closed.text.length < longFamily.length);
        verify(closed.width <= lyric.availableWidth);
    }

    // The pages are created the way the config dialog creates them: every
    // cfg_ value an initial property. This is the arrangement in which a
    // derived family computed inside the sections loops (see
    // AppearanceSection's lyricEffectiveFamily), which the page tests in
    // tst_appearance.qml, creating the pages with no properties, never
    // exercised. A loop here fails ctest through FAIL_REGULAR_EXPRESSION,
    // not through an assertion.
    function test_pagesCreatedWithStoredFontKeys_data() {
        const listed = FontCatalog.families();
        const first = listed.length > 0 ? listed[0] : "";
        const last = listed.length > 0 ? listed[listed.length - 1] : "";
        return [
            { tag: "defaults", family: "", sameAsLyrics: true, trackInfoFamily: "" },
            { tag: "listed", family: first, sameAsLyrics: true, trackInfoFamily: last },
            { tag: "independent", family: first, sameAsLyrics: false, trackInfoFamily: last },
            { tag: "missing", family: "No Such Family", sameAsLyrics: false, trackInfoFamily: "No Such Mono" }
        ];
    }

    // Once for each set: the stored lyric font sits in the set the page
    // opens on (DESIGN.md decision 76), which the mode picks; the other set
    // is left unset.
    function test_pagesCreatedWithStoredFontKeys(data) {
        for (const form of ["desktop", "panel"]) {
            for (const dark of [false, true]) {
                const tag = form + (dark ? " dark" : " light");
                const prefix = ThemePolicy.keyPrefix(form, dark);
                const props = {};
                props["cfg_" + ThemePolicy.modeKey(form)] = dark ? "dark" : "light";
                props["cfg_" + prefix + "FontFamily"] = data.family;
                props["cfg_" + prefix + "FontWeight"] = 700;
                props["cfg_" + form + "ShowTrackInfo"] = true;
                props["cfg_" + form + "TrackInfoFontSameAsLyrics"] = data.sameAsLyrics;
                props["cfg_" + form + "TrackInfoFontFamily"] = data.trackInfoFamily;
                props["cfg_" + form + "TrackInfoFontWeight"] = 300;
                const page = createTemporaryObject(form === "desktop"
                    ? configDesktopAppearanceComponent
                    : configPanelAppearanceComponent, this, props);
                verify(page !== null, tag);
                compare(page.editingDark, dark, tag);
                const section = findAll(page, o => typeof o.fontFamilyEdited === "function")[0];
                verify(section !== undefined, tag);
                const trackInfoSection = findAll(page, o => typeof o.trackInfoFontFamilyEdited === "function")[0];
                verify(trackInfoSection !== undefined, tag);
                compare(section.lyricEffectiveFamily,
                    FontPolicy.lyricFamily(FontCatalog, data.family, systemFamily()), tag);
                compare(trackInfoSection.lyricEffectiveFamily, section.lyricEffectiveFamily, tag);
                compare(trackInfoSection.trackInfoEffectiveFamily,
                    FontPolicy.trackInfoFamily(FontCatalog, data.sameAsLyrics, data.trackInfoFamily,
                        section.lyricEffectiveFamily, systemFamily()), tag);
                // Displaying the pages wrote nothing back.
                compare(page["cfg_" + prefix + "FontFamily"], data.family, tag);
                compare(page["cfg_" + prefix + "FontWeight"], 700, tag);
                compare(page["cfg_" + form + "TrackInfoFontWeight"], 300, tag);
            }
        }
    }

    // "Same as lyrics" and the track-info weight row follow the lyric font
    // of the set on screen, while the track-info keys themselves are one
    // copy for both sets.
    function test_trackInfoFollowsTheLyricFontOfTheSetOnScreen() {
        const system = FontPolicy.lyricFamily(FontCatalog, "", systemFamily());
        const other = FontCatalog.families().find(family => family !== system);
        if (other === undefined) {
            skip("needs an installed family other than the Plasma font");
        }
        const otherFamily = FontPolicy.lyricFamily(FontCatalog, other, systemFamily());
        for (const form of ["desktop", "panel"]) {
            const props = {};
            props["cfg_" + form + "FontFamily"] = other;
            props["cfg_" + form + "LightFontFamily"] = "";
            props["cfg_" + form + "ShowTrackInfo"] = true;
            props["cfg_" + form + "TrackInfoFontSameAsLyrics"] = true;
            props["cfg_" + ThemePolicy.modeKey(form)] = "light";
            const page = createTemporaryObject(form === "desktop"
                ? configDesktopAppearanceComponent
                : configPanelAppearanceComponent, this, props);
            verify(page !== null, form);
            const trackInfoSection = findAll(page, o => typeof o.trackInfoFontFamilyEdited === "function")[0];
            const tabBar = named(page, "themeTabBar");
            compare(tabBar.currentIndex, 0, form);
            compare(trackInfoSection.trackInfoEffectiveFamily, system, form);
            compare(named(page, "trackInfoWeightComboBox").family, system, form);
            tabBar.currentIndex = 1;
            compare(trackInfoSection.trackInfoEffectiveFamily, otherFamily, form);
            compare(named(page, "trackInfoWeightComboBox").family, otherFamily, form);
            // Switching tabs wrote nothing.
            compare(page["cfg_" + form + "TrackInfoFontSameAsLyrics"], true, form);
            compare(page["cfg_" + form + "LightFontFamily"], "", form);
        }
    }

    function test_fontEditsReachTheirOwnConfigProperties() {
        // Pinned to dark, so the lyric font edits land on the original
        // <form>FontFamily keys; which set an edit reaches is
        // tst_appearance.qml's test_editsReachOnlyTheSetOnScreen.
        const desktopPage = createTemporaryObject(configDesktopAppearanceComponent, this,
            { cfg_desktopThemeMode: "dark" });
        const panelPage = createTemporaryObject(configPanelAppearanceComponent, this,
            { cfg_panelThemeMode: "dark" });
        const desktop = findAll(desktopPage, o => typeof o.fontFamilyEdited === "function")[0];
        const panel = findAll(panelPage, o => typeof o.fontFamilyEdited === "function")[0];
        const desktopTrackInfo = findAll(desktopPage, o => typeof o.trackInfoFontFamilyEdited === "function")[0];
        const panelTrackInfo = findAll(panelPage, o => typeof o.trackInfoFontFamilyEdited === "function")[0];
        verify(desktop !== undefined);
        verify(panel !== undefined);
        verify(desktopTrackInfo !== undefined);
        verify(panelTrackInfo !== undefined);
        verify(desktop.fontCatalog === FontCatalog);
        verify(desktopTrackInfo.fontCatalog === FontCatalog);

        desktop.fontFamilyEdited("Desktop Family");
        desktopTrackInfo.trackInfoFontSameAsLyricsEdited(true);
        desktopTrackInfo.trackInfoFontFamilyEdited("Desktop Track Family");
        compare(desktopPage.cfg_desktopFontFamily, "Desktop Family");
        compare(desktopPage.cfg_desktopTrackInfoFontSameAsLyrics, true);
        compare(desktopPage.cfg_desktopTrackInfoFontFamily, "Desktop Track Family");

        panel.fontFamilyEdited("Panel Family");
        panelTrackInfo.trackInfoFontSameAsLyricsEdited(false);
        panelTrackInfo.trackInfoFontFamilyEdited("Panel Track Family");
        compare(panelPage.cfg_panelFontFamily, "Panel Family");
        compare(panelPage.cfg_panelTrackInfoFontSameAsLyrics, false);
        compare(panelPage.cfg_panelTrackInfoFontFamily, "Panel Track Family");
        // The two tabs are separate page instances; neither reaches into
        // the other.
        compare(desktopPage.cfg_desktopFontFamily, "Desktop Family");
        compare(desktopPage.cfg_desktopTrackInfoFontSameAsLyrics, true);
    }
}
