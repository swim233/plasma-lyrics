import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import io.github.swim233.lyrics

import "../FontPolicy.js" as FontPolicy
import "../ThemePolicy.js" as ThemePolicy

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

    // DESIGN.md decision 76: the mode that picks one of the two sets, and
    // the light copy of every key ThemePolicy.themedSuffixes("desktop")
    // lists; the keys above with those suffixes are the dark copies. One
    // property per key, as for the dark set: the config dialog loads and
    // saves only the cfg_ properties a page declares.
    property string cfg_desktopColorSchemeMode
    property string cfg_desktopLightPlateMode
    property string cfg_desktopLightSolidColor
    property string cfg_desktopLightTextColor
    property bool cfg_desktopLightStroke
    property string cfg_desktopLightStrokeColor
    property string cfg_desktopLightFontFamily
    property alias cfg_desktopLightFontSize: desktopLightFontSize.value
    property int cfg_desktopLightFontWeight
    property string cfg_desktopLightOverflow
    property string cfg_desktopLightAnimation
    property alias cfg_desktopLightShowTranslation: desktopLightTranslation.checked
    property string cfg_desktopLightSecondLineSource
    property bool cfg_desktopLightSecondLineColorEnabled
    property string cfg_desktopLightSecondLineColor
    property int cfg_desktopLightLineHeight
    property bool cfg_desktopLightWordByWord
    property bool cfg_desktopLightWordByWordSynthetic
    property string cfg_desktopLightWordUnsungColor
    property string cfg_desktopLightWordActiveColor
    property string cfg_desktopLightWordSungColor
    property bool cfg_desktopLightWordLift
    property int cfg_desktopLightWordLiftPercent
    property bool cfg_desktopLightWordBrightness
    property int cfg_desktopLightWordBrightnessPercent
    property bool cfg_desktopLightWordBlurGlow
    property string cfg_desktopLightTrackInfoColor
    property bool cfg_desktopLightTrackInfoStroke
    property string cfg_desktopLightTrackInfoStrokeColor

    // The set the tabs show and edit. AppearanceSection reads and writes it
    // through these two: themed("TextColor") is cfg_desktopTextColor on the
    // Dark tab and cfg_desktopLightTextColor on the Light tab. A property
    // looked up by name is a binding dependency like any other, so a
    // binding over themed() follows both the tab and the key.
    readonly property bool editingDark: themeTabs.editingDark
    readonly property string editingPrefix: "cfg_" + ThemePolicy.keyPrefix("desktop", page.editingDark)
    // Whether the tab on screen is the set the widget renders with, which
    // decides whether a font pick writes the shared track-info weight (see
    // AppearanceSection's editLyricFamily).
    readonly property bool editingSetInEffect: page.editingDark === themeTabs.darkInEffect

    function themed(suffix) {
        return page[page.editingPrefix + suffix];
    }

    function editThemed(suffix, value) {
        page[page.editingPrefix + suffix] = value;
    }

    // Copies every key of the set `dark` names over the other set. Only the
    // values the dialog holds change; Apply or OK saves them like any other
    // edit. ThemeTabs calls this once the user confirms.
    function syncFrom(dark) {
        const from = "cfg_" + ThemePolicy.keyPrefix("desktop", dark);
        const to = "cfg_" + ThemePolicy.keyPrefix("desktop", !dark);
        for (const suffix of ThemePolicy.themedSuffixes("desktop")) {
            page[to + suffix] = page[from + suffix];
        }
    }

    // The families the lyrics and the track info render in, for the weight
    // rows of AppearanceSection and TrackInfoSection. Computed here, from the
    // plain cfg_ properties above, rather than inside either section: see
    // AppearanceSection's lyricEffectiveFamily. The lyric font is the one of
    // the set on screen, and so is the track info's "Same as lyrics".
    readonly property string lyricFamily: FontPolicy.lyricFamily(FontCatalog,
        page.themed("FontFamily"), Kirigami.Theme.defaultFont.family)
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

        ThemeTabs {
            id: themeTabs
            Layout.fillWidth: true
            formFactor: "desktop"
            mode: page.cfg_desktopColorSchemeMode
            twinFormLayouts: [appearanceSection]
            onModeEdited: value => page.cfg_desktopColorSchemeMode = value
            onSyncConfirmed: fromDark => page.syncFrom(fromDark)

            AppearanceSection {
                id: appearanceSection
                Layout.fillWidth: true
                plateMode: page.themed("PlateMode")
                solidColor: page.themed("SolidColor")
                textColor: page.themed("TextColor")
                strokeEnabled: page.themed("Stroke")
                strokeColor: page.themed("StrokeColor")
                fontCatalog: FontCatalog
                fontFamily: page.themed("FontFamily")
                lyricEffectiveFamily: page.lyricFamily
                fontWeight: page.themed("FontWeight")
                overflowMode: page.themed("Overflow")
                animationMode: page.themed("Animation")
                fontSizeControl: page.editingDark ? desktopFontSize : desktopLightFontSize
                translationControl: page.editingDark ? desktopTranslation : desktopLightTranslation
                secondLineSource: page.themed("SecondLineSource")
                secondLineColorEnabled: page.themed("SecondLineColorEnabled")
                secondLineColor: page.themed("SecondLineColor")
                lineHeightPercent: page.themed("LineHeight")
                // Kept as a literal, matching main.qml's fullRepresentation
                // lineHeightMinPercent: 125 override rather than sharing one
                // constant -- DESIGN.md decision 69 has the reasoning.
                lineHeightMin: 125
                liftSupported: true
                wordByWord: page.themed("WordByWord")
                syntheticWordByWord: page.themed("WordByWordSynthetic")
                wordUnsungColor: page.themed("WordUnsungColor")
                wordActiveColor: page.themed("WordActiveColor")
                wordSungColor: page.themed("WordSungColor")
                wordLift: page.themed("WordLift")
                wordLiftRowVisible: page.themed("WordByWord") && page.themed("WordLift")
                wordLiftPercent: page.themed("WordLiftPercent")
                wordBrightness: page.themed("WordBrightness")
                wordBrightnessPercent: page.themed("WordBrightnessPercent")
                wordBlurGlow: page.themed("WordBlurGlow")
                onSecondLineSourceEdited: value => page.editThemed("SecondLineSource", value)
                onSecondLineColorEnabledEdited: value => page.editThemed("SecondLineColorEnabled", value)
                onSecondLineColorEdited: value => page.editThemed("SecondLineColor", value)
                onLineHeightPercentEdited: value => page.editThemed("LineHeight", value)
                onWordByWordEdited: value => page.editThemed("WordByWord", value)
                onSyntheticWordByWordEdited: value => page.editThemed("WordByWordSynthetic", value)
                onWordUnsungColorEdited: value => page.editThemed("WordUnsungColor", value)
                onWordActiveColorEdited: value => page.editThemed("WordActiveColor", value)
                onWordSungColorEdited: value => page.editThemed("WordSungColor", value)
                onWordLiftEdited: value => page.editThemed("WordLift", value)
                onWordLiftPercentEdited: value => page.editThemed("WordLiftPercent", value)
                onWordBrightnessEdited: value => page.editThemed("WordBrightness", value)
                onWordBrightnessPercentEdited: value => page.editThemed("WordBrightnessPercent", value)
                onWordBlurGlowEdited: value => page.editThemed("WordBlurGlow", value)
                onPlateModeEdited: value => page.editThemed("PlateMode", value)
                onSolidColorEdited: value => page.editThemed("SolidColor", value)
                onTextColorEdited: value => page.editThemed("TextColor", value)
                onStrokeEnabledEdited: value => page.editThemed("Stroke", value)
                onStrokeColorEdited: value => page.editThemed("StrokeColor", value)
                onFontFamilyEdited: value => page.editThemed("FontFamily", value)
                onFontWeightEdited: value => page.editThemed("FontWeight", value)
                onOverflowModeEdited: value => page.editThemed("Overflow", value)
                onAnimationModeEdited: value => page.editThemed("Animation", value)

                showTrackInfo: page.cfg_desktopShowTrackInfo
                trackInfoColor: page.themed("TrackInfoColor")
                trackInfoStrokeEnabled: page.themed("TrackInfoStroke")
                trackInfoStrokeColor: page.themed("TrackInfoStrokeColor")
                onTrackInfoColorEdited: value => page.editThemed("TrackInfoColor", value)
                onTrackInfoStrokeEnabledEdited: value => page.editThemed("TrackInfoStroke", value)
                onTrackInfoStrokeColorEdited: value => page.editThemed("TrackInfoStrokeColor", value)
                // Shared by both sets: see editLyricFamily.
                setInEffect: page.editingSetInEffect
                trackInfoFontSameAsLyrics: page.cfg_desktopTrackInfoFontSameAsLyrics
                trackInfoFontWeight: page.cfg_desktopTrackInfoFontWeight
                onTrackInfoFontWeightEdited: value => page.cfg_desktopTrackInfoFontWeight = value
            }
        }

        // The rest of the track info, shared by both sets and so outside
        // the tabs.
        TrackInfoSection {
            Layout.fillWidth: true
            twinFormLayouts: [appearanceSection]
            fontCatalog: FontCatalog
            lyricEffectiveFamily: page.lyricFamily
            lyricSetInEffect: page.editingSetInEffect
            trackInfoEffectiveFamily: page.trackInfoFamily
            showTrackInfo: page.cfg_desktopShowTrackInfo
            trackInfoLayout: page.cfg_desktopTrackInfoLayout
            trackInfoFontSameAsLyrics: page.cfg_desktopTrackInfoFontSameAsLyrics
            trackInfoFontFamily: page.cfg_desktopTrackInfoFontFamily
            trackInfoFontWeight: page.cfg_desktopTrackInfoFontWeight
            trackInfoOverflow: page.cfg_desktopTrackInfoOverflow
            trackInfoFontSizeControl: desktopTrackInfoFontSize
            onShowTrackInfoEdited: value => page.cfg_desktopShowTrackInfo = value
            onTrackInfoLayoutEdited: value => page.cfg_desktopTrackInfoLayout = value
            onTrackInfoFontSameAsLyricsEdited: value => page.cfg_desktopTrackInfoFontSameAsLyrics = value
            onTrackInfoFontFamilyEdited: value => page.cfg_desktopTrackInfoFontFamily = value
            onTrackInfoFontWeightEdited: value => page.cfg_desktopTrackInfoFontWeight = value
            onTrackInfoOverflowEdited: value => page.cfg_desktopTrackInfoOverflow = value
        }

        QQC2.SpinBox { id: desktopFontSize; visible: false }
        QQC2.SpinBox { id: desktopLightFontSize; visible: false }
        QQC2.CheckBox { id: desktopTranslation; visible: false }
        QQC2.CheckBox { id: desktopLightTranslation; visible: false }
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
