.pragma library

function effectiveText(useDefault, configuredText, localizedDefault) {
    return useDefault ? localizedDefault : configuredText;
}

function migrateConfiguration(configuration) {
    const currentVersion = 1;
    if (configuration.textConfigVersion >= currentVersion) {
        return false;
    }

    // notFoundText predates its companion flag.  Preserve an existing custom
    // value when the new schema first appears; fresh instances have an empty
    // value and retain the localized-default behavior.  The other two texts
    // were introduced together with their flags and need no migration.
    if (configuration.notFoundText.length > 0) {
        configuration.notFoundTextUseDefault = false;
    }
    configuration.textConfigVersion = currentVersion;
    return true;
}
