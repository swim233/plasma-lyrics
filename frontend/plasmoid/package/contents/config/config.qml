import QtQuick
import org.kde.plasma.configuration
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore

ConfigModel {
    id: configModel
    readonly property bool onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar

    ConfigCategory {
        id: desktopAppearance
        name: i18n("Desktop appearance")
        icon: "preferences-desktop-theme"
        source: "config/ConfigDesktopAppearance.qml"
    }
    ConfigCategory {
        id: panelAppearance
        name: i18n("Panel appearance")
        icon: "preferences-desktop-theme"
        source: "config/ConfigPanelAppearance.qml"
    }
    ConfigCategory {
        name: i18n("Text")
        icon: "draw-text"
        source: "config/ConfigText.qml"
    }
    ConfigCategory {
        name: i18n("Global settings")
        icon: "chronometer"
        source: "config/ConfigGlobal.qml"
    }
    ConfigCategory {
        name: i18n("Lyrics Service")
        icon: "preferences-system-services"
        source: "config/ConfigBackend.qml"
    }

    // The dialog shell (org.kde.plasma.desktop's and plasmoidviewer's
    // AppletConfiguration.qml alike) picks the initially-shown page with
    // configModel.get(0) on the unfiltered model -- ConfigCategory.visible
    // only filters the sidebar list, not this lookup. Removing the
    // inapplicable appearance category outright keeps get(0) landing on the
    // matching one for both form factors instead of leaving a hidden,
    // unhighlighted "Desktop appearance" page open on panel instances.
    Component.onCompleted: configModel.removeCategory(onDesktop ? panelAppearance : desktopAppearance)
}

