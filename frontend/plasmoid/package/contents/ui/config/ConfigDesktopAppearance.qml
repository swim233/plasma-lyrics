import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: page

    property string cfg_desktopPlateMode
    property string cfg_desktopSolidColor
    property string cfg_desktopTextColor
    property bool cfg_desktopStroke
    property string cfg_desktopStrokeColor
    property alias cfg_desktopFontSize: desktopFontSize.value
    property int cfg_desktopFontWeight
    property string cfg_desktopOverflow
    property string cfg_desktopAnimation
    property alias cfg_desktopShowTranslation: desktopTranslation.checked

    property bool cfg_desktopShowTrackInfo
    property string cfg_desktopTrackInfoLayout
    property alias cfg_desktopTrackInfoFontSize: desktopTrackInfoFontSize.value
    property int cfg_desktopTrackInfoFontWeight
    property string cfg_desktopTrackInfoColor
    property bool cfg_desktopTrackInfoStroke
    property string cfg_desktopTrackInfoStrokeColor
    property string cfg_desktopTrackInfoOverflow

    // DESIGN.md decision 40. Plain top-level properties rather than the
    // hidden-control-plus-alias dance the appearance keys above need: those
    // exist only because their actual SpinBox/CheckBox lives one component
    // down, inside AppearanceSection, and a page-level "cfg_" property has to
    // bind to it somehow. These four have no such child component to reach
    // into -- the auto-hide FormLayout below is declared right here -- so a
    // plain property is the whole story.
    property bool cfg_desktopAutoHide
    property int cfg_desktopHideDelaySec
    property int cfg_desktopHideAnimationMs
    property bool cfg_desktopHideNonMusic

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        AppearanceSection {
            id: appearanceSection
            Layout.fillWidth: true
            plateMode: page.cfg_desktopPlateMode
            solidColor: page.cfg_desktopSolidColor
            textColor: page.cfg_desktopTextColor
            strokeEnabled: page.cfg_desktopStroke
            strokeColor: page.cfg_desktopStrokeColor
            fontWeight: page.cfg_desktopFontWeight
            overflowMode: page.cfg_desktopOverflow
            animationMode: page.cfg_desktopAnimation
            fontSizeControl: desktopFontSize
            translationControl: desktopTranslation
            onPlateModeEdited: value => page.cfg_desktopPlateMode = value
            onSolidColorEdited: value => page.cfg_desktopSolidColor = value
            onTextColorEdited: value => page.cfg_desktopTextColor = value
            onStrokeEnabledEdited: value => page.cfg_desktopStroke = value
            onStrokeColorEdited: value => page.cfg_desktopStrokeColor = value
            onFontWeightEdited: value => page.cfg_desktopFontWeight = value
            onOverflowModeEdited: value => page.cfg_desktopOverflow = value
            onAnimationModeEdited: value => page.cfg_desktopAnimation = value

            showTrackInfo: page.cfg_desktopShowTrackInfo
            trackInfoLayout: page.cfg_desktopTrackInfoLayout
            trackInfoFontWeight: page.cfg_desktopTrackInfoFontWeight
            trackInfoColor: page.cfg_desktopTrackInfoColor
            trackInfoStrokeEnabled: page.cfg_desktopTrackInfoStroke
            trackInfoStrokeColor: page.cfg_desktopTrackInfoStrokeColor
            trackInfoOverflow: page.cfg_desktopTrackInfoOverflow
            trackInfoFontSizeControl: desktopTrackInfoFontSize
            onShowTrackInfoEdited: value => page.cfg_desktopShowTrackInfo = value
            onTrackInfoLayoutEdited: value => page.cfg_desktopTrackInfoLayout = value
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
