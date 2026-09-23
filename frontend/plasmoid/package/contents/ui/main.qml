import QtQuick

import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

import io.github.swim233.lyrics

import "FontPolicy.js" as FontPolicy
import "TextPolicy.js" as TextPolicy
import "ThemePolicy.js" as ThemePolicy

PlasmoidItem {
    id: root

    readonly property bool onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar
    readonly property string activePlateMode: root.onDesktop
        ? desktopTheme.value("PlateMode")
        : panelTheme.value("PlateMode")

    readonly property bool activeAutoHide: root.onDesktop
        ? Plasmoid.configuration.desktopAutoHide
        : Plasmoid.configuration.panelAutoHide

    // Who draws the "ksvg" plate depends on whether auto-hide is on, because
    // the two things the plate has to do are mutually exclusive from inside an
    // applet.
    //
    // The shell's plate is a *sibling* of this applet's content item, so no
    // opacity we set can fade it -- which is what auto-hide needs on the
    // desktop. Drawing it ourselves fixes that. But the shell does more than
    // put a KSvg.FrameSvgItem on "widgets/background": when the theme ships
    // blurred-* elements (ChromeOS does; Breeze does not), it switches the
    // frame to prefix "blurred" and stacks a MultiEffect that samples the
    // wallpaper and blurs it behind the frame, masked by blurred-mask. That
    // effect reaches into the containment's own window and wallpaper, so an
    // applet cannot reproduce it -- self-drawing unconditionally turns a
    // blurred plate into a flat opaque slab on any theme that has one.
    //
    // So the shell keeps the plate whenever auto-hide is off, which is the
    // default and the only state where the plate never has to fade. Turning
    // auto-hide on trades the blur for a plate that can fade with the rest of
    // the widget. See DESIGN.md decision 40.
    //
    // ConfigurableBackground is left out on purpose: it adds the shell's own
    // show-background checkbox beside our three-way plate setting, and the two
    // would then disagree about the same thing.
    Plasmoid.backgroundHints: root.activePlateMode === "ksvg" && !root.plateSelfDrawn
        ? PlasmaCore.Types.DefaultBackground
        : PlasmaCore.Types.NoBackground
    // How long the desktop fade actually lasts, or 0 when there will be no
    // fade at all. Computed here rather than only inside LyricsView because
    // the plate handover below has to wait exactly as long as the fade does,
    // and two places disagreeing about whether an animation is running is
    // precisely what would leave the plate stranded in the wrong hands.
    readonly property int effectiveFadeMs:
        Plasmoid.configuration.desktopHideAnimationMs > 0 && Kirigami.Units.longDuration > 1
            ? Plasmoid.configuration.desktopHideAnimationMs
            : 0

    // The plate changes hands while auto-hide is on, because the shell draws a
    // strictly better one but ours is the only one that can fade. The shell
    // holds it whenever the widget is sitting fully visible; the applet takes
    // it for the duration of a fade and for as long as the widget is hidden.
    //
    // Both consumers -- Plasmoid.backgroundHints above and LyricsView's
    // `ownsPlate` below -- read this one property, so the handover is a single
    // binding pass rather than two that could disagree for a frame.
    property bool platePassedToShell: true
    readonly property bool plateSelfDrawn: root.activeAutoHide && !root.platePassedToShell

    Timer {
        id: plateHandbackTimer
        interval: root.effectiveFadeMs
        onTriggered: root.platePassedToShell = true
    }

    // Imperative rather than a binding: "hand back once the fade that just
    // started has finished" is a sequence, not a function of the current
    // state, and a binding cannot express the wait.
    function updatePlateOwner() {
        if (!root.activeAutoHide) {
            plateHandbackTimer.stop();
            root.platePassedToShell = true;
            return;
        }
        if (!visibilityPolicy.shouldBeVisible) {
            // Take the plate before the fade-out starts: the shell's does not
            // fade, so leaving it in place would fade the text out inside a
            // plate that stays put.
            plateHandbackTimer.stop();
            root.platePassedToShell = false;
            return;
        }
        if (!root.desktopAnimationsArmed || root.effectiveFadeMs <= 0) {
            // Nothing to wait for -- the landing transition out of
            // "undetermined" never animates, and a zero duration means the
            // widget simply appears.
            plateHandbackTimer.stop();
            root.platePassedToShell = true;
            return;
        }
        root.platePassedToShell = false;
        plateHandbackTimer.restart();
    }

    onActiveAutoHideChanged: root.updatePlateOwner()

    Connections {
        target: visibilityPolicy
        function onShouldBeVisibleChanged() { root.updatePlateOwner(); }
    }

    Plasmoid.title: i18n("Desktop Lyrics")
    preferredRepresentation: root.onDesktop ? fullRepresentation : compactRepresentation

    ConfigChangeLogger {
        configuration: Plasmoid.configuration
        appletId: Plasmoid.id
        form: root.onDesktop ? "desktop" : "panel"
    }

    LyricSource {
        id: lyricSource
    }

    // DESIGN.md decision 40. One instance shared by both representations,
    // living at the PlasmoidItem root rather than inside either
    // representation's Loader: a representation swap (e.g. dragging the
    // widget from the desktop into a panel) destroys and recreates whatever
    // lives inside the Loader, which would silently reset a buffer timer
    // mid-countdown and make lyrics reappear for no reason.
    VisibilityPolicy {
        id: visibilityPolicy
        serviceAvailable: lyricSource.serviceAvailable
        stale: lyricSource.stale
        playbackStatus: lyricSource.playbackStatus
        trackTitle: lyricSource.trackTitle
        lyricState: lyricSource.lyricState
        determined: lyricSource.determined
        enabled: root.onDesktop ? Plasmoid.configuration.desktopAutoHide : Plasmoid.configuration.panelAutoHide
        hideNonMusic: root.onDesktop ? Plasmoid.configuration.desktopHideNonMusic : Plasmoid.configuration.panelHideNonMusic
        delayMs: (root.onDesktop ? Plasmoid.configuration.desktopHideDelaySec : Plasmoid.configuration.panelHideDelaySec) * 1000
    }

    // DESIGN.md decision 76: the appearance set each form factor renders
    // with. Every themed key below is read as <form>Theme.value("<Suffix>"),
    // never as Plasmoid.configuration.<form><Suffix>, which would pin the
    // dark set; tst_configschema checks that, that each view reads all of its
    // form's suffixes from its own form's theme, and the three lines each
    // theme is declared with below. At the root for the reason
    // VisibilityPolicy gives above: a theme recreated inside a
    // representation's Loader would drop a transition in progress, and
    // activePlateMode needs one outside either representation anyway.
    AppearanceTheme {
        id: desktopTheme
        configuration: Plasmoid.configuration
        formFactor: "desktop"
        styleDark: root.plasmaStyleDark
        mounted: root.parent !== null
    }
    AppearanceTheme {
        id: panelTheme
        configuration: Plasmoid.configuration
        formFactor: "panel"
        styleDark: root.plasmaStyleDark
        mounted: root.parent !== null
    }
    // Inside the widget Kirigami.Theme carries the Plasma style's colours, not
    // the colour scheme's (checked with plasmoidviewer: breeze-dark under the
    // BreezeLight scheme reports #202326 here), so "auto" follows what draws
    // the plate and the panel. Read on this Item: the AppearanceTheme
    // QtObjects have no Kirigami.Theme of their own. It reads #000000,
    // whatever the style, until Plasma parents this item, and from then on
    // follows a style change as it happens, with the same value here as in
    // either representation.
    readonly property bool plasmaStyleDark: ThemePolicy.isDarkBackground(Kirigami.Theme.backgroundColor)
    // Put into a container, taken out of one or moved to another: the style's
    // colour follows the new parent within this turn, and both themes let it
    // arrive without a transition. Without a parent they do not settle at
    // all (`mounted` above).
    onParentChanged: {
        desktopTheme.holdStill();
        panelTheme.holdStill();
    }

    // Gates the desktop fade Behavior (see LyricsView's `animationsArmed`):
    // true forever after the first determination, never reset. Deliberately
    // NOT a direct binding on lyricSource.determined -- see below.
    property bool desktopAnimationsArmed: false

    Connections {
        target: lyricSource
        function onDeterminedChanged() {
            if (lyricSource.determined) {
                // Qt.callLater defers this to a later event-loop turn on
                // purpose. lyricSource.determined flipping true is also what
                // drives visibilityPolicy.shouldBeVisible to its first real
                // value in this very turn (via the `determined:` binding
                // above), and Qt does not guarantee the Behavior's `enabled`
                // binding re-evaluates after the opacity target's does. Arm
                // it one turn later instead of racing that ordering, so the
                // landing transition is guaranteed instantaneous rather than
                // "usually instantaneous".
                Qt.callLater(() => { root.desktopAnimationsArmed = true; });
            }
        }
    }

    // Plasmoid.status has to be reasserted imperatively, not bound: opening
    // the compact representation's popup makes CompactApplet.qml stomp it to
    // RequiresAttentionStatus for as long as the popup is open, restoring
    // whatever it captured beforehand on close. A plain binding would lose
    // to that the moment anyone expands the applet. HiddenStatus is a no-op
    // on the desktop (Planar) form factor, so this only actually does
    // anything for the panel.
    //
    // No edit-mode/userConfiguring escape hatch here on purpose: it already
    // exists, but on the shell's side rather than ours. The panel's own
    // LayoutManager.js binds each applet container's `visible` to exactly
    // `applet.status !== HiddenStatus || (!plasmoid.immutable &&
    // plasmoid.userConfiguring) || plasmoid.corona.editMode` -- setting
    // HiddenStatus while the user is editing the panel is consumed by that
    // binding and never actually hides anything. Duplicating the condition
    // here would be redundant at best and, given it would have to spell it
    // "Plasmoid.containment.corona.editMode" from an applet's side rather
    // than the containment's own "Plasmoid.corona.editMode", an easy place
    // to introduce a typo the shell has already made unnecessary.
    function updateStatus() {
        Plasmoid.status = visibilityPolicy.shouldBeVisible
            ? PlasmaCore.Types.ActiveStatus
            : PlasmaCore.Types.HiddenStatus;
    }

    Component.onCompleted: {
        TextPolicy.migrateConfiguration(Plasmoid.configuration);
        ThemePolicy.migrateConfiguration(Plasmoid.configuration);
        root.updatePlateOwner();
        if (!root.onDesktop) {
            root.updateStatus();
        }
    }

    Connections {
        target: visibilityPolicy
        function onShouldBeVisibleChanged() {
            // Both `expanded` writes below are gated on !onDesktop: the
            // popup-over-a-hidden-item problem they guard against is
            // structurally a panel-only concern. On the desktop,
            // preferredRepresentation is fullRepresentation (main.qml above),
            // so appletShouldBeExpanded() is always true and the compact
            // representation's popup/expander is never created in the first
            // place -- there is no popup path to reject or close, and
            // writing `expanded` here would be reaching for a door that does
            // not exist on this form factor.
            if (!root.onDesktop) {
                if (!visibilityPolicy.shouldBeVisible) {
                    // A popup anchored on a now-hidden compact item, showing
                    // an empty lyric area, would just be confusing -- close
                    // it rather than leave it stranded open.
                    root.expanded = false;
                }
                root.updateStatus();
            }
        }
    }

    onExpandedChanged: {
        if (!root.onDesktop) {
            if (root.expanded && !visibilityPolicy.shouldBeVisible) {
                // Reject: opening was triggered by the global shortcut or a
                // Space/Enter press on the compact item (CompactApplet.qml),
                // neither of which goes through a MouseArea we could disable.
                root.expanded = false;
                return;
            }
            root.updateStatus();
        }
    }

    compactRepresentation: LyricsView {
        id: compactView
        source: lyricSource
        plateMode: panelTheme.value("PlateMode")
        panelWidth: Plasmoid.configuration.panelWidth
        ownsPlate: root.plateSelfDrawn
        solidColor: panelTheme.value("SolidColor")
        textColor: panelTheme.value("TextColor")
        strokeEnabled: panelTheme.value("Stroke")
        strokeColor: panelTheme.value("StrokeColor")
        // DESIGN.md decision 30. The stored family and weights are never
        // written back. A family that is not installed renders in the Plasma
        // font -- named explicitly, not left to fontconfig's substitution --
        // and comes back by itself once it is reinstalled. Each weight is
        // snapped to a real face of the family actually drawn, by the CSS
        // weight-matching rule, so fontconfig never synthesizes bold. The
        // track-info family and both weights read the lyric family off this
        // item instead of resolving it a second time.
        fontFamily: FontPolicy.lyricFamily(FontCatalog,
                                           panelTheme.value("FontFamily"),
                                           Kirigami.Theme.defaultFont.family)
        fontSize: panelTheme.value("FontSize")
        fontWeight: FontPolicy.renderWeight(FontCatalog, compactView.fontFamily,
                                            panelTheme.value("FontWeight"))
        overflowMode: panelTheme.value("Overflow")
        animationMode: panelTheme.value("Animation")
        showTranslation: panelTheme.value("ShowTranslation")
        secondLineSource: panelTheme.value("SecondLineSource")
        secondLineColorEnabled: panelTheme.value("SecondLineColorEnabled")
        secondLineColor: panelTheme.value("SecondLineColor")
        lineHeightPercent: panelTheme.value("LineHeight")
        wordByWord: panelTheme.value("WordByWord")
        syntheticWordByWord: panelTheme.value("WordByWordSynthetic")
        wordUnsungColor: panelTheme.value("WordUnsungColor")
        wordActiveColor: panelTheme.value("WordActiveColor")
        wordSungColor: panelTheme.value("WordSungColor")
        wordBrightness: panelTheme.value("WordBrightness")
        wordBrightnessPercent: panelTheme.value("WordBrightnessPercent")
        wordBlurGlow: panelTheme.value("WordBlurGlow")
        idleText: TextPolicy.effectiveText(Plasmoid.configuration.emptyTextUseDefault,
                                           Plasmoid.configuration.idleText,
                                           i18n("No media is playing"))
        notFoundText: TextPolicy.effectiveText(Plasmoid.configuration.emptyTextUseDefault,
                                               Plasmoid.configuration.notFoundText,
                                               i18n("Lyrics not found"))
        noLyricText: TextPolicy.effectiveText(Plasmoid.configuration.emptyTextUseDefault,
                                              Plasmoid.configuration.noLyricText,
                                              i18n("Instrumental track, enjoy the music"))
        networkErrorText: TextPolicy.effectiveText(Plasmoid.configuration.emptyTextUseDefault,
                                                   Plasmoid.configuration.networkErrorText,
                                                   i18n("Network error, cannot fetch lyrics"))
        panelMode: true
        showTrackInfo: Plasmoid.configuration.panelShowTrackInfo
        trackInfoLayout: Plasmoid.configuration.panelTrackInfoLayout
        trackInfoFontFamily: FontPolicy.trackInfoFamily(FontCatalog,
                                                         Plasmoid.configuration.panelTrackInfoFontSameAsLyrics,
                                                         Plasmoid.configuration.panelTrackInfoFontFamily,
                                                         compactView.fontFamily,
                                                         Kirigami.Theme.defaultFont.family)
        trackInfoFontSize: Plasmoid.configuration.panelTrackInfoFontSize
        trackInfoFontWeight: FontPolicy.renderWeight(FontCatalog, compactView.trackInfoFontFamily,
                                                     Plasmoid.configuration.panelTrackInfoFontWeight)
        trackInfoColor: panelTheme.value("TrackInfoColor")
        trackInfoStrokeEnabled: panelTheme.value("TrackInfoStroke")
        trackInfoStrokeColor: panelTheme.value("TrackInfoStrokeColor")
        trackInfoOverflow: Plasmoid.configuration.panelTrackInfoOverflow
        // Colours fade only while the set in effect switches, never when one
        // of its own colours is edited.
        animateColors: panelTheme.transitioning
        colorTransitionMs: panelTheme.transitionMs
    }

    fullRepresentation: LyricsView {
        id: fullView
        source: lyricSource
        plateMode: desktopTheme.value("PlateMode")
        ownsPlate: root.plateSelfDrawn
        solidColor: desktopTheme.value("SolidColor")
        textColor: desktopTheme.value("TextColor")
        strokeEnabled: desktopTheme.value("Stroke")
        strokeColor: desktopTheme.value("StrokeColor")
        // Same resolution as compactRepresentation's, over the desktop set.
        fontFamily: FontPolicy.lyricFamily(FontCatalog,
                                           desktopTheme.value("FontFamily"),
                                           Kirigami.Theme.defaultFont.family)
        fontSize: desktopTheme.value("FontSize")
        fontWeight: FontPolicy.renderWeight(FontCatalog, fullView.fontFamily,
                                            desktopTheme.value("FontWeight"))
        overflowMode: desktopTheme.value("Overflow")
        animationMode: desktopTheme.value("Animation")
        showTranslation: desktopTheme.value("ShowTranslation")
        secondLineSource: desktopTheme.value("SecondLineSource")
        secondLineColorEnabled: desktopTheme.value("SecondLineColorEnabled")
        secondLineColor: desktopTheme.value("SecondLineColor")
        lineHeightPercent: desktopTheme.value("LineHeight")
        // DESIGN.md decision 69/73: the desktop form factor cannot go below
        // 125% -- below it, the previous line's descenders can reach into
        // the next line's ink. The panel (compactRepresentation above)
        // leaves this at LyricsView's own 100% default; its height is not
        // this widget's to grow, and at 100% it never had lift headroom to
        // begin with. Kept as a literal, matching AppearanceSection.qml's
        // lineHeightMin: 125 override in ConfigDesktopAppearance.qml rather
        // than sharing one constant -- DESIGN.md decision 69 has the reasoning.
        lineHeightMinPercent: 125
        wordByWord: desktopTheme.value("WordByWord")
        syntheticWordByWord: desktopTheme.value("WordByWordSynthetic")
        wordUnsungColor: desktopTheme.value("WordUnsungColor")
        wordActiveColor: desktopTheme.value("WordActiveColor")
        wordSungColor: desktopTheme.value("WordSungColor")
        wordLift: desktopTheme.value("WordLift")
        wordLiftPercent: desktopTheme.value("WordLiftPercent")
        wordBrightness: desktopTheme.value("WordBrightness")
        wordBrightnessPercent: desktopTheme.value("WordBrightnessPercent")
        wordBlurGlow: desktopTheme.value("WordBlurGlow")
        idleText: TextPolicy.effectiveText(Plasmoid.configuration.emptyTextUseDefault,
                                           Plasmoid.configuration.idleText,
                                           i18n("No media is playing"))
        notFoundText: TextPolicy.effectiveText(Plasmoid.configuration.emptyTextUseDefault,
                                               Plasmoid.configuration.notFoundText,
                                               i18n("Lyrics not found"))
        noLyricText: TextPolicy.effectiveText(Plasmoid.configuration.emptyTextUseDefault,
                                              Plasmoid.configuration.noLyricText,
                                              i18n("Instrumental track, enjoy the music"))
        networkErrorText: TextPolicy.effectiveText(Plasmoid.configuration.emptyTextUseDefault,
                                                   Plasmoid.configuration.networkErrorText,
                                                   i18n("Network error, cannot fetch lyrics"))
        panelMode: false
        showTrackInfo: Plasmoid.configuration.desktopShowTrackInfo
        trackInfoLayout: Plasmoid.configuration.desktopTrackInfoLayout
        trackInfoFontFamily: FontPolicy.trackInfoFamily(FontCatalog,
                                                         Plasmoid.configuration.desktopTrackInfoFontSameAsLyrics,
                                                         Plasmoid.configuration.desktopTrackInfoFontFamily,
                                                         fullView.fontFamily,
                                                         Kirigami.Theme.defaultFont.family)
        trackInfoFontSize: Plasmoid.configuration.desktopTrackInfoFontSize
        trackInfoFontWeight: FontPolicy.renderWeight(FontCatalog, fullView.trackInfoFontFamily,
                                                     Plasmoid.configuration.desktopTrackInfoFontWeight)
        trackInfoColor: desktopTheme.value("TrackInfoColor")
        trackInfoStrokeEnabled: desktopTheme.value("TrackInfoStroke")
        trackInfoStrokeColor: desktopTheme.value("TrackInfoStrokeColor")
        trackInfoOverflow: Plasmoid.configuration.desktopTrackInfoOverflow
        animateColors: desktopTheme.transitioning
        colorTransitionMs: desktopTheme.transitionMs
        shouldBeVisible: visibilityPolicy.shouldBeVisible
        animationsArmed: root.desktopAnimationsArmed
        hideAnimationMs: root.effectiveFadeMs
    }

    toolTipMainText: lyricSource.trackTitle.length > 0 ? lyricSource.trackTitle : i18n("Desktop Lyrics")
    toolTipSubText: lyricSource.trackArtists

    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: lyricSource.switchingProvider.length > 0
                ? i18n("Getting lyrics from %1…",
                       lyricSource.providerDisplayName(lyricSource.switchingProvider))
                : lyricSource.actualProvider.length > 0
                ? (lyricSource.temporaryFallback
                    ? i18n("Current lyrics: %1 (temporary fallback)",
                           lyricSource.providerDisplayName(lyricSource.actualProvider))
                    : i18n("Current lyrics: %1",
                           lyricSource.providerDisplayName(lyricSource.actualProvider)))
                : i18n("Current lyrics source: none")
            icon.name: "view-media-lyrics"
            enabled: false
        },
        PlasmaCore.Action {
            text: i18n("Lyrics source: Automatic")
            icon.name: "system-run"
            checkable: true
            checked: lyricSource.preferredProvider.length === 0
            enabled: lyricSource.canControlProvider
            onTriggered: lyricSource.clearPreferredProvider()
        },
        PlasmaCore.Action {
            text: i18n("Prefer local files for this song")
            icon.name: "folder-music-symbolic"
            checkable: true
            checked: lyricSource.preferredProvider === "local"
            visible: lyricSource.availableProviders.indexOf("local") >= 0
            enabled: lyricSource.canControlProvider
            onTriggered: lyricSource.setPreferredProvider("local")
        },
        PlasmaCore.Action {
            text: i18n("Prefer NetEase for this song")
            icon.name: "cloud"
            checkable: true
            checked: lyricSource.preferredProvider === "netease"
            visible: lyricSource.availableProviders.indexOf("netease") >= 0
            enabled: lyricSource.canControlProvider
            onTriggered: lyricSource.setPreferredProvider("netease")
        },
        PlasmaCore.Action {
            text: i18n("Prefer AMLL for this song")
            icon.name: "cloud"
            checkable: true
            checked: lyricSource.preferredProvider === "amll"
            visible: lyricSource.availableProviders.indexOf("amll") >= 0
            enabled: lyricSource.canControlProvider
            onTriggered: lyricSource.setPreferredProvider("amll")
        },
        PlasmaCore.Action {
            text: i18n("Prefer QQ Music for this song")
            icon.name: "cloud"
            checkable: true
            checked: lyricSource.preferredProvider === "qq"
            visible: lyricSource.availableProviders.indexOf("qq") >= 0
            enabled: lyricSource.canControlProvider
            onTriggered: lyricSource.setPreferredProvider("qq")
        },
        PlasmaCore.Action {
            text: i18n("Search for lyrics again")
            icon.name: "view-refresh"
            enabled: lyricSource.canControlProvider
            onTriggered: lyricSource.research()
        },
        PlasmaCore.Action {
            text: lyricSource.globalOffsetEnabled
                ? i18n("Lyrics 0.5 s earlier (all songs)")
                : i18n("Lyrics 0.5 s earlier")
            icon.name: "go-previous"
            enabled: lyricSource.canAdjustOffset
            onTriggered: lyricSource.adjustOffset(-500)
        },
        PlasmaCore.Action {
            text: lyricSource.globalOffsetEnabled
                ? i18n("Lyrics 0.5 s later (all songs)")
                : i18n("Lyrics 0.5 s later")
            icon.name: "go-next"
            enabled: lyricSource.canAdjustOffset
            onTriggered: lyricSource.adjustOffset(500)
        },
        PlasmaCore.Action {
            text: lyricSource.globalOffsetEnabled
                ? i18n("Reset global lyric offset (%1 ms)", lyricSource.offsetMs)
                : i18n("Reset lyric offset (%1 ms)", lyricSource.offsetMs)
            icon.name: "edit-undo"
            enabled: lyricSource.canAdjustOffset && lyricSource.offsetMs !== 0
            onTriggered: lyricSource.resetOffset()
        }
    ]
}
