import QtQuick

// The state behind the Global settings page's "Current song" and "This
// song's offset" rows (DESIGN.md decision 79), kept out of ConfigGlobal.qml
// so the QML tests can drive it with a stand-in source: the page itself
// opens the real LyricStore and snapshot and stays out of that suite, as
// decision 41 records.
//
// Unedited, the rows follow the snapshot, so a song change or a menu
// adjustment shows at once. The first edit pins them to that song's lyric
// ref and title until a save of that value succeeds: a later song change
// only marks the title, and menu adjustments no longer reach the value,
// which save() then writes over them.
QtObject {
    id: editor

    // A LyricSource, or a stand-in with the same members.
    required property var source

    property bool edited: false
    property int editedValue: 0
    property string pinnedProvider: ""
    property string pinnedTrackId: ""
    property string pinnedTitle: ""
    property string pinnedArtists: ""

    // The request on its way to the daemon. Only the answer to it counts.
    property bool saving: false
    property string sentProvider: ""
    property string sentTrackId: ""
    property int sentValue: 0

    // Localized, empty while the last save has not failed.
    property string saveError: ""

    readonly property bool hasSong: source.serviceAvailable && source.fingerprint.length > 0
    readonly property bool hasRef: source.lyricRefProvider.length > 0
        && source.lyricRefTrackId.length > 0
    // Why the value cannot be edited: "no-song", "no-lyrics", or "" when it
    // can. A pinned value stays editable whatever is playing now.
    readonly property string disabledReason: edited ? ""
        : !hasSong ? "no-song"
        : !hasRef ? "no-lyrics"
        : ""
    readonly property int value: edited ? editedValue : source.trackOffsetMs
    readonly property bool songChanged: edited
        && (source.lyricRefProvider !== pinnedProvider || source.lyricRefTrackId !== pinnedTrackId)
    // A value already on its way to the daemon is not an unsaved change; one
    // edited again while that request is out is.
    readonly property bool unsaved: edited
        && !(saving && sentProvider === pinnedProvider && sentTrackId === pinnedTrackId
             && sentValue === editedValue)

    readonly property string songText: {
        if (!edited && !hasSong) {
            return i18n("No song is playing");
        }
        const title = edited ? pinnedTitle : source.trackTitle;
        const artists = edited ? pinnedArtists : source.trackArtists;
        const song = artists.length > 0
            ? i18nc("@info track info: %1 is the song title, %2 is the artist name(s)", "%1 — %2", title, artists)
            : title;
        return songChanged ? i18n("%1 (song changed)", song) : song;
    }
    readonly property string disabledReasonText: disabledReason === "no-lyrics"
        ? i18n("The current song has no lyrics")
        : ""

    function edit(newValue) {
        if (!edited) {
            pinnedProvider = source.lyricRefProvider;
            pinnedTrackId = source.lyricRefTrackId;
            pinnedTitle = source.trackTitle;
            pinnedArtists = source.trackArtists;
            edited = true;
        }
        editedValue = newValue;
    }

    function save() {
        if (!unsaved) {
            return;
        }
        sentProvider = pinnedProvider;
        sentTrackId = pinnedTrackId;
        sentValue = editedValue;
        saving = true;
        saveError = "";
        source.setOffsetForTrack(sentProvider, sentTrackId, sentValue);
    }

    readonly property Connections replies: Connections {
        target: editor.source
        function onOffsetForTrackFinished(provider, trackId, offsetMs, error) {
            if (!editor.saving || provider !== editor.sentProvider
                || trackId !== editor.sentTrackId || offsetMs !== editor.sentValue) {
                return;
            }
            editor.saving = false;
            editor.saveError = error;
            // Edited again while this was out: that newer value stays pinned
            // and unsaved.
            if (error.length > 0 || editor.editedValue !== offsetMs) {
                return;
            }
            // The daemon republished the snapshot before it answered; read
            // it now, so the rows do not show the old value until the file
            // watcher catches up.
            editor.source.reload();
            editor.edited = false;
        }
    }
}
