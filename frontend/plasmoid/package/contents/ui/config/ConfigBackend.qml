import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import io.github.swim233.lyrics

Kirigami.ScrollablePage {
    id: page

    property bool unsavedChanges: backend.dirty
    property bool saveFailed: false
    function saveConfig() { page.saveFailed = !backend.save(); }
    function providerName(provider) {
        if (provider === "local") return i18n("Local files");
        if (provider === "netease") return i18n("NetEase");
        if (provider === "amll") return i18n("AMLL");
        return provider;
    }

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

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: backend.providerDiscoveryFallback
            type: Kirigami.MessageType.Warning
            text: i18n("The lyrics service is not running. Showing the built-in source list; unavailable sources will be preserved when saving.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.saveFailed
            type: Kirigami.MessageType.Error
            text: i18n("Could not save the lyrics service settings.")
        }

        RestartFeedback {
            objectName: "restartFeedback"
            Layout.fillWidth: true
            succeeded: backend.restartState === BackendConfig.RestartSucceeded
            failed: backend.restartState === BackendConfig.RestartFailed
            errorText: backend.restartError
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            Item {
                Kirigami.FormData.label: i18n("Lyrics source priority:")
                implicitWidth: Kirigami.Units.gridUnit * 18
                implicitHeight: sourceList.contentHeight

                ListView {
                    id: sourceList
                    anchors.fill: parent
                    interactive: false
                    spacing: Kirigami.Units.smallSpacing
                    model: backend.providerEntries

                    delegate: Item {
                        id: sourceDelegate
                        required property int index
                        required property var modelData
                        width: sourceList.width
                        height: Kirigami.Units.gridUnit * 2

                        Rectangle {
                            id: sourceRow
                            width: parent.width
                            height: parent.height
                            radius: Kirigami.Units.smallSpacing
                            color: dragArea.drag.active
                                ? Kirigami.Theme.highlightColor
                                : Kirigami.Theme.backgroundColor
                            border.color: Kirigami.Theme.disabledTextColor

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Kirigami.Units.smallSpacing
                                anchors.rightMargin: Kirigami.Units.smallSpacing
                                QQC2.Label {
                                    text: "☰"
                                    Accessible.name: i18n("Drag to reorder")
                                }
                                QQC2.CheckBox {
                                    Layout.fillWidth: true
                                    text: page.providerName(sourceDelegate.modelData.id)
                                    checked: sourceDelegate.modelData.enabled
                                    onToggled: {
                                        if (!backend.setProviderEnabled(sourceDelegate.modelData.id,
                                                                        checked)) {
                                            checked = Qt.binding(function() {
                                                return sourceDelegate.modelData.enabled;
                                            });
                                        }
                                    }
                                }
                                QQC2.ToolButton {
                                    icon.name: "go-up"
                                    enabled: sourceDelegate.index > 0
                                    Accessible.name: i18n("Move source up")
                                    onClicked: backend.moveProvider(sourceDelegate.index,
                                                                    sourceDelegate.index - 1)
                                }
                                QQC2.ToolButton {
                                    icon.name: "go-down"
                                    enabled: sourceDelegate.index + 1 < sourceList.count
                                    Accessible.name: i18n("Move source down")
                                    onClicked: backend.moveProvider(sourceDelegate.index,
                                                                    sourceDelegate.index + 1)
                                }
                            }

                            MouseArea {
                                id: dragArea
                                anchors.left: parent.left
                                width: Kirigami.Units.gridUnit * 2
                                height: parent.height
                                cursorShape: Qt.SizeVerCursor
                                drag.target: sourceRow
                                drag.axis: Drag.YAxis
                                drag.minimumY: -sourceDelegate.index * sourceDelegate.height
                                drag.maximumY: (sourceList.count - sourceDelegate.index - 1)
                                               * sourceDelegate.height
                                onReleased: {
                                    const target = Math.max(0, Math.min(sourceList.count - 1,
                                        sourceDelegate.index
                                        + Math.round(sourceRow.y / sourceDelegate.height)));
                                    sourceRow.y = 0;
                                    backend.moveProvider(sourceDelegate.index, target);
                                }
                            }
                        }
                    }
                }
            }
            QQC2.TextField {
                Kirigami.FormData.label: i18n("Local lyrics directory:")
                text: backend.localLyricsDirectory
                onTextEdited: backend.localLyricsDirectory = text
            }
            QQC2.TextField {
                Kirigami.FormData.label: i18n("NetEase API URL:")
                text: backend.neteaseBaseUrl
                onTextEdited: backend.neteaseBaseUrl = text
            }
            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("Initial network timeout:")
                from: 1000
                to: 30000
                stepSize: 500
                value: backend.networkTimeoutMs
                textFromValue: (value, locale) => i18n("%1 ms", value)
                onValueModified: backend.networkTimeoutMs = value
            }
            QQC2.TextField {
                Kirigami.FormData.label: i18n("AMLL index URL:")
                text: backend.amllIndexUrl
                onTextEdited: backend.amllIndexUrl = text
            }
            QQC2.TextField {
                Kirigami.FormData.label: i18n("AMLL content base URL:")
                text: backend.amllContentBaseUrl
                onTextEdited: backend.amllContentBaseUrl = text
            }
            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("AMLL network timeout:")
                from: 1000
                to: 60000
                stepSize: 500
                value: backend.amllTimeoutMs
                textFromValue: (value, locale) => i18n("%1 ms", value)
                onValueModified: backend.amllTimeoutMs = value
            }
            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("AMLL index refresh interval:")
                from: 1
                to: 168
                value: backend.amllIndexRefreshHours
                textFromValue: (value, locale) => i18np("%1 hour", "%1 hours", value)
                onValueModified: backend.amllIndexRefreshHours = value
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
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Enable lyrics for these platforms:")
                text: i18n("NetEase Cloud Music (music.163.com)")
                checked: backend.platformNetease
                onToggled: backend.platformNetease = checked
            }
            QQC2.CheckBox {
                text: i18n("Apple Music (music.apple.com, Cider, Sidra)")
                checked: backend.platformApple
                onToggled: backend.platformApple = checked
            }
            QQC2.TextArea {
                Kirigami.FormData.label: i18n("Custom URL prefixes:")
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
            QQC2.CheckBox {
                text: i18n("Record debug details")
                checked: backend.debugLoggingEnabled
                onToggled: backend.debugLoggingEnabled = checked
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 20
                text: i18n("Adds request URLs, match scoring and player events to the log. Takes effect after the service restarts.")
                wrapMode: Text.WordWrap
                color: Kirigami.Theme.disabledTextColor
                font: Kirigami.Theme.smallFont
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
                onClicked: {
                    page.saveFailed = false;
                    backend.load();
                }
            }
            QQC2.Button {
                text: i18n("Save service settings")
                icon.name: "document-save"
                enabled: backend.dirty
                onClicked: page.saveConfig()
            }
            QQC2.Button {
                objectName: "restartServiceButton"
                text: i18n("Save and restart service")
                icon.name: "system-reboot"
                enabled: !backend.restartInProgress
                onClicked: {
                    if (backend.dirty) page.saveConfig();
                    if (!page.saveFailed) backend.restartService();
                }
            }
        }
    }
}
