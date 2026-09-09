.pragma library

function effectiveText(useDefault, configuredText, localizedDefault) {
    if (configuredText.length > 0) {
        return configuredText;
    }
    return useDefault ? localizedDefault : "";
}

function migrateConfiguration(configuration) {
    const currentVersion = 2;
    if (configuration.textConfigVersion >= currentVersion) {
        return false;
    }

    // Version 1 introduced the per-message flags. notFoundText predates its
    // flag, so preserve an existing custom value when upgrading directly from
    // version 0 before folding all four legacy choices into the shared flag.
    if (configuration.textConfigVersion < 1 && configuration.notFoundText.length > 0) {
        configuration.notFoundTextUseDefault = false;
    }

    // Version 2 replaces the four per-message switches with one empty-text
    // policy. Keep localized fallbacks enabled if any old message used one;
    // only an instance that explicitly disabled all four migrates to off.
    configuration.emptyTextUseDefault = configuration.idleTextUseDefault
        || configuration.notFoundTextUseDefault
        || configuration.noLyricTextUseDefault
        || configuration.networkErrorTextUseDefault;
    configuration.textConfigVersion = currentVersion;
    return true;
}
