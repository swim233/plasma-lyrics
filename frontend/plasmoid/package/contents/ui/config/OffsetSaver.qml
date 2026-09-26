import QtQuick

// What the Global settings page's Apply does (DESIGN.md decisions 41 and
// 79), kept out of ConfigGlobal.qml for the same reason as
// TrackOffsetEditor: the QML tests drive it with stand-ins. The global value
// and the current song's own offset are saved and reported separately; one
// failing neither undoes nor blocks the other, and the failed one stays
// unsaved so Apply can retry it.
QtObject {
    id: saver

    // A GlobalConfig and a TrackOffsetEditor, or stand-ins with the same
    // members.
    required property var config
    required property var editor

    readonly property bool unsavedChanges: config.unsavedChanges || editor.unsaved
    property bool saveFailed: false

    function save() {
        // Only a changed global value is written, so an Apply for the song's
        // own offset alone cannot report an unrelated global failure.
        if (config.unsavedChanges) {
            saveFailed = !config.save();
            if (saveFailed) {
                // AppletConfiguration.qml's applyAction (:462-468) calls the
                // page's saveConfig() and then unconditionally sets
                // applyButton.enabled = false at :467; only
                // unsavedChangesChanged turns it back on (settingValueChanged(),
                // :104-105, hooked up at :197-199). A global save fails with
                // unsavedChanges still true, so nothing changes and Apply
                // would stay off. Sent again once the dialog has disabled the
                // button, it is turned back on. The per-song save answers
                // asynchronously while its value counts as saved, so its
                // failure flips unsavedChanges back on by itself.
                Qt.callLater(saver.unsavedChangesChanged);
            }
        }
        // Whatever became of the global value.
        editor.save();
    }
}
