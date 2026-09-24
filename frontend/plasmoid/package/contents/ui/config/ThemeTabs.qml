import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "../ThemePolicy.js" as ThemePolicy

// DESIGN.md decision 76: the theme mode row of an appearance page and, below
// it, a [Light | Dark] tab bar standing on a frame, so the two read as one
// tabbed control. The frame holds the rows that come in a light and a dark
// copy -- the page's AppearanceSection, declared as this component's content
// -- and the page binds those rows to the copy `editingDark` names. Both
// appearance pages use this as it is; nothing in it depends on the form
// factor except the number of keys a sync copies.
ColumnLayout {
    id: root

    // "desktop" or "panel": the key prefix, not Plasmoid.formFactor.
    required property string formFactor
    // <form>ThemeMode as the dialog holds it, saved or not: the tab
    // labels and the hint follow a change right away.
    property string mode: "auto"
    // Whether the Plasma style is dark; the page reads it (decision 76).
    required property bool styleDark
    // The page's FormLayouts the mode row lines its label up with, and
    // whether they are laid out in two columns: the page passes the form
    // inside the frame's, so that every form on it switches at one width.
    property list<Item> twinFormLayouts
    property bool wideMode: true

    signal modeEdited(string value)
    // The user confirmed replacing the set on screen with the other one;
    // `fromDark` is the set to copy from.
    signal syncConfirmed(bool fromDark)

    default property alias content: contentColumn.data
    // A popup is no child item, so the tests reach it through this.
    readonly property alias syncDialog: syncDialog

    readonly property bool darkInEffect: ThemePolicy.isDark(root.styleDark, root.mode)
    readonly property bool editingDark: tabBar.currentIndex === 1
    readonly property string syncTitle: root.editingDark
        ? i18nc("@action:button", "Sync from light theme")
        : i18nc("@action:button", "Sync from dark theme")

    spacing: Kirigami.Units.largeSpacing

    Kirigami.FormLayout {
        Layout.fillWidth: true
        twinFormLayouts: root.twinFormLayouts
        wideMode: root.wideMode

        QQC2.ComboBox {
            objectName: "themeModeComboBox"
            Kirigami.FormData.label: i18n("Theme mode:")
            model: [
                i18nc("@item:inlistbox theme mode", "Follow system"),
                i18nc("@item:inlistbox theme mode", "Always light"),
                i18nc("@item:inlistbox theme mode", "Always dark")
            ]
            currentIndex: ["auto", "light", "dark"].indexOf(root.mode)
            onActivated: index => root.modeEdited(["auto", "light", "dark"][index])
        }
        QQC2.Label {
            // Styled, capped and named as AppearanceSection's formDescription
            // labels are; its comment has the measurements behind the cap.
            // The cap alone is not enough here: the 24 gridUnits sit below
            // what the controls need in English but above it in Chinese, and
            // this sentence in Chinese is wider than the controls, which
            // widened every form on the page by 35 px (489 -> 524, measured).
            // A preferred width of 1 -- FormLayout ignores 0 -- leaves the
            // column width to the controls; fillWidth still lets the text
            // take all of it, up to the cap.
            objectName: "formDescription"
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.maximumWidth: Kirigami.Units.gridUnit * 24
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.smallFont
            text: i18n("When set to follow the system, allows plasma-lyrics to follow changes to the system theme.")
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0

        QQC2.TabBar {
            id: tabBar
            objectName: "themeTabBar"
            Layout.fillWidth: true

            // The set in effect when the dialog opens: the dialog creates
            // the page with every cfg_ value as an initial property, so the
            // mode is in place by now. Chosen once; after that only the user
            // switches tabs, and changing the mode leaves the rows being
            // edited where they are.
            Component.onCompleted: tabBar.currentIndex = root.darkInEffect ? 1 : 0

            QQC2.TabButton {
                text: root.darkInEffect
                    ? i18nc("@title:tab light Plasma style", "Light")
                    : i18nc("@title:tab light Plasma style, the set in effect", "Light (current)")
            }
            QQC2.TabButton {
                text: root.darkInEffect
                    ? i18nc("@title:tab dark Plasma style, the set in effect", "Dark (current)")
                    : i18nc("@title:tab dark Plasma style", "Dark")
            }
        }

        QQC2.Frame {
            Layout.fillWidth: true

            // As the content item, so the frame sizes it to its own width;
            // a plain child keeps its implicit width, and the forms inside
            // would not line up with the ones outside.
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                // Only on the tab whose set is not in effect, saying why:
                // the mode is fixed to the other set, or which set the
                // system theme puts in effect.
                Kirigami.InlineMessage {
                    objectName: "themeHint"
                    Layout.fillWidth: true
                    visible: root.editingDark !== root.darkInEffect
                    type: Kirigami.MessageType.Information
                    text: {
                        if (root.mode === "light") {
                            return i18n("The theme mode is “Always light”, so these settings are not used for now.");
                        }
                        if (root.mode === "dark") {
                            return i18n("The theme mode is “Always dark”, so these settings are not used for now.");
                        }
                        return root.darkInEffect
                            ? i18n("The system theme is currently dark, and the matching colors are in effect.")
                            : i18n("The system theme is currently light, and the matching colors are in effect.");
                    }
                }

                ColumnLayout {
                    id: contentColumn
                    Layout.fillWidth: true
                    spacing: 0
                }

                QQC2.Button {
                    objectName: "themeSyncButton"
                    Layout.alignment: Qt.AlignRight
                    icon.name: "edit-copy"
                    text: root.syncTitle
                    onClicked: syncDialog.open()
                }
            }
        }
    }

    // Copies into the values the dialog holds, not into the configuration:
    // Apply or OK saves them as any other edit, Cancel drops them.
    Kirigami.PromptDialog {
        id: syncDialog
        objectName: "themeSyncDialog"
        title: root.syncTitle
        subtitle: root.editingDark
            ? i18np("This replaces the %1 setting of the dark theme with that of the light theme. Nothing is saved until you click Apply or OK.",
                "This replaces the %1 settings of the dark theme with those of the light theme. Nothing is saved until you click Apply or OK.",
                ThemePolicy.themedSuffixes(root.formFactor).length)
            : i18np("This replaces the %1 setting of the light theme with that of the dark theme. Nothing is saved until you click Apply or OK.",
                "This replaces the %1 settings of the light theme with those of the dark theme. Nothing is saved until you click Apply or OK.",
                ThemePolicy.themedSuffixes(root.formFactor).length)
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Sync")
                icon.name: "edit-copy"
                onTriggered: {
                    root.syncConfirmed(!root.editingDark);
                    syncDialog.close();
                }
            },
            Kirigami.Action {
                text: i18nc("@action:button", "Cancel")
                icon.name: "dialog-cancel"
                onTriggered: syncDialog.close()
            }
        ]
    }
}
