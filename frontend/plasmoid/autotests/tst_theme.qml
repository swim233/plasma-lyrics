import QtQuick
import QtTest
import "../package/contents/ui" as LyricsUi
import "../package/contents/ui/ThemePolicy.js" as ThemePolicy

// DESIGN.md decision 75: the light/dark appearance sets. ThemePolicy.js's key
// table and migration, and AppearanceTheme's choice of set and its gate on
// colour animation.
TestCase {
    name: "Theme"

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
}
