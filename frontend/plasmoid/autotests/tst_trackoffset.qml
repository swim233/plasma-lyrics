import QtQuick
import QtTest
import "../package/contents/ui/config" as LyricsConfig

// DESIGN.md decision 79: the Global settings page's per-song offset rows
// and its Apply. TrackOffsetEditor carries the rows' state and OffsetSaver
// what Apply does; ConfigGlobal.qml itself opens the real LyricStore and
// snapshot and is not instantiated here (decision 41).
TestCase {
    name: "TrackOffset"

    // Stands in for LyricSource: the members TrackOffsetEditor reads, and a
    // record of the SetOffsetForTrack requests it makes.
    Component {
        id: sourceComponent
        QtObject {
            property bool serviceAvailable: true
            property string fingerprint: "mediaSrc:a"
            property string trackTitle: "Song A"
            property string trackArtists: "Artist A"
            property string lyricRefProvider: "netease"
            property string lyricRefTrackId: "1"
            property int trackOffsetMs: 200
            property var requests: []
            property int reloads: 0
            signal offsetForTrackFinished(string provider, string trackId, int offsetMs, string error)
            function setOffsetForTrack(provider, trackId, offsetMs) {
                requests = requests.concat([{ provider: provider, trackId: trackId, offsetMs: offsetMs }]);
            }
            function reload() {
                ++reloads;
            }
            function playSongB() {
                fingerprint = "mediaSrc:b";
                trackTitle = "Song B";
                trackArtists = "";
                lyricRefProvider = "qq";
                lyricRefTrackId = "2";
                trackOffsetMs = -300;
            }
        }
    }

    Component {
        id: editorComponent
        LyricsConfig.TrackOffsetEditor {}
    }

    // Stands in for GlobalConfig: whether it has unsaved changes, and what
    // its next save() returns.
    Component {
        id: configComponent
        QtObject {
            property bool unsavedChanges: false
            property bool saveSucceeds: true
            property int saves: 0
            function save() {
                ++saves;
                if (saveSucceeds) {
                    unsavedChanges = false;
                }
                return saveSucceeds;
            }
        }
    }

    Component {
        id: saverComponent
        LyricsConfig.OffsetSaver {}
    }

    function createEditor() {
        const source = createTemporaryObject(sourceComponent, this);
        const editor = createTemporaryObject(editorComponent, this, { source: source });
        verify(editor !== null);
        return editor;
    }

    function createSaver() {
        const editor = createEditor();
        const config = createTemporaryObject(configComponent, this);
        const saver = createTemporaryObject(saverComponent, this, { config: config, editor: editor });
        verify(saver !== null);
        return saver;
    }

    function test_uneditedRowsFollowTheSnapshot() {
        const editor = createEditor();
        const source = editor.source;
        compare(editor.value, 200);
        compare(editor.songText, "Song A — Artist A");
        compare(editor.disabledReason, "");
        verify(!editor.unsaved);

        // A menu adjustment, then a song change.
        source.trackOffsetMs = 700;
        compare(editor.value, 700);
        source.playSongB();
        compare(editor.value, -300);
        compare(editor.songText, "Song B");
        verify(!editor.songChanged);
        verify(!editor.unsaved);
    }

    function test_noSongOrNoLyricsDisablesTheValue() {
        const editor = createEditor();
        const source = editor.source;

        source.lyricRefProvider = "";
        source.lyricRefTrackId = "";
        source.trackOffsetMs = 0;
        compare(editor.disabledReason, "no-lyrics");
        compare(editor.disabledReasonText, "The current song has no lyrics");
        compare(editor.songText, "Song A — Artist A");

        source.fingerprint = "";
        compare(editor.disabledReason, "no-song");
        compare(editor.songText, "No song is playing");
        compare(editor.disabledReasonText, "");

        // A daemon that went away leaves its last snapshot behind; that is
        // no current song either.
        source.fingerprint = "mediaSrc:a";
        source.lyricRefProvider = "netease";
        source.lyricRefTrackId = "1";
        compare(editor.disabledReason, "");
        source.serviceAvailable = false;
        compare(editor.disabledReason, "no-song");
        compare(editor.songText, "No song is playing");
    }

    function test_anEditPinsTheSongUntilItIsSaved() {
        const editor = createEditor();
        const source = editor.source;
        editor.edit(500);
        verify(editor.edited);
        verify(editor.unsaved);
        compare(editor.value, 500);

        // Menu adjustments to the pinned song no longer reach the value.
        source.trackOffsetMs = 1000;
        compare(editor.value, 500);
        editor.edit(600);
        compare(editor.value, 600);

        // After a song change the title stays, marked; the value stays
        // editable even with nothing playing.
        source.playSongB();
        verify(editor.songChanged);
        compare(editor.songText, "Song A — Artist A (song changed)");
        compare(editor.value, 600);
        source.fingerprint = "";
        source.serviceAvailable = false;
        compare(editor.disabledReason, "");
        source.fingerprint = "mediaSrc:b";
        source.serviceAvailable = true;

        // Apply writes the absolute value to the pinned ref.
        editor.save();
        compare(source.requests.length, 1);
        compare(source.requests[0].provider, "netease");
        compare(source.requests[0].trackId, "1");
        compare(source.requests[0].offsetMs, 600);
        // On its way, it no longer counts as unsaved.
        verify(!editor.unsaved);
        verify(editor.edited);

        source.offsetForTrackFinished("netease", "1", 600, "");
        compare(source.reloads, 1);
        verify(!editor.edited);
        verify(!editor.unsaved);
        compare(editor.saveError, "");
        // And the rows follow the current song again.
        compare(editor.value, -300);
        compare(editor.songText, "Song B");
    }

    function test_aFailedSaveStaysPinnedAndUnsaved() {
        const editor = createEditor();
        const source = editor.source;
        editor.edit(-800);
        editor.save();
        verify(!editor.unsaved);

        source.offsetForTrackFinished("netease", "1", -800, "Could not save the lyric offset.");
        compare(editor.saveError, "Could not save the lyric offset.");
        verify(editor.edited);
        verify(editor.unsaved);
        compare(editor.value, -800);
        compare(source.reloads, 0);

        // Apply again retries it and clears the message until it answers.
        editor.save();
        compare(source.requests.length, 2);
        compare(editor.saveError, "");
        source.offsetForTrackFinished("netease", "1", -800, "");
        verify(!editor.edited);
        compare(editor.value, 200);
    }

    function test_anEditWhileSavingStaysUnsaved() {
        const editor = createEditor();
        const source = editor.source;
        editor.edit(500);
        editor.save();
        editor.edit(900);
        verify(editor.unsaved);

        source.offsetForTrackFinished("netease", "1", 500, "");
        verify(editor.edited);
        verify(editor.unsaved);
        compare(editor.value, 900);
        compare(source.reloads, 0);

        editor.save();
        compare(source.requests.length, 2);
        compare(source.requests[1].offsetMs, 900);
    }

    function test_onlyTheAnswerToTheRequestOutCounts() {
        const editor = createEditor();
        const source = editor.source;
        // Nothing sent yet.
        source.offsetForTrackFinished("netease", "1", 200, "");
        editor.edit(500);
        source.offsetForTrackFinished("netease", "1", 500, "");
        verify(editor.edited);

        editor.save();
        source.offsetForTrackFinished("qq", "1", 500, "");
        source.offsetForTrackFinished("netease", "2", 500, "");
        source.offsetForTrackFinished("netease", "1", 400, "boom");
        verify(editor.saving);
        verify(editor.edited);
        compare(editor.saveError, "");

        // Nothing unsaved: Apply does not send again.
        editor.save();
        compare(source.requests.length, 1);
    }
    function test_aGlobalFailureStillSavesTheSongsOffset() {
        const saver = createSaver();
        const source = saver.editor.source;
        saver.config.unsavedChanges = true;
        saver.config.saveSucceeds = false;
        saver.editor.edit(500);

        saver.save();
        compare(saver.config.saves, 1);
        verify(saver.saveFailed);
        compare(source.requests.length, 1);
        compare(source.requests[0].offsetMs, 500);
        verify(saver.unsavedChanges);
    }

    function test_aGlobalFailureSignalsUnsavedChangesAgainOnTheNextEventLoop() {
        const saver = createSaver();
        saver.config.unsavedChanges = true;
        saver.config.saveSucceeds = false;
        let signals = 0;
        saver.unsavedChangesChanged.connect(() => ++signals);

        saver.save();
        // Not yet: the shell disables Apply right after saveConfig() returns,
        // and only a later signal turns it back on.
        compare(signals, 0);
        tryCompare(saver, "saveFailed", true);
        tryVerify(() => signals === 1);
        verify(saver.unsavedChanges);
    }

    function test_anUnchangedGlobalValueIsNotSaved() {
        const saver = createSaver();
        const source = saver.editor.source;
        saver.editor.edit(300);

        saver.save();
        compare(saver.config.saves, 0);
        verify(!saver.saveFailed);
        compare(source.requests.length, 1);
        compare(source.requests[0].offsetMs, 300);
    }

    function test_aSuccessfulGlobalSaveClearsTheFailure() {
        const saver = createSaver();
        saver.config.unsavedChanges = true;
        saver.config.saveSucceeds = false;
        saver.save();
        verify(saver.saveFailed);
        // Let that failure's own resend go out first.
        wait(0);
        let signals = 0;
        saver.unsavedChangesChanged.connect(() => ++signals);

        saver.config.saveSucceeds = true;
        saver.save();
        verify(!saver.saveFailed);
        verify(!saver.unsavedChanges);
        wait(0);
        // Only the real change to false; no resend after a success.
        compare(signals, 1);
        // Nothing edited, nothing sent for the song.
        compare(saver.editor.source.requests.length, 0);
    }
}
