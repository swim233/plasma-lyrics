import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Card {
    id: root

    required property string title
    property string plateMode: "ksvg"
    property string solidColor: "#99000000"
    property string textColor: "#fffaf5"
    property bool strokeEnabled: false
    property string strokeColor: "#cc000000"
    property string overflowMode: "fit"
    property string animationMode: "slide"
    required property var fontSizeControl
    required property var translationControl

    signal plateModeEdited(string value)
    signal solidColorEdited(string value)
    signal textColorEdited(string value)
    signal strokeEnabledEdited(bool value)
    signal strokeColorEdited(string value)
    signal overflowModeEdited(string value)
    signal animationModeEdited(string value)

    header: Kirigami.Heading { text: root.title; level: 3 }
    contentItem: Kirigami.FormLayout {
        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Background:")
            model: [i18n("None"), i18n("Plasma theme"), i18n("Solid translucent color")]
            currentIndex: ["none", "ksvg", "solid"].indexOf(root.plateMode)
            onActivated: root.plateModeEdited(["none", "ksvg", "solid"][currentIndex])
        }
        QQC2.TextField {
            Kirigami.FormData.label: i18n("Background color:")
            visible: root.plateMode === "solid"
            text: root.solidColor
            onEditingFinished: root.solidColorEdited(text)
        }
        QQC2.TextField {
            Kirigami.FormData.label: i18n("Text color:")
            text: root.textColor
            onEditingFinished: root.textColorEdited(text)
        }
        QQC2.SpinBox {
            Kirigami.FormData.label: i18n("Font size:")
            from: 10
            to: 96
            value: root.fontSizeControl.value
            onValueModified: root.fontSizeControl.value = value
        }
        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Outline:")
            checked: root.strokeEnabled
            onToggled: root.strokeEnabledEdited(checked)
        }
        QQC2.TextField {
            Kirigami.FormData.label: i18n("Outline color:")
            visible: root.strokeEnabled
            text: root.strokeColor
            onEditingFinished: root.strokeColorEdited(text)
        }
        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Translation:")
            checked: root.translationControl.checked
            onToggled: root.translationControl.checked = checked
        }
        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Long lyrics:")
            model: [i18n("Fit text"), i18n("Wrap to two lines"), i18n("Marquee")]
            currentIndex: ["fit", "wrap", "marquee"].indexOf(root.overflowMode)
            onActivated: root.overflowModeEdited(["fit", "wrap", "marquee"][currentIndex])
        }
        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Line transition:")
            model: [i18n("None"), i18n("Fade"), i18n("Slide up")]
            currentIndex: ["none", "fade", "slide"].indexOf(root.animationMode)
            onActivated: root.animationModeEdited(["none", "fade", "slide"][currentIndex])
        }
    }
}

