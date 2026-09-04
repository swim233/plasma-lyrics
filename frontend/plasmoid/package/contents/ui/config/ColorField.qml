import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kquickcontrols as KQuickControls

// A Plasma colour swatch paired with the hex field this widget has always had.
// Both edit the same "#RRGGBB" / "#AARRGGBB" string: QML's colour toString()
// keeps the alpha byte, so the picker round-trips the translucent defaults
// (#99000000, #cc000000) without dropping transparency.
RowLayout {
    id: root

    property string value: "#ffffffff"
    signal edited(string hexColor)

    spacing: Kirigami.Units.smallSpacing

    // Neither child binds to `value` declaratively. Accepting the colour dialog
    // assigns ColorButton.selectedColor and typing assigns TextField.text, and
    // either write destroys a binding on that property for good -- after one
    // edit the control would quietly stop tracking the configuration. Pushing
    // the value in on every change keeps the two in step with each other and
    // with the config, including when neither was touched (the config dialog's
    // Defaults button, or the other control in this row).
    function sync() {
        if (swatch.color.toString() !== root.value) {
            swatch.color = root.value;
        }
        if (hexField.text !== root.value) {
            hexField.text = root.value;
        }
    }

    onValueChanged: root.sync()
    Component.onCompleted: root.sync()

    KQuickControls.ColorButton {
        id: swatch
        showAlphaChannel: true
        onAccepted: root.edited(swatch.color.toString())
    }

    QQC2.TextField {
        id: hexField
        Layout.fillWidth: true
        // editingFinished only fires on acceptable input, so the validator is
        // what stops an unparseable string reaching the config and rendering
        // the lyrics black-on-black.
        validator: RegularExpressionValidator {
            regularExpression: /#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})/
        }
        onEditingFinished: root.edited(hexField.text)
    }
}
