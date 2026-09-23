import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "../FontPolicy.js" as FontPolicy

// The track-info rows of an appearance page that both of its sets share
// (DESIGN.md decision 75), a FormLayout of their own rather than the tail of
// AppearanceSection's because they sit below the frame that holds the set
// being edited. The three colour rows are AppearanceSection's, inside that
// frame. The page lists AppearanceSection in this form's twinFormLayouts, so
// the label columns still line up.
Kirigami.FormLayout {
    id: root

    // The FontCatalog singleton, or a stand-in, as for AppearanceSection.
    required property var fontCatalog
    property bool showTrackInfo: true
    property string trackInfoLayout: "single"
    property bool trackInfoFontSameAsLyrics: true
    property string trackInfoFontFamily: ""
    property int trackInfoFontWeight: Font.Normal
    property string trackInfoOverflow: "fit"
    required property var trackInfoFontSizeControl

    signal showTrackInfoEdited(bool value)
    signal trackInfoLayoutEdited(string value)
    signal trackInfoFontSameAsLyricsEdited(bool value)
    signal trackInfoFontFamilyEdited(string value)
    signal trackInfoFontWeightEdited(int value)
    signal trackInfoOverflowEdited(string value)

    // The families the lyrics and the track info render in, computed by the
    // page for the reason AppearanceSection's lyricEffectiveFamily gives.
    required property string lyricEffectiveFamily
    required property string trackInfoEffectiveFamily
    readonly property string systemFamily: Kirigami.Theme.defaultFont.family

    // A pick that moves the track info onto another family also writes its
    // weight, snapped onto one of the new family's real faces, as
    // AppearanceSection's editLyricFamily does for the lyrics. "Same as
    // lyrics" leaves trackInfoFontFamily as it is, so turning it back off
    // restores the family chosen before.
    function editTrackInfoFont(sameAsLyrics, stored) {
        const before = root.trackInfoEffectiveFamily;
        const after = FontPolicy.trackInfoFamily(root.fontCatalog, sameAsLyrics, stored,
            root.lyricEffectiveFamily, root.systemFamily);
        const trackInfoWeight = root.trackInfoFontWeight;
        root.trackInfoFontSameAsLyricsEdited(sameAsLyrics);
        if (!sameAsLyrics) {
            root.trackInfoFontFamilyEdited(stored);
        }
        if (after !== before) {
            root.trackInfoFontWeightEdited(FontPolicy.renderWeight(root.fontCatalog, after, trackInfoWeight));
        }
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
    FontPicker {
        objectName: "trackInfoFontPicker"
        Kirigami.FormData.label: i18n("Font:")
        visible: root.showTrackInfo
        // The same cap as AppearanceSection's lyricFontPicker.
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 20
        fontCatalog: root.fontCatalog
        storedFamily: root.trackInfoFontFamily
        offerSameAsLyrics: true
        sameAsLyrics: root.trackInfoFontSameAsLyrics
        lyricFamily: root.lyricEffectiveFamily
        onSameAsLyricsPicked: root.editTrackInfoFont(true, root.trackInfoFontFamily)
        onFollowSystemPicked: root.editTrackInfoFont(false, "")
        onFamilyPicked: family => root.editTrackInfoFont(false, family)
    }
    QQC2.SpinBox {
        Kirigami.FormData.label: i18n("Font size:")
        visible: root.showTrackInfo
        from: 8
        to: 64
        value: root.trackInfoFontSizeControl.value
        onValueModified: root.trackInfoFontSizeControl.value = value
    }
    WeightComboBox {
        objectName: "trackInfoWeightComboBox"
        Kirigami.FormData.label: i18n("Font weight:")
        visible: root.showTrackInfo
        fontCatalog: root.fontCatalog
        family: root.trackInfoEffectiveFamily
        storedWeight: root.trackInfoFontWeight
        onWeightPicked: weight => root.trackInfoFontWeightEdited(weight)
    }
    QQC2.ComboBox {
        Kirigami.FormData.label: i18n("Overflow:")
        visible: root.showTrackInfo
        model: [i18n("Shrink to fit"), i18n("Truncate")]
        currentIndex: ["fit", "elide"].indexOf(root.trackInfoOverflow)
        onActivated: root.trackInfoOverflowEdited(["fit", "elide"][currentIndex])
    }
}
