import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ScrollablePage {
    id: page

    property string cfg_panelPlateMode
    property string cfg_panelSolidColor
    property string cfg_panelTextColor
    property bool cfg_panelStroke
    property string cfg_panelStrokeColor
    property alias cfg_panelFontSize: panelFontSize.value
    property int cfg_panelFontWeight
    property string cfg_panelOverflow
    property string cfg_panelAnimation
    property alias cfg_panelShowTranslation: panelTranslation.checked

    property bool cfg_panelShowTrackInfo
    property string cfg_panelTrackInfoLayout
    property alias cfg_panelTrackInfoFontSize: panelTrackInfoFontSize.value
    property int cfg_panelTrackInfoFontWeight
    property string cfg_panelTrackInfoColor
    property bool cfg_panelTrackInfoStroke
    property string cfg_panelTrackInfoStrokeColor
    property string cfg_panelTrackInfoOverflow

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

        AppearanceSection {
            id: appearanceSection
            Layout.fillWidth: true
            plateMode: page.cfg_panelPlateMode
            solidColor: page.cfg_panelSolidColor
            textColor: page.cfg_panelTextColor
            strokeEnabled: page.cfg_panelStroke
            strokeColor: page.cfg_panelStrokeColor
            fontWeight: page.cfg_panelFontWeight
            overflowMode: page.cfg_panelOverflow
            animationMode: page.cfg_panelAnimation
            fontSizeControl: panelFontSize
            translationControl: panelTranslation
            onPlateModeEdited: value => page.cfg_panelPlateMode = value
            onSolidColorEdited: value => page.cfg_panelSolidColor = value
            onTextColorEdited: value => page.cfg_panelTextColor = value
            onStrokeEnabledEdited: value => page.cfg_panelStroke = value
            onStrokeColorEdited: value => page.cfg_panelStrokeColor = value
            onFontWeightEdited: value => page.cfg_panelFontWeight = value
            onOverflowModeEdited: value => page.cfg_panelOverflow = value
            onAnimationModeEdited: value => page.cfg_panelAnimation = value

            showTrackInfo: page.cfg_panelShowTrackInfo
            trackInfoLayout: page.cfg_panelTrackInfoLayout
            trackInfoFontWeight: page.cfg_panelTrackInfoFontWeight
            trackInfoColor: page.cfg_panelTrackInfoColor
            trackInfoStrokeEnabled: page.cfg_panelTrackInfoStroke
            trackInfoStrokeColor: page.cfg_panelTrackInfoStrokeColor
            trackInfoOverflow: page.cfg_panelTrackInfoOverflow
            trackInfoFontSizeControl: panelTrackInfoFontSize
            onShowTrackInfoEdited: value => page.cfg_panelShowTrackInfo = value
            onTrackInfoLayoutEdited: value => page.cfg_panelTrackInfoLayout = value
            onTrackInfoFontWeightEdited: value => page.cfg_panelTrackInfoFontWeight = value
            onTrackInfoColorEdited: value => page.cfg_panelTrackInfoColor = value
            onTrackInfoStrokeEnabledEdited: value => page.cfg_panelTrackInfoStroke = value
            onTrackInfoStrokeColorEdited: value => page.cfg_panelTrackInfoStrokeColor = value
            onTrackInfoOverflowEdited: value => page.cfg_panelTrackInfoOverflow = value
        }

        QQC2.SpinBox { id: panelFontSize; visible: false }
        QQC2.CheckBox { id: panelTranslation; visible: false }
        QQC2.SpinBox { id: panelTrackInfoFontSize; visible: false }

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
