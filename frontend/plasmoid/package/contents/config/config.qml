import QtQuick
import org.kde.plasma.configuration

ConfigModel {
    ConfigCategory {
        name: i18n("Desktop appearance")
        icon: "preferences-desktop-theme"
        source: "config/ConfigDesktopAppearance.qml"
    }
    ConfigCategory {
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
        name: i18n("Lyrics Service")
        icon: "preferences-system-services"
        source: "config/ConfigBackend.qml"
    }
}

