import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A bare FormLayout rather than a Card wrapping one: this section now lives
// directly on its own config tab (DESIGN.md decision 40 split it out of the
// combined appearance page), and the tab name in the sidebar already carries
// the title a Card header would have repeated. Being a FormLayout itself
// (rather than containing one) also means the auto-hide section that follows
// it on the same page can list this as one of its own twinFormLayouts, so
// the two forms' label columns line up.
Kirigami.FormLayout {
    id: root

    property string plateMode: "ksvg"
    property string solidColor: "#99000000"
    property string textColor: "#fffaf5"
    property bool strokeEnabled: false
    property string strokeColor: "#cc000000"
    property int fontWeight: Font.Normal
    property string overflowMode: "fit"
    property string animationMode: "slide"
    required property var fontSizeControl
    required property var translationControl

    property bool showTrackInfo: true
    property string trackInfoLayout: "single"
    property int trackInfoFontWeight: Font.Normal
    property string trackInfoColor: "#b3fffaf5"
    property bool trackInfoStrokeEnabled: false
    property string trackInfoStrokeColor: "#cc000000"
    property string trackInfoOverflow: "fit"
    required property var trackInfoFontSizeControl

    signal plateModeEdited(string value)
    signal solidColorEdited(string value)
    signal textColorEdited(string value)
    signal strokeEnabledEdited(bool value)
    signal strokeColorEdited(string value)
    signal fontWeightEdited(int value)
    signal overflowModeEdited(string value)
    signal animationModeEdited(string value)

    signal showTrackInfoEdited(bool value)
    signal trackInfoLayoutEdited(string value)
    signal trackInfoFontWeightEdited(int value)
    signal trackInfoColorEdited(string value)
    signal trackInfoStrokeEnabledEdited(bool value)
    signal trackInfoStrokeColorEdited(string value)
    signal trackInfoOverflowEdited(string value)

    // Qt snaps a weight the family has no face for onto the nearest one it does
    // have, so offering all nine standard steps would mostly produce duplicates.
    // These six are the ones a typical family ships; the Plasma default here,
    // Noto Sans CJK SC, has a real face for every one of them except DemiBold.
    readonly property var fontWeightValues: [
        Font.Light, Font.Normal, Font.Medium,
        Font.DemiBold, Font.Bold, Font.Black
    ]

    QQC2.ComboBox {
        Kirigami.FormData.label: i18n("Background:")
        model: [i18n("None"), i18n("Plasma theme"), i18n("Solid translucent color")]
        currentIndex: ["none", "ksvg", "solid"].indexOf(root.plateMode)
        onActivated: root.plateModeEdited(["none", "ksvg", "solid"][currentIndex])
    }
    ColorField {
        Kirigami.FormData.label: i18n("Background color:")
        visible: root.plateMode === "solid"
        value: root.solidColor
        onEdited: hexColor => root.solidColorEdited(hexColor)
    }
    ColorField {
        Kirigami.FormData.label: i18n("Text color:")
        value: root.textColor
        onEdited: hexColor => root.textColorEdited(hexColor)
    }
    QQC2.SpinBox {
        Kirigami.FormData.label: i18n("Font size:")
        from: 10
        to: 96
        value: root.fontSizeControl.value
        onValueModified: root.fontSizeControl.value = value
    }
    QQC2.ComboBox {
        Kirigami.FormData.label: i18n("Font weight:")
        model: [
            i18nc("@item:inlistbox font weight", "Light"),
            i18nc("@item:inlistbox font weight", "Regular"),
            i18nc("@item:inlistbox font weight", "Medium"),
            i18nc("@item:inlistbox font weight", "Demi bold"),
            i18nc("@item:inlistbox font weight", "Bold"),
            i18nc("@item:inlistbox font weight", "Black")
        ]
        currentIndex: {
            const known = root.fontWeightValues.indexOf(root.fontWeight);
            // Fall back to Regular rather than to index 0, so an unknown
            // stored weight does not silently read as Light.
            return known >= 0 ? known : root.fontWeightValues.indexOf(Font.Normal);
        }
        onActivated: root.fontWeightEdited(root.fontWeightValues[currentIndex])
    }
    QQC2.CheckBox {
        Kirigami.FormData.label: i18n("Outline:")
        checked: root.strokeEnabled
        onToggled: root.strokeEnabledEdited(checked)
    }
    ColorField {
        Kirigami.FormData.label: i18n("Outline color:")
        visible: root.strokeEnabled
        value: root.strokeColor
        onEdited: hexColor => root.strokeColorEdited(hexColor)
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

    Kirigami.Separator {
        Kirigami.FormData.isSection: true
        Kirigami.FormData.label: i18n("Track info")
    }
    QQC2.CheckBox {
        Kirigami.FormData.label: i18n("Show track info:")
        checked: root.showTrackInfo
        onToggled: root.showTrackInfoEdited(checked)
    }
    QQC2.ComboBox {
        Kirigami.FormData.label: i18n("Layout:")
        visible: root.showTrackInfo
        model: [i18n("Single line"), i18n("Two lines")]
        currentIndex: ["single", "double"].indexOf(root.trackInfoLayout)
        onActivated: root.trackInfoLayoutEdited(["single", "double"][currentIndex])
    }
    QQC2.SpinBox {
        Kirigami.FormData.label: i18n("Font size:")
        visible: root.showTrackInfo
        from: 8
        to: 64
        value: root.trackInfoFontSizeControl.value
        onValueModified: root.trackInfoFontSizeControl.value = value
    }
    QQC2.ComboBox {
        Kirigami.FormData.label: i18n("Font weight:")
        visible: root.showTrackInfo
        model: [
            i18nc("@item:inlistbox font weight", "Light"),
            i18nc("@item:inlistbox font weight", "Regular"),
            i18nc("@item:inlistbox font weight", "Medium"),
            i18nc("@item:inlistbox font weight", "Demi bold"),
            i18nc("@item:inlistbox font weight", "Bold"),
            i18nc("@item:inlistbox font weight", "Black")
        ]
        currentIndex: {
            const known = root.fontWeightValues.indexOf(root.trackInfoFontWeight);
            return known >= 0 ? known : root.fontWeightValues.indexOf(Font.Normal);
        }
        onActivated: root.trackInfoFontWeightEdited(root.fontWeightValues[currentIndex])
    }
    ColorField {
        Kirigami.FormData.label: i18n("Text color:")
        visible: root.showTrackInfo
        value: root.trackInfoColor
        onEdited: hexColor => root.trackInfoColorEdited(hexColor)
    }
    QQC2.CheckBox {
        Kirigami.FormData.label: i18n("Outline:")
        visible: root.showTrackInfo
        checked: root.trackInfoStrokeEnabled
        onToggled: root.trackInfoStrokeEnabledEdited(checked)
    }
    ColorField {
        Kirigami.FormData.label: i18n("Outline color:")
        visible: root.showTrackInfo && root.trackInfoStrokeEnabled
        value: root.trackInfoStrokeColor
        onEdited: hexColor => root.trackInfoStrokeColorEdited(hexColor)
    }
    QQC2.ComboBox {
        Kirigami.FormData.label: i18n("Overflow:")
        visible: root.showTrackInfo
        model: [i18n("Shrink to fit"), i18n("Truncate")]
        currentIndex: ["fit", "elide"].indexOf(root.trackInfoOverflow)
        onActivated: root.trackInfoOverflowEdited(["fit", "elide"][currentIndex])
    }
}
