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
    property string secondLineSource: "translation"
    property bool secondLineColorEnabled: false
    property string secondLineColor: "#adfffaf5"
    property int lineHeightPercent: 125
    required property var fontSizeControl
    // Still the second line's on/off switch; the combo below turns it and
    // secondLineSource into the one three-way choice the user sees.
    required property var translationControl

    // The panel has no lift keys at all, so the row below stands there
    // disabled rather than pretending to store anything.
    property bool liftSupported: true
    property bool wordByWord: true
    // Computed by the page rather than as a conjunction here. Written inline
    // on this row's `visible`, any two-property conjunction sends Kirigami's
    // binding-loop detector into a loop -- reproduced by bisection: `true`,
    // `wordByWord` alone and `wordLift` alone are all silent, the conjunction
    // is not, and it is specific to this row (the identically shaped
    // brightness row below is fine). A single property read is silent.
    property bool wordLiftRowVisible: true
    property string wordUnsungColor: "#8cfffaf5"
    property string wordActiveColor: "#e6fffaf5"
    property string wordSungColor: "#c4fffaf5"
    property bool wordLift: true
    property int wordLiftPercent: 14
    property bool wordBrightness: true
    property int wordBrightnessPercent: 60
    property bool wordBlurGlow: false

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
    signal secondLineSourceEdited(string value)
    signal secondLineColorEnabledEdited(bool value)
    signal secondLineColorEdited(string value)
    signal lineHeightPercentEdited(int value)

    signal wordByWordEdited(bool value)
    signal wordUnsungColorEdited(string value)
    signal wordActiveColorEdited(string value)
    signal wordSungColorEdited(string value)
    signal wordLiftEdited(bool value)
    signal wordLiftPercentEdited(int value)
    signal wordBrightnessEdited(bool value)
    signal wordBrightnessPercentEdited(int value)
    signal wordBlurGlowEdited(bool value)

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
    QQC2.SpinBox {
        Kirigami.FormData.label: i18n("Line height:")
        from: 100
        to: 200
        stepSize: 5
        value: root.lineHeightPercent
        onValueModified: root.lineHeightPercentEdited(value)
        textFromValue: (value, locale) => i18nc("@item:valuesuffix line height as a percentage of the font size", "%1%", value)
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
    QQC2.ComboBox {
        Kirigami.FormData.label: i18n("Second line:")
        model: [i18n("Translation"), i18n("Romanization"), i18n("None")]
        currentIndex: !root.translationControl.checked
            ? 2
            : (root.secondLineSource === "romanization" ? 1 : 0)
        onActivated: {
            root.translationControl.checked = currentIndex !== 2;
            if (currentIndex !== 2) {
                root.secondLineSourceEdited(currentIndex === 1 ? "romanization" : "translation");
            }
        }
    }
    QQC2.CheckBox {
        Kirigami.FormData.label: i18n("Second line color:")
        visible: root.translationControl.checked
        text: i18n("Set it separately from the lyric color")
        checked: root.secondLineColorEnabled
        onToggled: root.secondLineColorEnabledEdited(checked)
    }
    ColorField {
        Kirigami.FormData.label: i18n("Color:")
        visible: root.translationControl.checked && root.secondLineColorEnabled
        value: root.secondLineColor
        onEdited: hexColor => root.secondLineColorEdited(hexColor)
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
        Kirigami.FormData.label: i18n("Word-by-word")
    }
    QQC2.CheckBox {
        Kirigami.FormData.label: i18n("Word-by-word:")
        text: i18n("Highlight each word as it is sung")
        checked: root.wordByWord
        onToggled: root.wordByWordEdited(checked)
    }
    QQC2.Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: i18n("Used only when the lyrics source provides word timings. Other tracks keep showing one line at a time.")
    }
    ColorField {
        visible: root.wordByWord
        Kirigami.FormData.label: i18n("Upcoming words:")
        value: root.wordUnsungColor
        onEdited: hexColor => root.wordUnsungColorEdited(hexColor)
    }
    ColorField {
        visible: root.wordByWord
        Kirigami.FormData.label: i18n("Current word:")
        value: root.wordActiveColor
        onEdited: hexColor => root.wordActiveColorEdited(hexColor)
    }
    ColorField {
        visible: root.wordByWord
        Kirigami.FormData.label: i18n("Sung words:")
        value: root.wordSungColor
        onEdited: hexColor => root.wordSungColorEdited(hexColor)
    }
    QQC2.CheckBox {
        Kirigami.FormData.label: i18n("Lift:")
        visible: root.wordByWord
        enabled: root.liftSupported
        checked: root.liftSupported && root.wordLift
        text: root.liftSupported
            ? i18n("Raise the word being sung")
            : i18n("Not available in a panel, which sets the widget height")
        onToggled: root.wordLiftEdited(checked)
    }
    QQC2.SpinBox {
        Kirigami.FormData.label: i18n("Lift height:")
        // What makes this row loop, measured on this tree (suite-wide count of
        // Kirigami "Binding loop" warnings):
        //   root.wordLiftRowVisible                              -> 0  (current)
        //   root.wordLiftRowVisible && root.liftSupported        -> 0
        //   root.wordByWord && root.liftSupported && root.wordLift -> 4
        //   root.wordByWord && root.wordLift                     -> 4
        //   root.liftSupported / root.wordByWord / root.wordLift alone -> 0
        // What fixes it is the hoisting: the computation lives in the config
        // pages and this row reads one plain pass-through property.
        //
        // Do not derive a clause-level rule from that table. The identical
        // expression `wordByWord && wordLift` measured 0 loops on an earlier
        // tree, where the panel page left wordLift unbound at its default
        // true, and measures 4 here, where the panel assigns it false. Same
        // text, opposite result, because the binding graph around it changed --
        // so both measurements are accurate reports of their own tree and
        // neither generalises. On that earlier tree adding `&& liftSupported`
        // was the single clause that flipped 0 to looping; here adding or
        // removing it changes nothing. Three more points that defeat any
        // single-clause rule: wordByWord && wordLift loops;
        // wordLiftRowVisible && liftSupported does not; and the brightness row
        // two entries below has always read wordByWord && wordBrightness
        // without looping.
        //
        // The finding is that the trigger is graph-wide rather than clausal.
        // That is why three careful measurements produced three stories, and
        // why the next person should re-measure rather than reason from this
        // file if the expression or its surroundings change.
        //
        // Why Kirigami loops on this row and not on the identically shaped
        // brightness row below is not established.
        visible: root.wordLiftRowVisible
        from: 2
        to: 40
        value: root.wordLiftPercent
        onValueModified: root.wordLiftPercentEdited(value)
        textFromValue: (value, locale) => i18nc("@item:valuesuffix lift height as a percentage of the font size", "%1%", value)
    }
    QQC2.CheckBox {
        Kirigami.FormData.label: i18n("Brightening:")
        visible: root.wordByWord
        text: i18n("Brighten the word being sung")
        checked: root.wordBrightness
        onToggled: root.wordBrightnessEdited(checked)
    }
    QQC2.SpinBox {
        Kirigami.FormData.label: i18n("Brightness:")
        visible: root.wordByWord && root.wordBrightness
        from: 10
        to: 100
        stepSize: 5
        value: root.wordBrightnessPercent
        onValueModified: root.wordBrightnessPercentEdited(value)
        textFromValue: (value, locale) => i18nc("@item:valuesuffix how far the sung word is brightened", "%1%", value)
    }
    QQC2.CheckBox {
        Kirigami.FormData.label: i18n("Blurred glow:")
        visible: root.wordByWord
        text: i18n("Add a blurred halo. Costs noticeably more to draw than brightening alone.")
        checked: root.wordBlurGlow
        onToggled: root.wordBlurGlowEdited(checked)
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
