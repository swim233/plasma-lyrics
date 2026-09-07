.pragma library

function effectiveText(useDefault, configuredText, localizedDefault) {
    return useDefault ? localizedDefault : configuredText;
}
