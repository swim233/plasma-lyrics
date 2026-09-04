import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: page

    property string cfg_idleText
    property bool cfg_idleTextUseDefault
    property string cfg_notFoundText

    property string cfg_desktopPlateMode
    property string cfg_desktopSolidColor
    property string cfg_desktopTextColor
    property bool cfg_desktopStroke
    property string cfg_desktopStrokeColor
    property alias cfg_desktopFontSize: desktopFontSize.value
    property string cfg_desktopOverflow
    property string cfg_desktopAnimation
    property alias cfg_desktopShowTranslation: desktopTranslation.checked

    property string cfg_panelPlateMode
    property string cfg_panelSolidColor
    property string cfg_panelTextColor
    property bool cfg_panelStroke
    property string cfg_panelStrokeColor
    property alias cfg_panelFontSize: panelFontSize.value
    property string cfg_panelOverflow
    property string cfg_panelAnimation
    property alias cfg_panelShowTranslation: panelTranslation.checked

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

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

        AppearanceSection {
            Layout.fillWidth: true
            title: i18n("Desktop appearance")
            plateMode: page.cfg_desktopPlateMode
            solidColor: page.cfg_desktopSolidColor
            textColor: page.cfg_desktopTextColor
            strokeEnabled: page.cfg_desktopStroke
            strokeColor: page.cfg_desktopStrokeColor
            overflowMode: page.cfg_desktopOverflow
            animationMode: page.cfg_desktopAnimation
            fontSizeControl: desktopFontSize
            translationControl: desktopTranslation
            onPlateModeEdited: value => page.cfg_desktopPlateMode = value
            onSolidColorEdited: value => page.cfg_desktopSolidColor = value
            onTextColorEdited: value => page.cfg_desktopTextColor = value
            onStrokeEnabledEdited: value => page.cfg_desktopStroke = value
            onStrokeColorEdited: value => page.cfg_desktopStrokeColor = value
            onOverflowModeEdited: value => page.cfg_desktopOverflow = value
            onAnimationModeEdited: value => page.cfg_desktopAnimation = value
        }

        QQC2.SpinBox { id: desktopFontSize; visible: false }
        QQC2.CheckBox { id: desktopTranslation; visible: false }

        AppearanceSection {
            Layout.fillWidth: true
            title: i18n("Panel appearance")
            plateMode: page.cfg_panelPlateMode
            solidColor: page.cfg_panelSolidColor
            textColor: page.cfg_panelTextColor
            strokeEnabled: page.cfg_panelStroke
            strokeColor: page.cfg_panelStrokeColor
            overflowMode: page.cfg_panelOverflow
            animationMode: page.cfg_panelAnimation
            fontSizeControl: panelFontSize
            translationControl: panelTranslation
            onPlateModeEdited: value => page.cfg_panelPlateMode = value
            onSolidColorEdited: value => page.cfg_panelSolidColor = value
            onTextColorEdited: value => page.cfg_panelTextColor = value
            onStrokeEnabledEdited: value => page.cfg_panelStroke = value
            onStrokeColorEdited: value => page.cfg_panelStrokeColor = value
            onOverflowModeEdited: value => page.cfg_panelOverflow = value
            onAnimationModeEdited: value => page.cfg_panelAnimation = value
        }

        QQC2.SpinBox { id: panelFontSize; visible: false }
        QQC2.CheckBox { id: panelTranslation; visible: false }
    }
}
