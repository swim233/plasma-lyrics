import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import io.github.swim233.lyrics

// DESIGN.md decisions 41 and 79. The global offset lives in the LyricStore
// SQLite database rather than kcfg (unlike every other tab), so this page
// cannot use the cfg_ auto-binding the other tabs rely on -- it goes through
// KDE's official non-kcfg hooks instead (`AppletConfiguration.qml`'s
// Apply/OK path calls saveConfig() and gates the Apply button on
// unsavedChanges), the same pair the shell already offers third-party config
// pages for exactly this case. The current song's own offset is committed by
// the same Apply, through the daemon's SetOffsetForTrack.
Kirigami.ScrollablePage {
    id: page

    property bool unsavedChanges: globalConfig.unsavedChanges || trackOffset.unsaved
    // The global and the per-song value are saved and reported separately:
    // one failing neither undoes nor blocks the other, and the failed one
    // stays unsaved so Apply can retry it.
    //
    // AppletConfiguration.qml's applyAction (:462-468) calls saveConfig()
    // and then unconditionally sets applyButton.enabled = false at :467, and
    // only unsavedChangesChanged turns it back on (settingValueChanged(),
    // :104-105, hooked up at :197-199). A global save fails synchronously
    // with unsavedChanges still true, so no change is signalled and Apply
    // would stay off; the signal is re-sent once the dialog has disabled the
    // button. The per-song save answers asynchronously, while its value
    // counts as saved, so its failure flips unsavedChanges back on by itself.
    //
    // The error messages are only reliable on the Apply path, though. The OK
    // button (:454-459, and Enter at :480) calls applyAction.trigger() and
    // then unconditionally closes the dialog; closing()'s "don't close with
    // unsaved changes" guard (:42-47) reads applyButton.enabled, which :467
    // has already cleared, so nothing this page raises reaches it first --
    // and the per-song answer arrives after the page is gone. Kept anyway
    // (DESIGN.md decision 41's accepted-residue note): it is free, strictly
    // better than nothing, and someone dialing in an offset by ear mostly
    // lives on the Apply path rather than OK/Enter.
    property bool saveFailed: false
    function saveConfig() {
        if (globalConfig.unsavedChanges) {
            page.saveFailed = !globalConfig.save();
            if (page.saveFailed) {
                Qt.callLater(page.unsavedChangesChanged);
            }
        }
        trackOffset.save();
    }

    GlobalConfig { id: globalConfig }
    // A snapshot reader like each widget's own, for the current song and its
    // offset. It only reads the daemon's snapshot file.
    LyricSource { id: lyricSource }
    TrackOffsetEditor {
        id: trackOffset
        source: lyricSource
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // Unlike the "Lyrics Service" tab (restart required) or the three
        // per-instance tabs (this widget only), this page takes effect
        // immediately and reaches every widget without a restart -- worth
        // spelling out since it disagrees with both of its neighbors.
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: true
            type: Kirigami.MessageType.Information
            text: i18n("This setting affects every Desktop Lyrics widget and takes effect immediately, without restarting the lyrics service.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.saveFailed
            type: Kirigami.MessageType.Error
            text: i18n("Could not save the global offset. The setting was not applied.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: trackOffset.saveError.length > 0
            type: Kirigami.MessageType.Error
            // Two complete sentences, each translated whole: what failed,
            // then the daemon's own reason.
            text: i18n("Could not save this song's offset. The setting was not applied.")
                + "\n" + trackOffset.saveError
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Lyric offset:")
                text: i18n("Use one offset for all songs")
                checked: globalConfig.enabled
                onToggled: globalConfig.enabled = checked
            }
            QQC2.Label {
                visible: globalConfig.enabled
                Layout.fillWidth: true
                // Capped and styled as AppearanceSection's formDescription
                // labels; its comment has the measurements behind the cap.
                Layout.maximumWidth: Kirigami.Units.gridUnit * 26
                wrapMode: Text.WordWrap
                color: Kirigami.Theme.disabledTextColor
                font: Kirigami.Theme.smallFont
                text: i18n("Each song's effective offset is the global offset plus its own offset. The context menu only changes the song's own offset.")
            }
            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("Global offset:")
                visible: globalConfig.enabled
                from: -globalConfig.maximumOffsetMs
                to: globalConfig.maximumOffsetMs
                stepSize: 100
                value: globalConfig.offsetMs
                textFromValue: (value, locale) => i18n("%1 ms", value)
                onValueModified: globalConfig.offsetMs = value
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Current song:")
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 26
                wrapMode: Text.Wrap
                enabled: trackOffset.disabledReason !== "no-song"
                text: trackOffset.songText
            }
            RowLayout {
                Kirigami.FormData.label: i18n("This song's offset:")
                spacing: Kirigami.Units.smallSpacing
                QQC2.SpinBox {
                    // Both SpinBoxes share the store's range (decision 79).
                    from: -globalConfig.maximumOffsetMs
                    to: globalConfig.maximumOffsetMs
                    stepSize: 100
                    enabled: trackOffset.disabledReason === ""
                    value: trackOffset.value
                    textFromValue: (value, locale) => i18n("%1 ms", value)
                    onValueModified: trackOffset.edit(value)
                }
                QQC2.Label {
                    visible: text.length > 0
                    color: Kirigami.Theme.disabledTextColor
                    text: trackOffset.disabledReasonText
                }
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Effective offset:")
                visible: globalConfig.enabled
                // What the two SpinBoxes add up to, applied or not.
                text: page.unsavedChanges
                    ? i18n("%1 ms (after applying)", globalConfig.offsetMs + trackOffset.value)
                    : i18n("%1 ms", globalConfig.offsetMs + trackOffset.value)
            }
        }
    }
}
