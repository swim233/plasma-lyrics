import QtQuick
import QtTest
import "../package/contents/ui" as LyricsUi
import "../package/contents/ui/ThemePolicy.js" as ThemePolicy

// DESIGN.md decision 76: the light/dark appearance sets. ThemePolicy.js's key
// table and migration, AppearanceTheme's choice of set and its gate on
// colour animation, and LyricsView's colour fade driven by that gate.
TestCase {
    name: "Theme"
    when: windowShown

    // A configuration object for the migration: every themed key of both
    // form factors, the dark ones at their defaults and the light ones at a
    // marker, so a test sees exactly which light keys the migration wrote.
    readonly property string untouched: "untouched light default"
    function freshConfiguration() {
        const configuration = { themeConfigVersion: 0 };
        for (const formFactor of ["desktop", "panel"]) {
            for (const suffix of ThemePolicy.themedSuffixes(formFactor)) {
                configuration[formFactor + suffix] = ThemePolicy.darkDefaults[formFactor][suffix];
                configuration[formFactor + "Light" + suffix] = untouched;
            }
        }
        return configuration;
    }
    function lightKeysAt(configuration, formFactor, expected) {
        for (const suffix of ThemePolicy.themedSuffixes(formFactor)) {
            const key = formFactor + "Light" + suffix;
            const want = expected === undefined ? configuration[formFactor + suffix] : expected;
            if (configuration[key] !== want) {
                return key + " is " + configuration[key] + ", expected " + want;
            }
        }
        return "";
    }

    function test_themedSuffixes() {
        const desktop = ThemePolicy.themedSuffixes("desktop");
        const panel = ThemePolicy.themedSuffixes("panel");
        compare(desktop.length, 28);
        compare(panel.length, 26);
        verify(desktop.includes("WordLift"));
        verify(desktop.includes("WordLiftPercent"));
        verify(!panel.includes("WordLift"));
        verify(!panel.includes("WordLiftPercent"));
        for (const suffix of ["TrackInfoColor", "TrackInfoStroke", "TrackInfoStrokeColor"]) {
            verify(desktop.includes(suffix), suffix);
            verify(panel.includes(suffix), suffix);
        }
        // Shared by both sets: never in the table.
        for (const suffix of ["ShowTrackInfo", "TrackInfoLayout", "TrackInfoFontSameAsLyrics",
                              "TrackInfoFontFamily", "TrackInfoFontSize", "TrackInfoFontWeight",
                              "TrackInfoOverflow", "AutoHide", "HideDelaySec", "HideNonMusic",
                              "HideAnimationMs", "Width"]) {
            verify(!desktop.includes(suffix), suffix);
            verify(!panel.includes(suffix), suffix);
        }
        compare(ThemePolicy.keyPrefix("desktop", true), "desktop");
        compare(ThemePolicy.keyPrefix("panel", false), "panelLight");
        compare(ThemePolicy.modeKey("panel"), "panelColorSchemeMode");
    }

    function test_isDark_data() {
        return [
            { tag: "auto, dark scheme", scheme: Qt.ColorScheme.Dark, mode: "auto", dark: true },
            { tag: "auto, light scheme", scheme: Qt.ColorScheme.Light, mode: "auto", dark: false },
            { tag: "auto, unknown scheme", scheme: Qt.ColorScheme.Unknown, mode: "auto", dark: false },
            { tag: "light pinned, dark scheme", scheme: Qt.ColorScheme.Dark, mode: "light", dark: false },
            { tag: "dark pinned, light scheme", scheme: Qt.ColorScheme.Light, mode: "dark", dark: true },
            { tag: "dark pinned, unknown scheme", scheme: Qt.ColorScheme.Unknown, mode: "dark", dark: true },
            { tag: "unrecognised mode follows the scheme", scheme: Qt.ColorScheme.Dark, mode: "sepia", dark: true },
        ];
    }
    function test_isDark(data) {
        compare(ThemePolicy.colorSchemeDark, Qt.ColorScheme.Dark);
        compare(ThemePolicy.isDark(data.scheme, data.mode), data.dark);
    }

    function test_migrationKeepsLightDefaultsOfUntouchedSets() {
        const configuration = freshConfiguration();
        compare(ThemePolicy.migrateConfiguration(configuration), true);
        compare(configuration.themeConfigVersion, 1);
        compare(lightKeysAt(configuration, "desktop", untouched), "");
        compare(lightKeysAt(configuration, "panel", untouched), "");
        compare(ThemePolicy.migrateConfiguration(configuration), false);
    }

    function test_migrationCopiesCustomisedSetsWhole() {
        const configuration = freshConfiguration();
        configuration.desktopTextColor = "#ffcc00";
        compare(ThemePolicy.migrateConfiguration(configuration), true);
        // Every desktop key, the untouched ones too, so the light set is
        // exactly the look the instance had.
        compare(lightKeysAt(configuration, "desktop"), "");
        compare(configuration.desktopLightTextColor, "#ffcc00");
        compare(configuration.desktopLightSolidColor, "#99000000");
        compare(configuration.desktopLightWordBrightness, true);
        // The other form factor is judged on its own keys.
        compare(lightKeysAt(configuration, "panel", untouched), "");
    }

    function test_migrationCountsNonColourKeys() {
        const configuration = freshConfiguration();
        configuration.panelFontSize = 20;
        compare(ThemePolicy.migrateConfiguration(configuration), true);
        compare(lightKeysAt(configuration, "panel"), "");
        compare(configuration.panelLightFontSize, 20);
        compare(configuration.panelLightTextColor, "#fffaf5");
        compare(lightKeysAt(configuration, "desktop", untouched), "");
    }

    function test_migrationComparesColoursCaseInsensitively() {
        const configuration = freshConfiguration();
        configuration.desktopTextColor = "#FFFAF5";
        compare(ThemePolicy.migrateConfiguration(configuration), true);
        compare(lightKeysAt(configuration, "desktop", untouched), "");
    }

    function test_migrationRunsOnce() {
        const configuration = freshConfiguration();
        configuration.themeConfigVersion = 1;
        configuration.desktopTextColor = "#ffcc00";
        compare(ThemePolicy.migrateConfiguration(configuration), false);
        compare(configuration.desktopLightTextColor, untouched);
    }

    Component {
        id: themeComponent
        QtObject {
            id: harness
            property string desktopColorSchemeMode: "auto"
            property string desktopTextColor: "#fffaf5"
            property string desktopLightTextColor: "#1f1b16"
            property int scheme: Qt.ColorScheme.Light
            property int transitionMs: 400
            // What `transitioning` read at the moment `dark` changed, i.e.
            // what a Behavior's `enabled` would see when its colour updates.
            property var transitioningWhenDarkChanged: undefined
            property int darkChanges: 0
            property LyricsUi.AppearanceTheme theme: LyricsUi.AppearanceTheme {
                configuration: harness
                formFactor: "desktop"
                colorScheme: harness.scheme
                transitionMs: harness.transitionMs
                onDarkChanged: {
                    harness.transitioningWhenDarkChanged = transitioning;
                    ++harness.darkChanges;
                }
            }
            readonly property string textColor: harness.theme.value("TextColor")
        }
    }

    function test_themeStartsWithoutTransition() {
        const harness = createTemporaryObject(themeComponent, this, { scheme: Qt.ColorScheme.Dark });
        verify(harness !== null);
        compare(harness.theme.dark, true);
        compare(harness.theme.transitioning, false);
        compare(harness.textColor, "#fffaf5");

        const light = createTemporaryObject(themeComponent, this, { scheme: Qt.ColorScheme.Unknown });
        compare(light.theme.dark, false);
        compare(light.textColor, "#1f1b16");
    }

    function test_themeSwitchArmsTransitionFirst() {
        const harness = createTemporaryObject(themeComponent, this, { transitionMs: 150 });
        compare(harness.theme.dark, false);
        harness.scheme = Qt.ColorScheme.Dark;
        compare(harness.theme.dark, true);
        compare(harness.transitioningWhenDarkChanged, true);
        compare(harness.textColor, "#fffaf5");
        tryCompare(harness.theme, "transitioning", false, 1000);

        harness.scheme = Qt.ColorScheme.Light;
        compare(harness.transitioningWhenDarkChanged, true);
        compare(harness.textColor, "#1f1b16");
    }

    function test_themeModePinsASet() {
        const harness = createTemporaryObject(themeComponent, this,
            { scheme: Qt.ColorScheme.Dark, desktopColorSchemeMode: "light" });
        compare(harness.theme.dark, false);
        harness.scheme = Qt.ColorScheme.Light;
        harness.scheme = Qt.ColorScheme.Dark;
        compare(harness.darkChanges, 0);
        // Changing the mode is a switch like any other.
        harness.desktopColorSchemeMode = "auto";
        compare(harness.theme.dark, true);
        compare(harness.transitioningWhenDarkChanged, true);
    }

    function test_themeSwitchesInstantlyWithAnimationsOff() {
        const harness = createTemporaryObject(themeComponent, this, { transitionMs: 1 });
        harness.scheme = Qt.ColorScheme.Dark;
        compare(harness.theme.dark, true);
        compare(harness.transitioningWhenDarkChanged, false);
        compare(harness.theme.transitioning, false);
    }

    function test_themeEditOfSetInEffectDoesNotTransition() {
        const harness = createTemporaryObject(themeComponent, this);
        harness.desktopLightTextColor = "#003366";
        compare(harness.textColor, "#003366");
        compare(harness.theme.transitioning, false);
        // The set not in effect is not read at all.
        harness.desktopTextColor = "#ffffff";
        compare(harness.textColor, "#003366");
        compare(harness.darkChanges, 0);
    }

    // tst_appearance.qml's stand-in for LyricSource.
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
        }
    }

    // Every colour LyricsView fades, each away from its default.
    function fadedColours() {
        return {
            solidColor: "#80336699",
            textColor: "#1f1b16",
            strokeColor: "#ccffffff",
            secondLineColor: "#ad1f1b16",
            wordUnsungColor: "#8c1f1b16",
            wordActiveColor: "#e61f1b16",
            wordSungColor: "#c41f1b16",
            trackInfoColor: "#b31f1b16",
            trackInfoStrokeColor: "#ccffffff",
        };
    }
    function coloursAt(view, colours) {
        return Object.keys(colours).every(name => Qt.colorEqual(view[name], colours[name]));
    }

    function test_viewColoursFadeWhileAnimating() {
        const source = createTemporaryObject(fakeSourceComponent, this);
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, animateColors: true, colorTransitionMs: 100 });
        verify(view !== null);
        const colours = fadedColours();
        for (const name in colours) {
            verify(!Qt.colorEqual(view[name], colours[name]), name + " starts at its target");
            view[name] = colours[name];
        }
        for (const name in colours) {
            verify(!Qt.colorEqual(view[name], colours[name]), name + " did not fade");
        }
        // Not colours: these switch at once even while colours fade.
        view.plateMode = "solid";
        view.strokeEnabled = true;
        view.fontSize = 20;
        compare(view.plateMode, "solid");
        compare(view.strokeEnabled, true);
        compare(view.fontSize, 20);
        tryVerify(() => coloursAt(view, colours), 5000);
    }

    function test_viewColourFadeTakesTheGivenDuration() {
        const source = createTemporaryObject(fakeSourceComponent, this);
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, animateColors: true, colorTransitionMs: 60000 });
        view.textColor = "#1f1b16";
        // Twice ColorAnimation's own default duration: an animation that
        // ignored colorTransitionMs would be done by now.
        wait(500);
        verify(!Qt.colorEqual(view.textColor, "#1f1b16"));
    }

    function test_viewColoursApplyAtOnceOtherwise() {
        const source = createTemporaryObject(fakeSourceComponent, this);
        const view = createTemporaryObject(lyricsViewComponent, this,
            { source: source, colorTransitionMs: 60000 });
        compare(view.animateColors, false);
        const colours = fadedColours();
        for (const name in colours) {
            view[name] = colours[name];
            verify(Qt.colorEqual(view[name], colours[name]), name);
        }
    }

    // main.qml's wiring, which the suite cannot instantiate: a view reading
    // its keys through an AppearanceTheme, with the fade gated on the theme's
    // `transitioning`.
    Component {
        id: wiredViewComponent
        Item {
            id: wired
            property int scheme: Qt.ColorScheme.Light
            property int transitionMs: 150
            readonly property QtObject configuration: QtObject {
                property string desktopColorSchemeMode: "auto"
                property string desktopPlateMode: "ksvg"
                property string desktopLightPlateMode: "solid"
                property string desktopTextColor: "#fffaf5"
                property string desktopLightTextColor: "#1f1b16"
            }
            readonly property QtObject lyricSource: fakeSourceComponent.createObject(wired)
            readonly property alias theme: desktopTheme
            readonly property alias view: view

            LyricsUi.AppearanceTheme {
                id: desktopTheme
                configuration: wired.configuration
                formFactor: "desktop"
                colorScheme: wired.scheme
                transitionMs: wired.transitionMs
            }
            LyricsUi.LyricsView {
                id: view
                width: 400
                height: 300
                animationMode: "none"
                source: wired.lyricSource
                plateMode: desktopTheme.value("PlateMode")
                textColor: desktopTheme.value("TextColor")
                animateColors: desktopTheme.transitioning
                colorTransitionMs: desktopTheme.transitionMs
            }
        }
    }

    function test_wiredViewFadesOnlyWhenTheSetSwitches() {
        const wired = createTemporaryObject(wiredViewComponent, this);
        verify(wired !== null);
        const view = wired.view;
        // Starting up is not a switch.
        verify(Qt.colorEqual(view.textColor, "#1f1b16"));
        compare(view.plateMode, "solid");

        wired.scheme = Qt.ColorScheme.Dark;
        compare(view.animateColors, true);
        verify(!Qt.colorEqual(view.textColor, "#fffaf5"), "the text colour did not fade");
        compare(view.plateMode, "ksvg");
        tryVerify(() => Qt.colorEqual(view.textColor, "#fffaf5"), 5000);
        tryCompare(view, "animateColors", false, 5000);

        // Editing the set in effect applies at once, colour or not.
        wired.configuration.desktopTextColor = "#336699";
        verify(Qt.colorEqual(view.textColor, "#336699"));
        wired.configuration.desktopPlateMode = "none";
        compare(view.plateMode, "none");
    }

    function test_wiredViewSwitchesAtOnceWithAnimationsOff() {
        const wired = createTemporaryObject(wiredViewComponent, this, { transitionMs: 1 });
        wired.scheme = Qt.ColorScheme.Dark;
        compare(wired.view.animateColors, false);
        verify(Qt.colorEqual(wired.view.textColor, "#fffaf5"));
        compare(wired.view.plateMode, "ksvg");
    }
}
