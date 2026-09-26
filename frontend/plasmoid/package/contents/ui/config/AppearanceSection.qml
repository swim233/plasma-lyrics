import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "../FontPolicy.js" as FontPolicy
import "../ThemePolicy.js" as ThemePolicy

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
    // The FontCatalog singleton, passed in by the page rather than imported
    // here so the tests can hand this a stand-in whose families do not
    // depend on what the machine running them has installed.
    required property var fontCatalog
    property string fontFamily: ""
    property int fontWeight: Font.Normal
    property string overflowMode: "fit"
    property string animationMode: "slide"
    property int lineHeightPercent: 125
    // Floor for the SpinBox below, not for lineHeightPercent itself: the
    // desktop and panel config pages share this one component, and only the
    // desktop page raises it (see ConfigDesktopAppearance.qml). Left at
    // 100 -- the panel's own floor -- rather than doubling as some kind of
    // shared default, since the panel page never sets it at all.
    property int lineHeightMin: 100
    required property var fontSizeControl

    // DESIGN.md decision 78: the secondary lyrics, a section of their own.
    // The *Default properties are the defaults of the keys of the set on
    // screen (the page reads them from the config dialog, see
    // ConfigDesktopAppearance.qml), which the two switches compare against
    // when they are turned on.
    property string secondaryLyricSource: "translation"
    property bool secondaryLyricColorEnabled: false
    property string secondaryLyricColor: "#adfffaf5"
    property string secondaryLyricColorDefault
    property bool secondaryLyricFontEnabled: false
    property string secondaryLyricFontFamily: ""
    property int secondaryLyricFontSize: 34
    property int secondaryLyricFontWeight: Font.Normal
    property bool secondaryLyricFontItalic: false
    property string secondaryLyricFontFamilyDefault
    property int secondaryLyricFontSizeDefault
    property int secondaryLyricFontWeightDefault
    property bool secondaryLyricFontItalicDefault
    readonly property bool secondaryLyricShown: root.secondaryLyricSource !== "none"

    // The panel has no lift keys at all, so the row below stands there
    // disabled rather than pretending to store anything.
    property bool liftSupported: true
    property bool wordByWord: true
    // DESIGN.md's synthetic word-by-word decision. Read-only pass-through
    // like the other word-mode properties on this component; the page below
    // owns the actual cfg_ binding.
    property bool syntheticWordByWord: false
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
    property bool wordParticles: true
    property bool wordParticleColorEnabled: false
    property string wordParticleColor: "#fffaf5"

    // DESIGN.md decision 76: the track info's colours come in a light and a
    // dark copy like every row above, so their three rows close this form;
    // TrackInfoSection holds the rest, which both sets share. showTrackInfo
    // hides these three, heading included, along with TrackInfoSection's.
    property bool showTrackInfo: true
    property string trackInfoColor: "#b3fffaf5"
    property bool trackInfoStrokeEnabled: false
    property string trackInfoStrokeColor: "#cc000000"
    // Picking a lyric font also writes the track-info weight when the track
    // info follows the lyric font, so editLyricFamily needs these three and
    // trackInfoFontWeightEdited below. `setInEffect` is whether these rows
    // show the set the widget renders with: the track info follows the
    // lyric font of that set only.
    property bool trackInfoFontSameAsLyrics: true
    property int trackInfoFontWeight: Font.Normal
    property bool setInEffect: true

    signal plateModeEdited(string value)
    signal solidColorEdited(string value)
    signal textColorEdited(string value)
    signal strokeEnabledEdited(bool value)
    signal strokeColorEdited(string value)
    signal fontFamilyEdited(string value)
    signal fontWeightEdited(int value)
    signal overflowModeEdited(string value)
    signal animationModeEdited(string value)
    signal lineHeightPercentEdited(int value)
    signal secondaryLyricSourceEdited(string value)
    signal secondaryLyricColorEnabledEdited(bool value)
    signal secondaryLyricColorEdited(string value)
    signal secondaryLyricFontEnabledEdited(bool value)
    signal secondaryLyricFontFamilyEdited(string value)
    signal secondaryLyricFontSizeEdited(int value)
    signal secondaryLyricFontWeightEdited(int value)
    signal secondaryLyricFontItalicEdited(bool value)

    signal wordByWordEdited(bool value)
    signal syntheticWordByWordEdited(bool value)
    signal wordUnsungColorEdited(string value)
    signal wordActiveColorEdited(string value)
    signal wordSungColorEdited(string value)
    signal wordLiftEdited(bool value)
    signal wordLiftPercentEdited(int value)
    signal wordBrightnessEdited(bool value)
    signal wordBrightnessPercentEdited(int value)
    signal wordBlurGlowEdited(bool value)
    signal wordParticlesEdited(bool value)
    signal wordParticleColorEnabledEdited(bool value)
    signal wordParticleColorEdited(string value)

    signal trackInfoColorEdited(string value)
    signal trackInfoStrokeEnabledEdited(bool value)
    signal trackInfoStrokeColorEdited(string value)
    signal trackInfoFontWeightEdited(int value)

    // What the lyrics render in: FontPolicy.lyricFamily() of the same key,
    // the call main.qml makes, so the weight row below lists the faces
    // actually drawn. Computed by the page and passed in, the same remedy as
    // wordLiftRowVisible above. Computed here from fontFamily instead, it
    // loops, and so does TrackInfoSection's trackInfoEffectiveFamily,
    // measured when both sections were one: tst_appearance.qml reported 10
    // "Binding loop" warnings on trackInfoEffectiveFamily, and
    // tst_fontpicker.qml's test_pagesCreatedWithStoredFontKeys, which
    // creates the pages the way the config dialog does, 6 on
    // lyricEffectiveFamily and 10 on trackInfoEffectiveFamily. Computed by
    // the page: 0 in both files. The loops appeared only for a stored value
    // that differs from the default declared in the component: flipping
    // trackInfoFontSameAsLyrics' default to false silenced tst_appearance.qml's
    // 10, which is why test_pagesCreatedWithStoredFontKeys passes stored
    // values, not none.
    required property string lyricEffectiveFamily
    // What the secondary lyrics render in while their font switch is on:
    // FontPolicy.lyricFamily() of their own family key, computed by the page
    // for the same reason.
    required property string secondaryLyricEffectiveFamily
    readonly property string systemFamily: Kirigami.Theme.defaultFont.family

    // A pick that moves the lyrics onto another family also writes their
    // weight, snapped onto one of the new family's real faces, so the stored
    // weight is always one the family has -- and the track-info weight too
    // while the track info renders in that family, which is while it follows
    // the lyric font and these rows are the set in effect. The track-info
    // weight is one key for both sets (DESIGN.md decision 76), so a pick on
    // the other set leaves it for the renderer to snap, as it does for any
    // family. Only from this handler and TrackInfoSection's
    // editTrackInfoFont, never from a binding: the weight rows merely display
    // the snapped value, for the reason lineHeightSpinBox's comment gives. It
    // captures what it compares against before emitting, since each emit
    // flows back into this component's properties through the page.
    function editLyricFamily(stored) {
        const before = root.lyricEffectiveFamily;
        const after = FontPolicy.lyricFamily(root.fontCatalog, stored, root.systemFamily);
        const trackInfoFollows = root.trackInfoFontSameAsLyrics && root.setInEffect;
        const lyricWeight = root.fontWeight;
        const trackInfoWeight = root.trackInfoFontWeight;
        root.fontFamilyEdited(stored);
        if (after === before) {
            return;
        }
        root.fontWeightEdited(FontPolicy.renderWeight(root.fontCatalog, after, lyricWeight));
        if (trackInfoFollows) {
            root.trackInfoFontWeightEdited(FontPolicy.renderWeight(root.fontCatalog, after, trackInfoWeight));
        }
    }

    // Turning the colour switch on over a colour still at its key's default
    // copies the text colour in, at the alpha the secondary lyrics take from
    // it while the switch is off (decision 78), so nothing on screen moves.
    // A colour set before is kept, so turning the switch off and on again
    // brings it back. A text colour that is no colour at all, as a
    // configuration edited by hand can hold, makes Qt.color() throw: then
    // nothing is copied, and the switch is written all the same, so the
    // check box never shows a state the configuration does not have. Only
    // from the check box, never from a binding.
    function editSecondaryLyricColorEnabled(enabled) {
        if (enabled && ThemePolicy.isDefaultValue(root.secondaryLyricColor, root.secondaryLyricColorDefault)) {
            let copy = "";
            try {
                const text = Qt.color(root.textColor);
                copy = Qt.rgba(text.r, text.g, text.b, 0xad / 255).toString();
            } catch (error) {
                copy = "";
            }
            if (copy.length > 0) {
                root.secondaryLyricColorEdited(copy);
            }
        }
        root.secondaryLyricColorEnabledEdited(enabled);
    }

    // The same for the font switch (decision 78): turned on while family,
    // size, weight and italic are all still at their keys' defaults, it
    // copies the main lyrics' family as stored, their size and their weight
    // in, so the preview does not move -- italic stays off. Values set
    // before are kept, so turning the switch off and on again brings them
    // back.
    function editSecondaryLyricFontEnabled(enabled) {
        const untouched = root.secondaryLyricFontFamily === root.secondaryLyricFontFamilyDefault
            && root.secondaryLyricFontSize === root.secondaryLyricFontSizeDefault
            && root.secondaryLyricFontWeight === root.secondaryLyricFontWeightDefault
            && root.secondaryLyricFontItalic === root.secondaryLyricFontItalicDefault;
        if (enabled && untouched) {
            const family = root.fontFamily;
            const size = root.fontSizeControl.value;
            const weight = root.fontWeight;
            root.secondaryLyricFontFamilyEdited(family);
            root.secondaryLyricFontSizeEdited(size);
            root.secondaryLyricFontWeightEdited(weight);
        }
        root.secondaryLyricFontEnabledEdited(enabled);
    }

    // A pick that moves the secondary lyrics onto another family writes
    // their weight too, snapped onto the new family's faces in the slant
    // drawn, as editLyricFamily does for the main lyrics. Turning italic on
    // or off moves no family, so it writes no weight: the weight row and the
    // renderer snap the stored one.
    function editSecondaryLyricFamily(stored) {
        const before = root.secondaryLyricEffectiveFamily;
        const after = FontPolicy.lyricFamily(root.fontCatalog, stored, root.systemFamily);
        const weight = root.secondaryLyricFontWeight;
        const italic = root.secondaryLyricFontItalic;
        root.secondaryLyricFontFamilyEdited(stored);
        if (after === before) {
            return;
        }
        root.secondaryLyricFontWeightEdited(FontPolicy.renderWeight(root.fontCatalog, after, weight, italic));
    }

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
    FontPicker {
        objectName: "lyricFontPicker"
        Kirigami.FormData.label: i18n("Font:")
        // Fills the column up to a cap rather than sizing to its text, the
        // same mechanism formDescription below uses, so no family name can
        // become what the page sizes itself to. 20 gridUnits fits "Follow
        // system font (Noto Sans CJK SC)" in English and Chinese with that
        // font at 12 pt, and leaves the page width as it was (measured under
        // org.kde.desktop: 894 px English, 592 px Chinese, with the pickers
        // and without).
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 20
        fontCatalog: root.fontCatalog
        storedFamily: root.fontFamily
        onFollowSystemPicked: root.editLyricFamily("")
        onFamilyPicked: family => root.editLyricFamily(family)
    }
    QQC2.SpinBox {
        Kirigami.FormData.label: i18n("Font size:")
        from: 10
        to: 96
        value: root.fontSizeControl.value
        onValueModified: root.fontSizeControl.value = value
    }
    WeightComboBox {
        objectName: "lyricWeightComboBox"
        Kirigami.FormData.label: i18n("Font weight:")
        fontCatalog: root.fontCatalog
        family: root.lyricEffectiveFamily
        storedWeight: root.fontWeight
        onWeightPicked: weight => root.fontWeightEdited(weight)
    }
    QQC2.SpinBox {
        objectName: "lineHeightSpinBox"
        Kirigami.FormData.label: i18n("Line height:")
        // A stored value below `from` on the desktop page (any value saved
        // before this floor existed) is NOT rejected or migrated: `value` is
        // a one-way binding from the stored cfg_desktopLineHeight, and QQC2
        // only clamps what it *displays* -- that clamped 125 never writes
        // back to cfg_desktopLineHeight (onValueModified fires only on
        // actual user interaction, never from this binding alone), and the
        // shell's saveConfig() writes whatever cfg_desktopLineHeight still
        // holds. So a stale value below 125 stays in the config file
        // indefinitely, through any number of dialog opens and saves, until
        // the user actually drags/types/arrows this control themselves.
        // Rendering is unaffected regardless -- LyricsView's own
        // Math.max(lineHeightMinPercent, lineHeightPercent) clamp applies
        // independently of what is stored.
        from: root.lineHeightMin
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
    // Above the secondary lyrics section: its heading would otherwise take
    // these two in, and both apply to the main lyrics as well.
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

    // DESIGN.md decision 78. Every row after the first shows only while the
    // secondary lyrics do. The rows are named for the tests that check which
    // of them show.
    Kirigami.Separator {
        objectName: "secondaryLyricSeparator"
        Kirigami.FormData.isSection: true
        Kirigami.FormData.label: i18n("Secondary lyrics")
    }
    QQC2.ComboBox {
        objectName: "secondaryLyricSourceComboBox"
        Kirigami.FormData.label: i18n("Secondary lyrics:")
        // "None" with a context of its own: decision 78 words it 不显示,
        // where the background and line transition rows' "None" is 无.
        model: [i18n("Translation"), i18n("Romanization"),
                i18nc("@item:inlistbox secondary lyrics source", "None")]
        // What LyricsView shows for each value, an unknown one included.
        currentIndex: root.secondaryLyricSource === "none"
            ? 2
            : (root.secondaryLyricSource === "romanization" ? 1 : 0)
        onActivated: index => root.secondaryLyricSourceEdited(["translation", "romanization", "none"][index])
    }
    QQC2.CheckBox {
        objectName: "secondaryLyricColorCheckBox"
        Kirigami.FormData.label: i18n("Secondary lyrics color:")
        visible: root.secondaryLyricShown
        text: i18n("Set it separately from the main lyrics color")
        checked: root.secondaryLyricColorEnabled
        onToggled: root.editSecondaryLyricColorEnabled(checked)
    }
    ColorField {
        objectName: "secondaryLyricColorField"
        Kirigami.FormData.label: i18n("Color:")
        visible: root.secondaryLyricShown && root.secondaryLyricColorEnabled
        value: root.secondaryLyricColor
        onEdited: hexColor => root.secondaryLyricColorEdited(hexColor)
    }
    QQC2.CheckBox {
        objectName: "secondaryLyricFontCheckBox"
        Kirigami.FormData.label: i18n("Secondary lyrics font:")
        visible: root.secondaryLyricShown
        text: i18n("Set it separately from the main lyrics")
        checked: root.secondaryLyricFontEnabled
        onToggled: root.editSecondaryLyricFontEnabled(checked)
    }
    FontPicker {
        objectName: "secondaryLyricFontPicker"
        Kirigami.FormData.label: i18n("Font:")
        visible: root.secondaryLyricShown && root.secondaryLyricFontEnabled
        // The same cap as lyricFontPicker's.
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 20
        fontCatalog: root.fontCatalog
        storedFamily: root.secondaryLyricFontFamily
        onFollowSystemPicked: root.editSecondaryLyricFamily("")
        onFamilyPicked: family => root.editSecondaryLyricFamily(family)
    }
    QQC2.SpinBox {
        objectName: "secondaryLyricFontSizeSpinBox"
        Kirigami.FormData.label: i18n("Font size:")
        visible: root.secondaryLyricShown && root.secondaryLyricFontEnabled
        from: 10
        to: 96
        value: root.secondaryLyricFontSize
        onValueModified: root.secondaryLyricFontSizeEdited(value)
    }
    WeightComboBox {
        objectName: "secondaryLyricWeightComboBox"
        Kirigami.FormData.label: i18n("Font weight:")
        visible: root.secondaryLyricShown && root.secondaryLyricFontEnabled
        fontCatalog: root.fontCatalog
        family: root.secondaryLyricEffectiveFamily
        storedWeight: root.secondaryLyricFontWeight
        italic: root.secondaryLyricFontItalic
        onWeightPicked: weight => root.secondaryLyricFontWeightEdited(weight)
    }
    QQC2.CheckBox {
        objectName: "secondaryLyricItalicCheckBox"
        Kirigami.FormData.label: i18n("Italic:")
        visible: root.secondaryLyricShown && root.secondaryLyricFontEnabled
        checked: root.secondaryLyricFontItalic
        onToggled: root.secondaryLyricFontItalicEdited(checked)
    }

    Kirigami.Separator {
        Kirigami.FormData.isSection: true
        Kirigami.FormData.label: i18n("Word-by-word")
    }
    QQC2.CheckBox {
        Kirigami.FormData.label: i18n("Word-by-word lyrics:")
        text: i18n("Highlight each word as it is sung")
        checked: root.wordByWord
        onToggled: root.wordByWordEdited(checked)
    }
    QQC2.Label {
        // Named so the regression test can find these descriptions without a
        // shape check that would also match every other Label on the page,
        // the same reason LyricLine.qml's word glyphs carry one.
        objectName: "formDescription"
        Layout.fillWidth: true
        // A wrapping Text still reports its *unwrapped* single-line width as
        // implicitWidth, and Layout.fillWidth does not cap that -- it only
        // lets the item grow. FormLayout then sizes itself to the widest
        // child's preferred width, so a long enough sentence here silently
        // widens the whole config page. Measured in tst_appearance's English
        // with this label uncapped: it wants 722 px and the form's
        // implicitWidth goes 592 -> 846, too wide for two columns in the
        // test's 702 px. Layout.preferredWidth: 0 does NOT help (still 846);
        // only an explicit cap does. 26 gridUnits bounds how wide any
        // description can make the field column. A description can still be
        // the widest item there -- measured, the first one is in Chinese, and
        // in English with Plasma's default fonts the capped ones are -- so a
        // wording change can move the page width, but never past the cap.
        // Why 26: Plasma's applet config dialog is gridUnit * 45 = 810 px
        // wide and does not grow with its content, which leaves this section
        // 614 px, and FormLayout drops to one column once the section's
        // implicitWidth exceeds that. Measured in zh_CN with Noto Sans CJK SC
        // 12 (small font 11) under org.kde.desktop: the section is 588 px at
        // 26 and 615 px -- one column -- at 28, and at 26 the glow (465 px)
        // and particle (435 px) descriptions each fit on one line.
        // English is now the closer of the two: in English with Plasma's
        // default font, Noto Sans 10, under org.kde.desktop (the kde platform
        // theme reading a kdeglobals that sets only that font), the section
        // is 601 px without decision 78's secondary lyrics rows and 612 px
        // with all of them shown -- 2 px from one column. Their Chinese
        // labels left the width as it was. A longer English label or check
        // box text on this page is likely to tip it into one column.
        Layout.maximumWidth: Kirigami.Units.gridUnit * 26
        wrapMode: Text.WordWrap
        // Same styling as the "Record debug details" description on the
        // Lyrics Service page: this is secondary copy about the checkbox
        // above it, not a control label.
        color: Kirigami.Theme.disabledTextColor
        font: Kirigami.Theme.smallFont
        // Earlier wordings ("Used only when the lyrics source provides word
        // timings.", then a pointer at "Simulate timing" below) described
        // when the effect runs; this one names what the user has to do to
        // get it -- pick a source that carries word timings.
        text: i18n("Turn this on for word-by-word lyrics. The lyrics source has to support it (QQ Music and AMLL are the recommended first choices).")
    }
    QQC2.CheckBox {
        Kirigami.FormData.label: i18n("Simulate timing:")
        visible: root.wordByWord
        enabled: root.wordByWord
        checked: root.syntheticWordByWord
        text: i18n("Turn on simulated word-by-word for lyrics sources that do not support it")
        onToggled: root.syntheticWordByWordEdited(checked)
    }
    QQC2.Label {
        // Named so the regression test can find these descriptions without a
        // shape check that would also match every other Label on the page,
        // the same reason LyricLine.qml's word glyphs carry one.
        objectName: "formDescription"
        Layout.fillWidth: true
        // Capped as the first formDescription is; its comment has the
        // measurements behind the cap.
        Layout.maximumWidth: Kirigami.Units.gridUnit * 26
        wrapMode: Text.WordWrap
        visible: root.wordByWord
        // Same styling as the "Record debug details" description on the
        // Lyrics Service page, as above.
        color: Kirigami.Theme.disabledTextColor
        font: Kirigami.Theme.smallFont
        text: i18n("With this on, the word-by-word effect is simulated from the line timings, so it is less accurate.")
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
        text: i18n("Turn on lyric glow")
        checked: root.wordBlurGlow
        onToggled: root.wordBlurGlowEdited(checked)
    }
    QQC2.Label {
        // Named, capped and styled like the formDescription labels above;
        // the first one's comment has the measurements behind the cap.
        objectName: "formDescription"
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 26
        wrapMode: Text.WordWrap
        visible: root.wordByWord
        color: Kirigami.Theme.disabledTextColor
        font: Kirigami.Theme.smallFont
        text: i18n("With this on, a glow is overlaid on word-by-word lyrics for a more elegant look, at a slight performance cost.")
    }
    // DESIGN.md decision 77. The three control rows are named for the tests
    // that check which of them show.
    QQC2.CheckBox {
        objectName: "wordParticlesCheckBox"
        Kirigami.FormData.label: i18n("Particles:")
        visible: root.wordByWord
        text: i18n("Turn on lyric particle animation")
        checked: root.wordParticles
        onToggled: root.wordParticlesEdited(checked)
    }
    QQC2.Label {
        // As the glow's description above. Shown with its row, so it stays
        // while the particles themselves are off.
        objectName: "formDescription"
        Layout.fillWidth: true
        Layout.maximumWidth: Kirigami.Units.gridUnit * 26
        wrapMode: Text.WordWrap
        visible: root.wordByWord
        color: Kirigami.Theme.disabledTextColor
        font: Kirigami.Theme.smallFont
        text: i18n("With this on, particles show the live progress of word-by-word lyrics, at a slight performance cost.")
    }
    QQC2.CheckBox {
        objectName: "wordParticleColorCheckBox"
        Kirigami.FormData.label: i18n("Particle color:")
        visible: root.wordByWord && root.wordParticles
        text: i18n("Use a separate particle color")
        checked: root.wordParticleColorEnabled
        onToggled: root.wordParticleColorEnabledEdited(checked)
    }
    ColorField {
        objectName: "wordParticleColorField"
        Kirigami.FormData.label: i18n("Color:")
        // The particles are drawn opaque whatever this holds.
        alphaEnabled: false
        visible: root.wordByWord && root.wordParticles && root.wordParticleColorEnabled
        value: root.wordParticleColor
        onEdited: hexColor => root.wordParticleColorEdited(hexColor)
    }

    Kirigami.Separator {
        objectName: "trackInfoColorsSeparator"
        Kirigami.FormData.isSection: true
        Kirigami.FormData.label: i18n("Track info colors")
        visible: root.showTrackInfo
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
}
