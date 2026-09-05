import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Split out of the old combined appearance page (DESIGN.md decision 40):
// these three keys are shared between the two form factors rather than being
// per-form-factor appearance, so they do not belong duplicated onto the
// desktop and panel tabs (same key edited in two places would look like two
// independent settings). "Shared" there means shared between this widget's
// desktop and panel halves and nothing more -- like every other key on this
// dialog except the "Lyrics Service" tab, they are per widget instance, which
// is what the banner below spells out.
Kirigami.ScrollablePage {
    id: page

    property string cfg_idleText
    property bool cfg_idleTextUseDefault
    property string cfg_notFoundText

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // DESIGN.md decision 18: the frontend keys are per-instance, and
        // nothing in the dialog said so. Two widgets out at once is a
        // supported arrangement (DESIGN.md section 2.1), and the one tab that
        // does reach every widget -- "Lyrics Service" -- says as much in its
        // own banner, which made the silence here read as "shared".
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: true
            type: Kirigami.MessageType.Information
            text: i18n("These settings apply to this widget only. Every Desktop Lyrics widget keeps its own copy.")
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            QQC2.TextField {
                Kirigami.FormData.label: i18n("Not playing text:")
                text: page.cfg_idleText
                placeholderText: i18n("No media is playing")
                onTextEdited: {
                    page.cfg_idleText = text;
                    page.cfg_idleTextUseDefault = false;
                }
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Empty text:")
                text: i18n("Use the localized default message")
                checked: page.cfg_idleTextUseDefault
                onToggled: page.cfg_idleTextUseDefault = checked
            }
            QQC2.TextField {
                Kirigami.FormData.label: i18n("Lyrics not found text:")
                text: page.cfg_notFoundText
                placeholderText: i18n("Leave empty to hide")
                onTextChanged: page.cfg_notFoundText = text
            }
        }
    }
}
