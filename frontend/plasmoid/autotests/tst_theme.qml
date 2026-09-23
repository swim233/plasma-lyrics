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
        compare(ThemePolicy.modeKey("panel"), "panelThemeMode");
    }

    function test_isDark_data() {
        return [
            { tag: "auto, dark style", styleDark: true, mode: "auto", dark: true },
            { tag: "auto, light style", styleDark: false, mode: "auto", dark: false },
            { tag: "light pinned, dark style", styleDark: true, mode: "light", dark: false },
            { tag: "dark pinned, light style", styleDark: false, mode: "dark", dark: true },
            { tag: "unrecognised mode follows the style", styleDark: true, mode: "sepia", dark: true },
        ];
    }
    function test_isDark(data) {
        compare(ThemePolicy.isDark(data.styleDark, data.mode), data.dark);
    }

    // The window backgrounds plasmoidviewer reported for breeze-dark and for
    // BreezeLight, and the two sides of qGray() == 192.
    function test_isDarkBackground() {
        compare(ThemePolicy.isDarkBackground(Qt.color("#202326")), true);
        compare(ThemePolicy.isDarkBackground(Qt.color("#eff0f1")), false);
        compare(ThemePolicy.isDarkBackground(Qt.rgba(192 / 255, 192 / 255, 192 / 255, 1)), false);
        compare(ThemePolicy.isDarkBackground(Qt.rgba(191 / 255, 191 / 255, 191 / 255, 1)), true);
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
            property string desktopThemeMode: "auto"
            property string desktopTextColor: "#fffaf5"
            property string desktopLightTextColor: "#1f1b16"
            property bool styleDark: false
            property int transitionMs: 400
            // What `transitioning` read at the moment `dark` changed, i.e.
            // what a Behavior's `enabled` would see when its colour updates.
            property var transitioningWhenDarkChanged: undefined
            property int darkChanges: 0
            property LyricsUi.AppearanceTheme theme: LyricsUi.AppearanceTheme {
                configuration: harness
                formFactor: "desktop"
                styleDark: harness.styleDark
                transitionMs: harness.transitionMs
                onDarkChanged: {
                    harness.transitioningWhenDarkChanged = transitioning;
                    ++harness.darkChanges;
                }
            }
            readonly property string textColor: harness.theme.value("TextColor")
        }
    }

    // AppearanceTheme counts switches only from the turn of the event loop
    // after its creation; a test that switches has to wait that turn out.
    function createSettled(component, properties) {
        const object = createTemporaryObject(component, this, properties);
        verify(object !== null);
        tryCompare(object.theme, "settled", true);
        return object;
    }

    function test_themeStartsWithoutTransition() {
        const harness = createTemporaryObject(themeComponent, this, { styleDark: true });
        verify(harness !== null);
        compare(harness.theme.dark, true);
        compare(harness.theme.transitioning, false);
        compare(harness.textColor, "#fffaf5");

        const light = createTemporaryObject(themeComponent, this, { styleDark: false });
        compare(light.theme.dark, false);
        compare(light.textColor, "#1f1b16");
    }

    // An applet's Kirigami.Theme reads #000000 until Plasma parents the item,
    // which happens in the same turn of the event loop as its creation
    // (measured with plasmoidviewer). styleDark changing then is the style
    // settling, not a switch.
    function test_themeStyleSettlingIsNotASwitch() {
        const harness = createTemporaryObject(themeComponent, this, { styleDark: true });
        compare(harness.theme.settled, false);
        harness.styleDark = false;
        compare(harness.theme.dark, false);
        compare(harness.transitioningWhenDarkChanged, false);
        compare(harness.theme.transitioning, false);
        compare(harness.textColor, "#1f1b16");
        tryCompare(harness.theme, "settled", true);
        compare(harness.theme.dark, false);

        harness.styleDark = true;
        compare(harness.transitioningWhenDarkChanged, true);
    }

    // main.qml calls holdStill() whenever the widget's root item changes
    // parent -- the same #000000-then-style's-colour sequence as at startup.
    function test_themeHoldsStillForOneTurn() {
        const harness = createSettled(themeComponent, { styleDark: true });
        tryCompare(harness.theme, "transitioning", false);
        const changes = harness.darkChanges;

        harness.theme.holdStill();
        compare(harness.theme.settled, false);
        harness.styleDark = false;
        compare(harness.theme.dark, false);
        compare(harness.transitioningWhenDarkChanged, false);
        harness.styleDark = true;
        compare(harness.theme.dark, true);
        compare(harness.theme.transitioning, false);
        compare(harness.darkChanges, changes + 2);

        tryCompare(harness.theme, "settled", true);
        harness.styleDark = false;
        compare(harness.transitioningWhenDarkChanged, true);
        compare(harness.theme.transitioning, true);
    }

    function test_themeSwitchArmsTransitionFirst() {
        const harness = createSettled(themeComponent, { transitionMs: 150 });
        compare(harness.theme.dark, false);
        harness.styleDark = true;
        compare(harness.theme.dark, true);
        compare(harness.transitioningWhenDarkChanged, true);
        compare(harness.textColor, "#fffaf5");
        tryCompare(harness.theme, "transitioning", false, 1000);

        harness.styleDark = false;
        compare(harness.transitioningWhenDarkChanged, true);
        compare(harness.textColor, "#1f1b16");
    }

    function test_themeModePinsASet() {
        const harness = createSettled(themeComponent,
            { styleDark: true, desktopThemeMode: "light" });
        compare(harness.theme.dark, false);
        harness.styleDark = false;
        harness.styleDark = true;
        compare(harness.darkChanges, 0);
        // Changing the mode is a switch like any other.
        harness.desktopThemeMode = "auto";
        compare(harness.theme.dark, true);
        compare(harness.transitioningWhenDarkChanged, true);
    }

    function test_themeSwitchesInstantlyWithAnimationsOff() {
        const harness = createSettled(themeComponent, { transitionMs: 1 });
        harness.styleDark = true;
        compare(harness.theme.dark, true);
        compare(harness.transitioningWhenDarkChanged, false);
        compare(harness.theme.transitioning, false);
    }

    function test_themeEditOfSetInEffectDoesNotTransition() {
        const harness = createSettled(themeComponent);
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
            property bool styleDark: false
            property int transitionMs: 150
            readonly property QtObject configuration: QtObject {
                property string desktopThemeMode: "auto"
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
                styleDark: wired.styleDark
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
        const wired = createSettled(wiredViewComponent);
        const view = wired.view;
        // Starting up is not a switch.
        verify(Qt.colorEqual(view.textColor, "#1f1b16"));
        compare(view.plateMode, "solid");

        wired.styleDark = true;
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
        const wired = createSettled(wiredViewComponent, { transitionMs: 1 });
        wired.styleDark = true;
        compare(wired.view.animateColors, false);
        verify(Qt.colorEqual(wired.view.textColor, "#fffaf5"));
        compare(wired.view.plateMode, "ksvg");
    }

    function test_wiredViewDoesNotFadeWhileTheStyleSettles() {
        const wired = createTemporaryObject(wiredViewComponent, this, { styleDark: true });
        verify(Qt.colorEqual(wired.view.textColor, "#fffaf5"));
        wired.styleDark = false;
        compare(wired.view.animateColors, false);
        verify(Qt.colorEqual(wired.view.textColor, "#1f1b16"));
        compare(wired.view.plateMode, "solid");
    }
}
