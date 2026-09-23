.pragma library

// DESIGN.md decision 76: every instance keeps two sets of the appearance
// keys listed here, one for a dark Plasma style and one for a light one.
// The dark set is the original <form><Suffix> key, the light set the
// <form>Light<Suffix> key main.xml declares next to it. Everything the
// appearance pages show that is not in this table -- the rest of the track
// info rows, auto-hide, the panel width -- is one key shared by both sets.
//
// Each suffix maps to its dark key's default, which has to equal that key's
// <default> in main.xml: the migration below tells a customised set from an
// untouched one by comparing against these. tst_configschema reads this
// table straight out of this file and checks it against main.xml, so keep
// one `Suffix: value,` per line between the two marker comments.
//
// BEGIN darkDefaults
var darkDefaults = {
    desktop: {
        PlateMode: "ksvg",
        SolidColor: "#99000000",
        TextColor: "#fffaf5",
        Stroke: false,
        StrokeColor: "#cc000000",
        FontFamily: "",
        FontSize: 34,
        FontWeight: 700,
        Overflow: "fit",
        Animation: "slide",
        ShowTranslation: true,
        SecondLineSource: "translation",
        SecondLineColorEnabled: false,
        SecondLineColor: "#adfffaf5",
        LineHeight: 125,
        WordByWord: true,
        WordByWordSynthetic: false,
        WordUnsungColor: "#8cfffaf5",
        WordActiveColor: "#e6fffaf5",
        WordSungColor: "#c4fffaf5",
        WordLift: true,
        WordLiftPercent: 14,
        WordBrightness: true,
        WordBrightnessPercent: 60,
        WordBlurGlow: false,
        TrackInfoColor: "#b3fffaf5",
        TrackInfoStroke: false,
        TrackInfoStrokeColor: "#cc000000",
    },
    panel: {
        PlateMode: "ksvg",
        SolidColor: "#99000000",
        TextColor: "#fffaf5",
        Stroke: false,
        StrokeColor: "#cc000000",
        FontFamily: "",
        FontSize: 16,
        FontWeight: 400,
        Overflow: "fit",
        Animation: "fade",
        ShowTranslation: false,
        SecondLineSource: "translation",
        SecondLineColorEnabled: false,
        SecondLineColor: "#adfffaf5",
        LineHeight: 125,
        WordByWord: true,
        WordByWordSynthetic: false,
        WordUnsungColor: "#8cfffaf5",
        WordActiveColor: "#e6fffaf5",
        WordSungColor: "#c4fffaf5",
        WordBrightness: true,
        WordBrightnessPercent: 60,
        WordBlurGlow: false,
        TrackInfoColor: "#b3fffaf5",
        TrackInfoStroke: false,
        TrackInfoStrokeColor: "#cc000000",
    },
};
// END darkDefaults

// The suffixes of the keys that come in a light and a dark copy, for
// "desktop" or "panel" (the key prefix, not Plasmoid.formFactor): 28 for the
// desktop, 26 for the panel, which has no lift keys.
function themedSuffixes(formFactor) {
    return Object.keys(darkDefaults[formFactor]);
}

// The prefix of the set in effect: <form> for dark, <form>Light for light.
function keyPrefix(formFactor, dark) {
    return dark ? formFactor : formFactor + "Light";
}

function modeKey(formFactor) {
    return formFactor + "ThemeMode";
}

// Whether a background colour is a dark one: qGray() below 192, the rule
// plasma-integration's KHintsSettings::determineColorScheme() and
// xdg-desktop-portal-kde's readFdoColorScheme() apply to the window
// background. main.qml gives it Kirigami.Theme.backgroundColor, which inside
// the widget is the Plasma style's (decision 76); PlasmaStyle::isDark(), for
// the config pages, applies the same rule in C++.
function isDarkBackground(color) {
    const gray = Math.floor((Math.round(color.r * 255) * 11
                             + Math.round(color.g * 255) * 16
                             + Math.round(color.b * 255) * 5) / 32);
    return gray < 192;
}

// "light" and "dark" pin a set; anything else, "auto" included, follows the
// Plasma style: `styleDark` is whether it is a dark one.
function isDark(styleDark, mode) {
    if (mode === "dark") {
        return true;
    }
    if (mode === "light") {
        return false;
    }
    return styleDark === true;
}

// Colours compare case-insensitively: a hand-edited "#FFFAF5" is the
// default, not a customisation.
function isDefaultValue(value, defaultValue) {
    if (typeof defaultValue === "string" && defaultValue.startsWith("#") && typeof value === "string") {
        return value.toLowerCase() === defaultValue;
    }
    return value === defaultValue;
}

// Version 1 creates the light set. A form factor whose dark set has any key
// off its default had its look chosen before the light set existed, so the
// whole set is copied across and the instance looks the same under either
// Plasma style. An untouched set -- which is also what a freshly added
// instance has -- keeps the light defaults from main.xml. Whole sets, never
// key by key: a custom colour next to a light default of its neighbour would
// be a combination nobody picked.
function migrateConfiguration(configuration) {
    const currentVersion = 1;
    if (configuration.themeConfigVersion >= currentVersion) {
        return false;
    }
    for (const formFactor of Object.keys(darkDefaults)) {
        const defaults = darkDefaults[formFactor];
        const suffixes = Object.keys(defaults);
        const customised = suffixes.some(
            suffix => !isDefaultValue(configuration[formFactor + suffix], defaults[suffix]));
        if (!customised) {
            continue;
        }
        for (const suffix of suffixes) {
            configuration[keyPrefix(formFactor, false) + suffix] = configuration[formFactor + suffix];
        }
    }
    configuration.themeConfigVersion = currentVersion;
    return true;
}
