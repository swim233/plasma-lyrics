import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import io.github.swim233.lyrics

// DESIGN.md decision 41. The global offset lives in the LyricStore SQLite
// database rather than kcfg (unlike every other tab), so this page cannot
// use the cfg_ auto-binding the other tabs rely on -- it goes through KDE's
// official non-kcfg hooks instead (`AppletConfiguration.qml`'s Apply/OK path
// calls saveConfig() and gates the Apply button on unsavedChanges), the same
// pair the shell already offers third-party config pages for exactly this
// case.
Kirigami.ScrollablePage {
    id: page

    property bool unsavedChanges: globalConfig.unsavedChanges
    // save() can fail (disk full, permissions, a corrupt database), and the
    // Apply/OK hook has no channel back to the shell for that -- unlike
    // unsavedChanges staying true (a weak signal: the Apply button just
    // stays enabled), this is the explicit one the user actually notices.
    //
    // Only reliable on the Apply path, though. AppletConfiguration.qml's OK
    // button (:454-459, and Enter at :480) calls applyAction.trigger() and
    // then unconditionally closes the dialog; applyAction itself (:462-468)
    // unconditionally sets applyButton.enabled = false at :467 before this
    // page's saveConfig() return value could ever be seen, which means
    // closing()'s "don't close with unsaved changes" guard (:42-47) is
    // necessarily satisfied by the time the close runs -- there is no signal
    // this page could raise synchronously that would reach it first. Kept
    // anyway (DESIGN.md decision 41's accepted-residue note): it is free,
    // strictly better than nothing, and someone dialing in an offset by ear
    // mostly lives on the Apply path rather than OK/Enter.
    property bool saveFailed: false
    function saveConfig() { page.saveFailed = !globalConfig.save(); }

    GlobalConfig { id: globalConfig }

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

        Kirigami.FormLayout {
            Layout.fillWidth: true
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Lyric offset:")
                text: i18n("Use one offset for all songs")
                checked: globalConfig.enabled
                onToggled: globalConfig.enabled = checked
            }
            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("Offset:")
                visible: globalConfig.enabled
                from: -globalConfig.maximumOffsetMs
                to: globalConfig.maximumOffsetMs
                stepSize: 100
                value: globalConfig.offsetMs
                textFromValue: (value, locale) => i18n("%1 ms", value)
                onValueModified: globalConfig.offsetMs = value
            }
        }
    }
}
