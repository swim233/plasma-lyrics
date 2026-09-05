import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Split out of the old combined appearance page (DESIGN.md decision 40):
// these three keys are global text, not per-form-factor appearance, so they
// do not belong duplicated onto the desktop and panel tabs (same key edited
// in two places would look like two independent settings), and they are
// per-instance and take effect immediately, unlike the "Lyrics Service" tab
// which explicitly warns that its keys are global and need a daemon restart.
Kirigami.ScrollablePage {
    id: page

    property string cfg_idleText
    property bool cfg_idleTextUseDefault
    property string cfg_notFoundText

    Kirigami.FormLayout {
        width: parent.width
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
