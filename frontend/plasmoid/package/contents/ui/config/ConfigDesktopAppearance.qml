import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import io.github.swim233.lyrics

import "../FontPolicy.js" as FontPolicy

Kirigami.ScrollablePage {
    id: page

    property string cfg_desktopPlateMode
    property string cfg_desktopSolidColor
    property string cfg_desktopTextColor
    property bool cfg_desktopStroke
    property string cfg_desktopStrokeColor
    property string cfg_desktopFontFamily
    property alias cfg_desktopFontSize: desktopFontSize.value
    property int cfg_desktopFontWeight
    property string cfg_desktopOverflow
    property string cfg_desktopAnimation
    property alias cfg_desktopShowTranslation: desktopTranslation.checked
    property string cfg_desktopSecondLineSource
    property bool cfg_desktopSecondLineColorEnabled
    property string cfg_desktopSecondLineColor
    property int cfg_desktopLineHeight

    property bool cfg_desktopWordByWord
    property bool cfg_desktopWordByWordSynthetic
    property string cfg_desktopWordUnsungColor
    property string cfg_desktopWordActiveColor
    property string cfg_desktopWordSungColor
    property bool cfg_desktopWordLift
    property int cfg_desktopWordLiftPercent
    property bool cfg_desktopWordBrightness
    property int cfg_desktopWordBrightnessPercent
    property bool cfg_desktopWordBlurGlow

    property bool cfg_desktopShowTrackInfo
    property string cfg_desktopTrackInfoLayout
    property bool cfg_desktopTrackInfoFontSameAsLyrics
    property string cfg_desktopTrackInfoFontFamily
    property alias cfg_desktopTrackInfoFontSize: desktopTrackInfoFontSize.value
    property int cfg_desktopTrackInfoFontWeight
    property string cfg_desktopTrackInfoColor
    property bool cfg_desktopTrackInfoStroke
    property string cfg_desktopTrackInfoStrokeColor
    property string cfg_desktopTrackInfoOverflow

    // The families the lyrics and the track info render in, for the weight
    // rows of AppearanceSection and TrackInfoSection. Computed here, from the
    // plain cfg_ properties above, rather than inside either section: see
    // AppearanceSection's lyricEffectiveFamily.
    readonly property string lyricFamily: FontPolicy.lyricFamily(FontCatalog,
        page.cfg_desktopFontFamily, Kirigami.Theme.defaultFont.family)
    readonly property string trackInfoFamily: FontPolicy.trackInfoFamily(FontCatalog,
        page.cfg_desktopTrackInfoFontSameAsLyrics, page.cfg_desktopTrackInfoFontFamily,
        page.lyricFamily, Kirigami.Theme.defaultFont.family)

    // DESIGN.md decision 40. Plain top-level properties rather than the
    // hidden-control-plus-alias dance the appearance keys above need: those
    // exist only because their actual SpinBox/CheckBox lives one component
    // down, inside AppearanceSection or TrackInfoSection, and a page-level
    // "cfg_" property has to bind to it somehow. These four have no such
    // child component to reach into -- the auto-hide FormLayout below is
    // declared right here -- so a plain property is the whole story.
    property bool cfg_desktopAutoHide
    property int cfg_desktopHideDelaySec
    property int cfg_desktopHideAnimationMs
    property bool cfg_desktopHideNonMusic

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
            text: i18n("These settings affect this widget only; other widgets are not affected.")
        }

        AppearanceSection {
            id: appearanceSection
            Layout.fillWidth: true
            plateMode: page.cfg_desktopPlateMode
            solidColor: page.cfg_desktopSolidColor
            textColor: page.cfg_desktopTextColor
            strokeEnabled: page.cfg_desktopStroke
            strokeColor: page.cfg_desktopStrokeColor
            fontCatalog: FontCatalog
            fontFamily: page.cfg_desktopFontFamily
            lyricEffectiveFamily: page.lyricFamily
            fontWeight: page.cfg_desktopFontWeight
            overflowMode: page.cfg_desktopOverflow
            animationMode: page.cfg_desktopAnimation
            fontSizeControl: desktopFontSize
            translationControl: desktopTranslation
            secondLineSource: page.cfg_desktopSecondLineSource
            secondLineColorEnabled: page.cfg_desktopSecondLineColorEnabled
            secondLineColor: page.cfg_desktopSecondLineColor
            lineHeightPercent: page.cfg_desktopLineHeight
            // Kept as a literal, matching main.qml's fullRepresentation
            // lineHeightMinPercent: 125 override rather than sharing one
            // constant -- DESIGN.md decision 69 has the reasoning.
            lineHeightMin: 125
            liftSupported: true
            wordByWord: page.cfg_desktopWordByWord
            syntheticWordByWord: page.cfg_desktopWordByWordSynthetic
            wordUnsungColor: page.cfg_desktopWordUnsungColor
            wordActiveColor: page.cfg_desktopWordActiveColor
            wordSungColor: page.cfg_desktopWordSungColor
            wordLift: page.cfg_desktopWordLift
            wordLiftRowVisible: page.cfg_desktopWordByWord && page.cfg_desktopWordLift
            wordLiftPercent: page.cfg_desktopWordLiftPercent
            wordBrightness: page.cfg_desktopWordBrightness
            wordBrightnessPercent: page.cfg_desktopWordBrightnessPercent
            wordBlurGlow: page.cfg_desktopWordBlurGlow
            onSecondLineSourceEdited: value => page.cfg_desktopSecondLineSource = value
            onSecondLineColorEnabledEdited: value => page.cfg_desktopSecondLineColorEnabled = value
            onSecondLineColorEdited: value => page.cfg_desktopSecondLineColor = value
            onLineHeightPercentEdited: value => page.cfg_desktopLineHeight = value
            onWordByWordEdited: value => page.cfg_desktopWordByWord = value
            onSyntheticWordByWordEdited: value => page.cfg_desktopWordByWordSynthetic = value
            onWordUnsungColorEdited: value => page.cfg_desktopWordUnsungColor = value
            onWordActiveColorEdited: value => page.cfg_desktopWordActiveColor = value
            onWordSungColorEdited: value => page.cfg_desktopWordSungColor = value
            onWordLiftEdited: value => page.cfg_desktopWordLift = value
            onWordLiftPercentEdited: value => page.cfg_desktopWordLiftPercent = value
            onWordBrightnessEdited: value => page.cfg_desktopWordBrightness = value
            onWordBrightnessPercentEdited: value => page.cfg_desktopWordBrightnessPercent = value
            onWordBlurGlowEdited: value => page.cfg_desktopWordBlurGlow = value
            onPlateModeEdited: value => page.cfg_desktopPlateMode = value
            onSolidColorEdited: value => page.cfg_desktopSolidColor = value
            onTextColorEdited: value => page.cfg_desktopTextColor = value
            onStrokeEnabledEdited: value => page.cfg_desktopStroke = value
            onStrokeColorEdited: value => page.cfg_desktopStrokeColor = value
            onFontFamilyEdited: value => page.cfg_desktopFontFamily = value
            onFontWeightEdited: value => page.cfg_desktopFontWeight = value
            onOverflowModeEdited: value => page.cfg_desktopOverflow = value
            onAnimationModeEdited: value => page.cfg_desktopAnimation = value

            trackInfoFontSameAsLyrics: page.cfg_desktopTrackInfoFontSameAsLyrics
            trackInfoFontWeight: page.cfg_desktopTrackInfoFontWeight
            onTrackInfoFontWeightEdited: value => page.cfg_desktopTrackInfoFontWeight = value
        }

        TrackInfoSection {
            Layout.fillWidth: true
            twinFormLayouts: [appearanceSection]
            fontCatalog: FontCatalog
            lyricEffectiveFamily: page.lyricFamily
            trackInfoEffectiveFamily: page.trackInfoFamily
            showTrackInfo: page.cfg_desktopShowTrackInfo
            trackInfoLayout: page.cfg_desktopTrackInfoLayout
            trackInfoFontSameAsLyrics: page.cfg_desktopTrackInfoFontSameAsLyrics
            trackInfoFontFamily: page.cfg_desktopTrackInfoFontFamily
            trackInfoFontWeight: page.cfg_desktopTrackInfoFontWeight
            trackInfoColor: page.cfg_desktopTrackInfoColor
            trackInfoStrokeEnabled: page.cfg_desktopTrackInfoStroke
            trackInfoStrokeColor: page.cfg_desktopTrackInfoStrokeColor
            trackInfoOverflow: page.cfg_desktopTrackInfoOverflow
            trackInfoFontSizeControl: desktopTrackInfoFontSize
            onShowTrackInfoEdited: value => page.cfg_desktopShowTrackInfo = value
            onTrackInfoLayoutEdited: value => page.cfg_desktopTrackInfoLayout = value
            onTrackInfoFontSameAsLyricsEdited: value => page.cfg_desktopTrackInfoFontSameAsLyrics = value
            onTrackInfoFontFamilyEdited: value => page.cfg_desktopTrackInfoFontFamily = value
            onTrackInfoFontWeightEdited: value => page.cfg_desktopTrackInfoFontWeight = value
            onTrackInfoColorEdited: value => page.cfg_desktopTrackInfoColor = value
            onTrackInfoStrokeEnabledEdited: value => page.cfg_desktopTrackInfoStroke = value
            onTrackInfoStrokeColorEdited: value => page.cfg_desktopTrackInfoStrokeColor = value
            onTrackInfoOverflowEdited: value => page.cfg_desktopTrackInfoOverflow = value
        }

        QQC2.SpinBox { id: desktopFontSize; visible: false }
        QQC2.CheckBox { id: desktopTranslation; visible: false }
        QQC2.SpinBox { id: desktopTrackInfoFontSize; visible: false }

        // A second top-level form rather than a section grafted onto
        // AppearanceSection: the panel tab's equivalent block has three
        // controls, not four (no animation duration -- the panel never
        // animates), so sharing a sub-component here would need a
        // visible-per-form-factor condition threaded through it for no
        // benefit. twinFormLayouts keeps its label column aligned with
        // AppearanceSection's above despite being a separate FormLayout.
        Kirigami.FormLayout {
            Layout.fillWidth: true
            twinFormLayouts: [appearanceSection]

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Auto-hide")
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Auto-hide:")
                text: i18n("Hide the widget when nothing is playing")
                checked: page.cfg_desktopAutoHide
                onToggled: page.cfg_desktopAutoHide = checked
            }
            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("Delay:")
                visible: page.cfg_desktopAutoHide
                from: 0
                to: 120
                value: page.cfg_desktopHideDelaySec
                onValueModified: page.cfg_desktopHideDelaySec = value
                textFromValue: (value, locale) => i18np("%1 second", "%1 seconds", value)
            }
            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("Fade duration:")
                visible: page.cfg_desktopAutoHide
                from: 0
                to: 3000
                stepSize: 50
                value: page.cfg_desktopHideAnimationMs
                onValueModified: page.cfg_desktopHideAnimationMs = value
                textFromValue: (value, locale) => i18n("%1 ms", value)
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Non-music media:")
                visible: page.cfg_desktopAutoHide
                text: i18n("Also hide while playing non-music media")
                checked: page.cfg_desktopHideNonMusic
                onToggled: page.cfg_desktopHideNonMusic = checked
            }
        }
    }
}
