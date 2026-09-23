.pragma library

// DESIGN.md decision 30: the family is the widget's own setting, defaulting to
// the Plasma font, and the weight is always one of the family's real faces.
// Both main.qml (what is drawn) and the appearance pages (what the weight list
// offers) go through these functions, so the two cannot disagree. `catalog`
// is the FontCatalog singleton, or a stand-in with the same functions in
// tests; `systemFamily` is Kirigami.Theme.defaultFont.family, passed in
// because a .pragma library script cannot reach Kirigami itself.

// The Plasma font under the name the catalog lists it by. kdeglobals keeps
// whatever name the font was picked under, which need not be the name the
// current locale lists ("Microsoft YaHei" where zh_CN lists "微软雅黑");
// without this, following the Plasma font and picking the same font from
// the list would be two different effective families. A name the catalog
// cannot resolve is returned as it is.
function plasmaFamily(catalog, systemFamily) {
    if (!systemFamily) {
        return systemFamily;
    }
    const listed = catalog.resolveFamily(systemFamily);
    return listed.length > 0 ? listed : systemFamily;
}

// The family the lyrics render in. `stored` is <form>FontFamily: empty
// follows the Plasma font, and so does a stored family that is not installed.
function lyricFamily(catalog, stored, systemFamily) {
    if (!stored) {
        return plasmaFamily(catalog, systemFamily);
    }
    const listed = catalog.resolveFamily(stored);
    return listed.length > 0 ? listed : plasmaFamily(catalog, systemFamily);
}

// The family the track info renders in. `sameAsLyrics` and `stored` are
// <form>TrackInfoFontSameAsLyrics and <form>TrackInfoFontFamily;
// `lyricEffectiveFamily` is lyricFamily()'s result for the same form.
function trackInfoFamily(catalog, sameAsLyrics, stored, lyricEffectiveFamily, systemFamily) {
    return sameAsLyrics ? lyricEffectiveFamily : lyricFamily(catalog, stored, systemFamily);
}

// True when a family is stored but not installed. The picker shows such a
// value as its own "(not installed)" entry rather than as any listed family.
function isMissing(catalog, stored) {
    return !!stored && catalog.resolveFamily(stored).length === 0;
}

// The weight to request for `family`, one of its real faces, so fontconfig
// never synthesizes bold. `storedWeight` is left as it is in the config.
function renderWeight(catalog, family, storedWeight) {
    return catalog.snapWeight(catalog.weights(family), storedWeight);
}
