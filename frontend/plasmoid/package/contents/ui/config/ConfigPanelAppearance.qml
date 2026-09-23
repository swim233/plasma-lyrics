import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import io.github.swim233.lyrics

import "../FontPolicy.js" as FontPolicy
import "../ThemePolicy.js" as ThemePolicy

Kirigami.ScrollablePage {
    id: page

    property string cfg_panelPlateMode
    property string cfg_panelSolidColor
    property string cfg_panelTextColor
    property bool cfg_panelStroke
    property string cfg_panelStrokeColor
    property string cfg_panelFontFamily
    property alias cfg_panelFontSize: panelFontSize.value
    property int cfg_panelFontWeight
    property string cfg_panelOverflow
    property string cfg_panelAnimation
    property alias cfg_panelShowTranslation: panelTranslation.checked
    property string cfg_panelSecondLineSource
    property bool cfg_panelSecondLineColorEnabled
    property string cfg_panelSecondLineColor
    property int cfg_panelLineHeight

    property bool cfg_panelWordByWord
    property bool cfg_panelWordByWordSynthetic
    property string cfg_panelWordUnsungColor
    property string cfg_panelWordActiveColor
    property string cfg_panelWordSungColor
    property bool cfg_panelWordBrightness
    property int cfg_panelWordBrightnessPercent
    property bool cfg_panelWordBlurGlow

    property bool cfg_panelShowTrackInfo
    property string cfg_panelTrackInfoLayout
    property bool cfg_panelTrackInfoFontSameAsLyrics
    property string cfg_panelTrackInfoFontFamily
    property alias cfg_panelTrackInfoFontSize: panelTrackInfoFontSize.value
    property int cfg_panelTrackInfoFontWeight
    property string cfg_panelTrackInfoColor
    property bool cfg_panelTrackInfoStroke
    property string cfg_panelTrackInfoStrokeColor
    property string cfg_panelTrackInfoOverflow

    // DESIGN.md decision 76: the mode that picks one of the two sets, and
    // the light copy of every key ThemePolicy.themedSuffixes("panel") lists;
    // the keys above with those suffixes are the dark copies. One property
    // per key, as for the dark set: the config dialog loads and saves only
    // the cfg_ properties a page declares.
    property string cfg_panelThemeMode
    property string cfg_panelLightPlateMode
    property string cfg_panelLightSolidColor
    property string cfg_panelLightTextColor
    property bool cfg_panelLightStroke
    property string cfg_panelLightStrokeColor
    property string cfg_panelLightFontFamily
    property alias cfg_panelLightFontSize: panelLightFontSize.value
    property int cfg_panelLightFontWeight
    property string cfg_panelLightOverflow
    property string cfg_panelLightAnimation
    property alias cfg_panelLightShowTranslation: panelLightTranslation.checked
    property string cfg_panelLightSecondLineSource
    property bool cfg_panelLightSecondLineColorEnabled
    property string cfg_panelLightSecondLineColor
    property int cfg_panelLightLineHeight
    property bool cfg_panelLightWordByWord
    property bool cfg_panelLightWordByWordSynthetic
    property string cfg_panelLightWordUnsungColor
    property string cfg_panelLightWordActiveColor
    property string cfg_panelLightWordSungColor
    property bool cfg_panelLightWordBrightness
    property int cfg_panelLightWordBrightnessPercent
    property bool cfg_panelLightWordBlurGlow
    property string cfg_panelLightTrackInfoColor
    property bool cfg_panelLightTrackInfoStroke
    property string cfg_panelLightTrackInfoStrokeColor

    // Whether the Plasma style is dark, as on the desktop page.
    property bool styleDark: PlasmaStyle.isDark()

    // The set the tabs show and edit, as on the desktop page.
    readonly property bool editingDark: themeTabs.editingDark
    readonly property string editingPrefix: "cfg_" + ThemePolicy.keyPrefix("panel", page.editingDark)
    readonly property bool editingSetInEffect: page.editingDark === themeTabs.darkInEffect

    function themed(suffix) {
        return page[page.editingPrefix + suffix];
    }

    function editThemed(suffix, value) {
        page[page.editingPrefix + suffix] = value;
    }

    // Copies every key of the set `dark` names over the other set, in the
    // dialog's values only, as on the desktop page.
    function syncFrom(dark) {
        const from = "cfg_" + ThemePolicy.keyPrefix("panel", dark);
        const to = "cfg_" + ThemePolicy.keyPrefix("panel", !dark);
        for (const suffix of ThemePolicy.themedSuffixes("panel")) {
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
        page.cfg_panelTrackInfoFontSameAsLyrics, page.cfg_panelTrackInfoFontFamily,
        page.lyricFamily, Kirigami.Theme.defaultFont.family)

    // Plain top-level property, same "Pattern 2" as the auto-hide block
    // below: this SpinBox is declared directly in this file rather than
    // inside AppearanceSection, so there is no child control for a "cfg_"
    // property to alias into.
    property int cfg_panelWidth

    // DESIGN.md decision 40: three controls here against the desktop tab's
    // four -- no fade-duration SpinBox, because the panel never animates
    // (it hides via Plasmoid.status, pulling the container out of the
    // layout, not by fading opacity). See main.xml for why that is a real
    // kcfg asymmetry and not an oversight.
    property bool cfg_panelAutoHide
    property int cfg_panelHideDelaySec
    property bool cfg_panelHideNonMusic

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
            formFactor: "panel"
            styleDark: page.styleDark
            mode: page.cfg_panelThemeMode
            twinFormLayouts: [appearanceSection]
            wideMode: appearanceSection.wideMode
            onModeEdited: value => page.cfg_panelThemeMode = value
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
                fontSizeControl: page.editingDark ? panelFontSize : panelLightFontSize
                translationControl: page.editingDark ? panelTranslation : panelLightTranslation
                secondLineSource: page.themed("SecondLineSource")
                secondLineColorEnabled: page.themed("SecondLineColorEnabled")
                secondLineColor: page.themed("SecondLineColor")
                lineHeightPercent: page.themed("LineHeight")
                liftSupported: false
                // No panel lift keys exist, so the magnitude row stays hidden
                // and the toggle above it reads unchecked-and-disabled.
                wordLift: false
                wordLiftRowVisible: false
                wordByWord: page.themed("WordByWord")
                syntheticWordByWord: page.themed("WordByWordSynthetic")
                wordUnsungColor: page.themed("WordUnsungColor")
                wordActiveColor: page.themed("WordActiveColor")
                wordSungColor: page.themed("WordSungColor")
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

                showTrackInfo: page.cfg_panelShowTrackInfo
                trackInfoColor: page.themed("TrackInfoColor")
                trackInfoStrokeEnabled: page.themed("TrackInfoStroke")
                trackInfoStrokeColor: page.themed("TrackInfoStrokeColor")
                onTrackInfoColorEdited: value => page.editThemed("TrackInfoColor", value)
                onTrackInfoStrokeEnabledEdited: value => page.editThemed("TrackInfoStroke", value)
                onTrackInfoStrokeColorEdited: value => page.editThemed("TrackInfoStrokeColor", value)
                // Shared by both sets: see editLyricFamily.
                setInEffect: page.editingSetInEffect
                trackInfoFontSameAsLyrics: page.cfg_panelTrackInfoFontSameAsLyrics
                trackInfoFontWeight: page.cfg_panelTrackInfoFontWeight
                onTrackInfoFontWeightEdited: value => page.cfg_panelTrackInfoFontWeight = value
            }
        }

        // The rest of the track info, shared by both sets and so outside
        // the tabs. Like every form outside the frame it takes wideMode from
        // the one inside, which the frame's padding makes the narrowest: on
        // their own, the forms would switch between one and two columns at
        // different page widths.
        TrackInfoSection {
            Layout.fillWidth: true
            twinFormLayouts: [appearanceSection]
            wideMode: appearanceSection.wideMode
            fontCatalog: FontCatalog
            lyricEffectiveFamily: page.lyricFamily
            lyricSetInEffect: page.editingSetInEffect
            trackInfoEffectiveFamily: page.trackInfoFamily
            showTrackInfo: page.cfg_panelShowTrackInfo
            trackInfoLayout: page.cfg_panelTrackInfoLayout
            trackInfoFontSameAsLyrics: page.cfg_panelTrackInfoFontSameAsLyrics
            trackInfoFontFamily: page.cfg_panelTrackInfoFontFamily
            trackInfoFontWeight: page.cfg_panelTrackInfoFontWeight
            trackInfoOverflow: page.cfg_panelTrackInfoOverflow
            trackInfoFontSizeControl: panelTrackInfoFontSize
            onShowTrackInfoEdited: value => page.cfg_panelShowTrackInfo = value
            onTrackInfoLayoutEdited: value => page.cfg_panelTrackInfoLayout = value
            onTrackInfoFontSameAsLyricsEdited: value => page.cfg_panelTrackInfoFontSameAsLyrics = value
            onTrackInfoFontFamilyEdited: value => page.cfg_panelTrackInfoFontFamily = value
            onTrackInfoFontWeightEdited: value => page.cfg_panelTrackInfoFontWeight = value
            onTrackInfoOverflowEdited: value => page.cfg_panelTrackInfoOverflow = value
        }

        QQC2.SpinBox { id: panelFontSize; visible: false }
        QQC2.SpinBox { id: panelLightFontSize; visible: false }
        QQC2.CheckBox { id: panelTranslation; visible: false }
        QQC2.CheckBox { id: panelLightTranslation; visible: false }
        QQC2.SpinBox { id: panelTrackInfoFontSize; visible: false }

        // Plasma 6.7 gives panel applets no drag-resize at all, so this is
        // the only way to size the widget in a panel. A separate FormLayout
        // rather than a fourth control folded into AppearanceSection: width
        // is panel-only (the desktop tab sizes itself by dragging its own
        // widget, see ConfigDesktopAppearance.qml), so there is no shared
        // control for it to become. twinFormLayouts keeps its label column
        // aligned with AppearanceSection's above despite being separate.
        Kirigami.FormLayout {
            Layout.fillWidth: true
            twinFormLayouts: [appearanceSection]
            wideMode: appearanceSection.wideMode

            QQC2.SpinBox {
                Kirigami.FormData.label: i18nc("@label:spinbox", "Width:")
                from: 80
                to: 2000
                stepSize: 10
                value: page.cfg_panelWidth
                onValueModified: page.cfg_panelWidth = value
            }
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            twinFormLayouts: [appearanceSection]
            wideMode: appearanceSection.wideMode

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Auto-hide")
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Auto-hide:")
                text: i18n("Hide the widget when nothing is playing")
                checked: page.cfg_panelAutoHide
                onToggled: page.cfg_panelAutoHide = checked
            }
            QQC2.SpinBox {
                Kirigami.FormData.label: i18n("Delay:")
                visible: page.cfg_panelAutoHide
                from: 0
                to: 120
                value: page.cfg_panelHideDelaySec
                onValueModified: page.cfg_panelHideDelaySec = value
                textFromValue: (value, locale) => i18np("%1 second", "%1 seconds", value)
            }
            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Non-music media:")
                visible: page.cfg_panelAutoHide
                text: i18n("Also hide while playing non-music media")
                checked: page.cfg_panelHideNonMusic
                onToggled: page.cfg_panelHideNonMusic = checked
            }
        }
    }
}
