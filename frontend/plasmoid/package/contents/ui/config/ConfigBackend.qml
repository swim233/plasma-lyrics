import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import io.github.swim233.lyrics

Kirigami.ScrollablePage {
    id: page

    BackendConfig { id: backend }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: true
            type: Kirigami.MessageType.Information
            text: i18n("These service settings affect every Desktop Lyrics widget. Restart plasma-lyricsd after saving.")
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            QQC2.TextField {
                Kirigami.FormData.label: i18n("NetEase API URL:")
                text: backend.neteaseBaseUrl
                onTextEdited: backend.neteaseBaseUrl = text
            }
            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("Network timeout:")
                from: 1000
                to: 30000
                stepSize: 500
                value: backend.networkTimeoutMs
                textFromValue: (value, locale) => i18n("%1 ms", value)
                onValueModified: backend.networkTimeoutMs = value
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Music detection:")
                text: i18n("Use metadata heuristic")
                checked: backend.metadataHeuristic
                onToggled: backend.metadataHeuristic = checked
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Lyrics cleanup:")
                text: i18n("Hide leading production credits")
                checked: backend.filterCredits
                onToggled: backend.filterCredits = checked
            }
            QQC2.TextArea {
                Kirigami.FormData.label: i18n("Music URL prefixes:")
                text: backend.musicUrlPrefixes
                placeholderText: i18n("One prefix per line")
                onTextChanged: backend.musicUrlPrefixes = text
            }
            QQC2.TextArea {
                Kirigami.FormData.label: i18n("Player blacklist:")
                text: backend.serviceBlacklist
                placeholderText: i18n("One D-Bus service wildcard per line")
                onTextChanged: backend.serviceBlacklist = text
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Diagnostics:")
                text: i18n("Also write a log file")
                checked: backend.fileLoggingEnabled
                onToggled: backend.fileLoggingEnabled = checked
            }
            QQC2.TextField {
                Kirigami.FormData.label: i18n("Log file:")
                visible: backend.fileLoggingEnabled
                text: backend.logFilePath
                onTextEdited: backend.logFilePath = text
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            QQC2.Button {
                text: i18n("Reload")
                icon.name: "view-refresh"
                onClicked: backend.load()
            }
            QQC2.Button {
                text: i18n("Save service settings")
                icon.name: "document-save"
                enabled: backend.dirty
                onClicked: backend.save()
            }
        }
    }
}
