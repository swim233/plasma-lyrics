import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import "../package/contents/ui" as LyricsUi
import "../package/contents/ui/FontPolicy.js" as FontPolicy

// The font family the renderer draws in (DESIGN.md decision 30): which family
// reaches which drawn and measuring text item, and FontPolicy.js's rules for
// turning the stored settings into that family and a real face's weight.
//
// Every top-level object below is a Component, for the load-time reason
// tst_appearance.qml's header gives. This file sorts after that one, so a
// binding loop raised while loading it would still be caught -- but only for
// as long as it keeps sorting after it.
//
// main.qml's own wiring (the config keys it reads, and FontCatalog handed to
// FontPolicy.js) is not covered here: it is a PlasmoidItem, which this suite
// cannot instantiate. qmllint's [missing-property] covers the properties it
// sets on LyricsView; nothing covers the key names.
TestCase {
    name: "FontRender"
    when: windowShown

    // Neither is installed anywhere. font.family reads back the requested
    // family whatever fontconfig substitutes for it, so the propagation tests
    // do not depend on the fonts of the machine running them.
    readonly property string lyricTestFamily: "Lyric Test Family"
    readonly property string trackTestFamily: "Track Test Family"

    readonly property var twoWords: [
        { startMs: 0, endMs: 1000, text: "ab" },
        { startMs: 1000, endMs: 2000, text: "cd" }
    ]

    // tst_appearance.qml's stand-in for LyricSource, with a second line.
    Component {
        id: fakeSourceComponent
        QtObject {
            property bool serviceAvailable: true
            property bool stale: false
            property string lyricState: "ok"
            property string playbackStatus: "Playing"
            property string trackTitle: "Title"
            property string trackArtists: "Artist"
            property string currentText: "abcd"
            property string currentTranslation: "second line"
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
            strokeEnabled: true
            trackInfoStrokeEnabled: true
            trackInfoLayout: "double"
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

    function only(list) {
        compare(list.length, 1);
        return list[0];
    }

    function lyricOf(view) {
        return only(findAll(view, o => o.shownText !== undefined));
    }

    function trackInfoOf(view) {
        return only(findAll(view, o => o.showArtistLine !== undefined));
    }

    // Every Text at any depth: whole-line texts, their eight outline copies,
    // word glyphs and the halo copies inside their Loaders.
    function textsUnder(item) {
        return findAll(item, o => o.font !== undefined && o.text !== undefined);
    }

    // The TextMetrics and FontMetrics each LyricLine sizes itself with. They
    // are not Items, so they live in `resources`, not `children`.
    function metricsUnder(item) {
        const out = [];
        const lines = findAll(item, o => o.lineText !== undefined && o.wordMode !== undefined);
        for (let i = 0; i < lines.length; ++i) {
            const resources = lines[i].resources;
            for (let j = 0; j < resources.length; ++j) {
                if (resources[j].font !== undefined) {
                    out.push(resources[j]);
                }
            }
        }
        return out;
    }

    function compareFamilies(items, family) {
        verify(items.length > 0);
        for (let i = 0; i < items.length; ++i) {
            compare(items[i].font.family, family,
                    "\"" + items[i].text + "\" (" + items[i] + ")");
        }
    }

    function createView(sourceProperties, viewProperties) {
        const source = createTemporaryObject(fakeSourceComponent, this, sourceProperties);
        const properties = viewProperties;
        properties.source = source;
        return createTemporaryObject(lyricsViewComponent, this, properties);
    }

    // ---- what the renderer draws in ----

    function test_nothingConfiguredDrawsAndMeasuresInThePlasmaFont() {
        const view = createView({ currentWords: twoWords, positionMs: 500 }, { wordBlurGlow: true });
        const plasma = Kirigami.Theme.defaultFont.family;
        compare(view.fontFamily, plasma);
        compare(view.trackInfoFontFamily, plasma);
        tryVerify(() => findAll(view, o => o.objectName === "lyricWord").length === 2);
        compareFamilies(textsUnder(view), plasma);
        compareFamilies(metricsUnder(view), plasma);
    }

    function test_wholeLineLyricAndSecondLineUseTheLyricFamily() {
        const view = createView({}, { fontFamily: lyricTestFamily, trackInfoFontFamily: trackTestFamily });
        const lyric = lyricOf(view);
        // Each line is the Text itself plus eight outline copies; asserting
        // the counts keeps the loop below from passing over an empty list.
        tryVerify(() => textsUnder(lyric).filter(t => t.text === "abcd").length === 9);
        const texts = textsUnder(lyric);
        compare(texts.filter(t => t.text === "second line").length, 9);
        compareFamilies(texts, lyricTestFamily);
    }

    function test_wordGlyphsAndTheirHalosUseTheLyricFamily() {
        // 500 ms is inside the first word, so its halo Loader is active.
        const view = createView({ currentWords: twoWords, positionMs: 500 },
            { fontFamily: lyricTestFamily, trackInfoFontFamily: trackTestFamily, wordBlurGlow: true });
        const lyric = lyricOf(view);
        tryVerify(() => findAll(lyric, o => o.objectName === "lyricWord").length === 2);
        const texts = textsUnder(lyric);
        compare(texts.filter(t => t.objectName === "lyricWord").length, 2);
        compare(texts.filter(t => t.text === "ab" && t.objectName !== "lyricWord").length, 1);
        compareFamilies(texts, lyricTestFamily);
    }

    // Whether a word-timed line fits or falls back to the whole line, and
    // the pixel size its words share, come from the TextMetrics; the clip
    // height comes from the FontMetrics. Measuring another family than the
    // one drawn gets the size or the clip wrong.
    function test_measuringFollowsTheFamilyThatIsDrawn() {
        const view = createView({ currentWords: twoWords }, { fontFamily: lyricTestFamily, trackInfoFontFamily: trackTestFamily });
        const lyric = lyricOf(view);
        const lyricMetrics = metricsUnder(lyric);
        // Two blocks of two lines, one TextMetrics and one FontMetrics each.
        compare(lyricMetrics.length, 8);
        compare(lyricMetrics.filter(m => m.objectName === "lineMetrics").length, 4);
        compareFamilies(lyricMetrics, lyricTestFamily);
        const trackMetrics = metricsUnder(trackInfoOf(view));
        compare(trackMetrics.length, 4);
        compareFamilies(trackMetrics, trackTestFamily);
    }

    function test_trackInfoUsesItsOwnFamily() {
        const view = createView({}, { fontFamily: lyricTestFamily, trackInfoFontFamily: trackTestFamily });
        const info = trackInfoOf(view);
        tryVerify(() => textsUnder(info).filter(t => t.text === "Title").length === 9);
        const texts = textsUnder(info);
        compare(texts.filter(t => t.text === "Artist").length, 9);
        compareFamilies(texts, trackTestFamily);
    }

    function test_aFamilyChangeReachesTheRendererWithoutARebuild() {
        const view = createView({ currentWords: twoWords }, {});
        const lyric = lyricOf(view);
        tryVerify(() => findAll(lyric, o => o.objectName === "lyricWord").length === 2);
        view.fontFamily = lyricTestFamily;
        view.trackInfoFontFamily = trackTestFamily;
        compareFamilies(textsUnder(lyric), lyricTestFamily);
        compareFamilies(metricsUnder(lyric), lyricTestFamily);
        compareFamilies(textsUnder(trackInfoOf(view)), trackTestFamily);
    }

    // ---- FontPolicy.js ----

    readonly property string plasmaFamily: "Plasma Sans"

    // A stand-in for FontCatalog with a fixed set of families, so these
    // results do not depend on what is installed (CI has next to nothing).
    // "Beta Alias" is another name for "Beta Sans", the way a family saved
    // under one locale is listed under another. snapWeight records its
    // arguments, keeps a weight that is one of the faces and otherwise
    // returns the heaviest face -- a rule neither nearest-weight nor CSS
    // matching follows, so such a result can only have come from here.
    function fakeCatalog() {
        const faces = { "Alpha": [400], "Beta Sans": [300, 700], "Plasma Sans": [400, 700] };
        const aliases = { "Beta Alias": "Beta Sans" };
        return {
            snapCalls: [],
            resolveFamily(stored) {
                if (faces[stored] !== undefined) {
                    return stored;
                }
                return aliases[stored] !== undefined ? aliases[stored] : "";
            },
            weights(family) {
                const list = faces[family] || [];
                return list.map(w => ({ weight: w, styleName: "" }));
            },
            snapWeight(available, target) {
                this.snapCalls.push({ weights: available.map(a => a.weight), target: target });
                if (available.length === 0 || available.some(a => a.weight === target)) {
                    return target;
                }
                return available[available.length - 1].weight;
            }
        };
    }

    function test_policyLyricFamilyFallsBackToThePlasmaFont() {
        const catalog = fakeCatalog();
        compare(FontPolicy.lyricFamily(catalog, "", plasmaFamily), plasmaFamily);
        compare(FontPolicy.lyricFamily(catalog, "Alpha", plasmaFamily), "Alpha");
        // The listed name, not the stored one: weights() only knows listed names.
        compare(FontPolicy.lyricFamily(catalog, "Beta Alias", plasmaFamily), "Beta Sans");
        // Not installed: the Plasma font by name, never the stored name
        // left for fontconfig to substitute.
        compare(FontPolicy.lyricFamily(catalog, "Gone Font", plasmaFamily), plasmaFamily);
    }

    function test_policyTrackInfoSameAsLyricsTakesTheLyricsEffectiveFamily() {
        const catalog = fakeCatalog();
        // Its own stored family is ignored while the switch is on.
        const lyric = FontPolicy.lyricFamily(catalog, "Beta Alias", plasmaFamily);
        compare(FontPolicy.trackInfoFamily(catalog, true, "Alpha", lyric, plasmaFamily), "Beta Sans");
        // The lyric's effective family, not its stored one: with the lyric
        // font uninstalled both render in the Plasma font.
        const missing = FontPolicy.lyricFamily(catalog, "Gone Font", plasmaFamily);
        compare(FontPolicy.trackInfoFamily(catalog, true, "Alpha", missing, plasmaFamily), plasmaFamily);
    }

    function test_policyTrackInfoOwnFamilyResolvesLikeTheLyrics() {
        const catalog = fakeCatalog();
        compare(FontPolicy.trackInfoFamily(catalog, false, "Alpha", "Beta Sans", plasmaFamily), "Alpha");
        compare(FontPolicy.trackInfoFamily(catalog, false, "Beta Alias", "Alpha", plasmaFamily), "Beta Sans");
        // Empty means the Plasma font, not "same as the lyrics".
        compare(FontPolicy.trackInfoFamily(catalog, false, "", "Beta Sans", plasmaFamily), plasmaFamily);
        compare(FontPolicy.trackInfoFamily(catalog, false, "Gone Font", "Beta Sans", plasmaFamily), plasmaFamily);
    }

    function test_policyIsMissingOnlyForAStoredFamilyThatIsNotInstalled() {
        const catalog = fakeCatalog();
        compare(FontPolicy.isMissing(catalog, ""), false);
        compare(FontPolicy.isMissing(catalog, "Alpha"), false);
        compare(FontPolicy.isMissing(catalog, "Beta Alias"), false);
        compare(FontPolicy.isMissing(catalog, "Gone Font"), true);
    }

    function test_policyRenderWeightSnapsToTheFamilysFaces() {
        const catalog = fakeCatalog();
        compare(FontPolicy.renderWeight(catalog, "Beta Sans", 500), 700);
        compare(catalog.snapCalls, [{ weights: [300, 700], target: 500 }]);
        // The faces of the family actually drawn: a missing lyric font
        // snaps against the Plasma font's faces, not the stored family's.
        const family = FontPolicy.lyricFamily(catalog, "Gone Font", plasmaFamily);
        FontPolicy.renderWeight(catalog, family, 600);
        compare(catalog.snapCalls[1], { weights: [400, 700], target: 600 });
    }

    function test_policyLeavesAnUnconfiguredInstanceAsItWas() {
        const catalog = fakeCatalog();
        // The defaults in config/main.xml: no family, the lyric at 700 and
        // the track info at 400, both real faces of the Plasma font.
        const lyric = FontPolicy.lyricFamily(catalog, "", plasmaFamily);
        const trackInfo = FontPolicy.trackInfoFamily(catalog, true, "", lyric, plasmaFamily);
        compare(lyric, plasmaFamily);
        compare(trackInfo, plasmaFamily);
        compare(FontPolicy.renderWeight(catalog, lyric, 700), 700);
        compare(FontPolicy.renderWeight(catalog, trackInfo, 400), 400);
        compare(catalog.snapCalls, [{ weights: [400, 700], target: 700 },
                                    { weights: [400, 700], target: 400 }]);
    }
}
