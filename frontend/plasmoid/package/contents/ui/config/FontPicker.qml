pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

import "../FontPolicy.js" as FontPolicy

// A combo box whose popup is a searchable list of the installed families.
// The model stays empty on purpose: the desktop style sizes a ComboBox from
// the widest entry in its model, and with ~200 family names that would make
// this the widest control on the page. displayText is set directly instead,
// and the list lives in the popup, which sits in the window's overlay and so
// cannot widen the form at all.
//
// This only reports what the user picked; AppearanceSection turns that into
// config keys. Picking the "(not installed)" entry reports nothing, so a
// stored family that is missing stays stored until the user picks another.
QQC2.ComboBox {
    id: root

    // The FontCatalog singleton, or a stand-in with the same functions in
    // tests.
    required property var fontCatalog
    // <form>FontFamily, or <form>TrackInfoFontFamily for the track-info
    // picker: empty follows the Plasma font.
    property string storedFamily: ""
    // The track-info picker offers "Same as lyrics" as well; `sameAsLyrics`
    // is <form>TrackInfoFontSameAsLyrics and `lyricFamily` the family the
    // lyrics actually render in, which that entry is drawn in.
    property bool offerSameAsLyrics: false
    property bool sameAsLyrics: false
    property string lyricFamily: ""

    signal followSystemPicked()
    signal sameAsLyricsPicked()
    signal familyPicked(string family)

    readonly property alias searchField: searchField
    readonly property alias entryList: entryList

    readonly property string systemFamily: Kirigami.Theme.defaultFont.family
    // One of "same", "system", "missing" or "font": which entry the stored
    // configuration corresponds to.
    readonly property string currentKind: {
        if (root.offerSameAsLyrics && root.sameAsLyrics) {
            return "same";
        }
        if (!root.storedFamily) {
            return "system";
        }
        return FontPolicy.isMissing(root.fontCatalog, root.storedFamily) ? "missing" : "font";
    }
    // The listed name of the stored family when currentKind is "font": a
    // name saved under another locale is shown as this locale lists it.
    readonly property string currentFamily: root.currentKind === "font"
        ? root.fontCatalog.resolveFamily(root.storedFamily)
        : ""
    // What the closed box says, before the box elides it to its width.
    readonly property string currentLabel: root.entryText(root.currentKind,
        root.currentKind === "missing" ? root.storedFamily : root.currentFamily)

    function entryText(kind, family) {
        switch (kind) {
        case "same":
            return i18n("Same as lyrics");
        case "system":
            return i18n("Follow system font (%1)", root.systemFamily);
        case "missing":
            return i18n("%1 (not installed)", family);
        default:
            return family;
        }
    }

    // A missing family cannot be drawn in itself; it renders in the Plasma
    // font, which is also what the widget falls back to.
    function renderFamily(kind, family) {
        switch (kind) {
        case "same":
            return root.lyricFamily || root.systemFamily;
        case "font":
            return family;
        default:
            return root.systemFamily;
        }
    }

    function makeEntry(kind, family) {
        return {
            kind: kind,
            family: family,
            text: root.entryText(kind, family),
            renderFamily: root.renderFamily(kind, family)
        };
    }

    // Rebuilt imperatively on every open and every keystroke rather than
    // bound: the list is only ever looked at while the popup is open, and a
    // binding would re-run search() whenever any input changed with the
    // popup closed.
    function rebuild() {
        const query = searchField.text;
        // Whitespace-only counts as empty, the same rule search() applies,
        // so a stray space does not hide the pinned entries while still
        // listing every family.
        const filtering = query.trim().length > 0;
        const rows = [];
        if (!filtering) {
            if (root.offerSameAsLyrics) {
                rows.push(root.makeEntry("same", ""));
            }
            rows.push(root.makeEntry("system", ""));
            // Only while it is the value in effect: with "Same as lyrics"
            // on, a stored track-info family is ignored, missing or not.
            if (root.currentKind === "missing") {
                rows.push(root.makeEntry("missing", root.storedFamily));
            }
        }
        const pinned = rows.length;
        const found = root.fontCatalog.search(query);
        for (let i = 0; i < found.length; ++i) {
            rows.push(root.makeEntry("font", found[i]));
        }
        entryList.pinnedCount = pinned;
        entryList.entries = rows;

        let highlight = rows.length > 0 ? 0 : -1;
        if (!filtering) {
            highlight = rows.findIndex(entry => entry.kind === root.currentKind
                && (entry.kind !== "font" || entry.family === root.currentFamily));
        }
        entryList.currentIndex = highlight;
    }

    function moveHighlight(delta) {
        if (entryList.count === 0) {
            return;
        }
        const next = Math.max(0, Math.min(entryList.count - 1, entryList.currentIndex + delta));
        entryList.currentIndex = next;
        entryList.positionViewAtIndex(next, ListView.Contain);
    }

    function pick(entry) {
        fontPopup.close();
        switch (entry.kind) {
        case "same":
            root.sameAsLyricsPicked();
            break;
        case "system":
            root.followSystemPicked();
            break;
        case "font":
            root.familyPicked(entry.family);
            break;
        }
    }

    function pickHighlighted() {
        if (entryList.currentIndex >= 0 && entryList.currentIndex < entryList.count) {
            root.pick(entryList.entries[entryList.currentIndex]);
        }
    }

    model: []
    // Nothing to step through with the wheel, and accepting the event would
    // stop the page from scrolling while the pointer is over this control.
    wheelEnabled: false
    // The closed box shows the current choice in the family it stands for.
    font.family: root.renderFamily(root.currentKind, root.currentFamily)
    // Drawn by the field below rather than by the style. org.kde.desktop,
    // the style plasmashell's config dialog uses, paints displayText from
    // the combo box's background in the UI font whatever `font` says, and
    // clips rather than elides it; left empty, it paints no text at all, and
    // the accessible name is set explicitly instead.
    displayText: ""
    Accessible.name: root.currentLabel

    // A read-only TextField, the same type every style's own contentItem
    // is: org.kde.desktop's text handles call TextInput functions on
    // whatever the contentItem is, and log a TypeError for anything else.
    // TextInput cannot elide, so the text is elided up front, measured in
    // the same font it is drawn in.
    contentItem: T.TextField {
        id: closedField
        objectName: "closedLabel"
        // Zero, so the box's implicit size comes from the style and never
        // from the name or the chosen family's line height; the layout
        // sizes it.
        implicitWidth: 0
        implicitHeight: 0
        padding: 0
        // The same indent as the style's own text field, read off
        // styleReference below, so the text lines up with the plain combo
        // boxes around this one: org.kde.desktop indents by the box's
        // padding alone (0 here), org.kde.breeze and Fusion also by their
        // field's leftPadding.
        leftPadding: (styleReference.contentItem as TextInput)?.leftPadding ?? 0
        // org.kde.desktop draws the drop-down arrow inside the content area
        // and has no indicator item; styles that have one (org.kde.breeze,
        // Fusion) already leave room for it in the box's rightPadding.
        rightPadding: root.indicator && root.indicator.width > 0
            ? (styleReference.contentItem as TextInput)?.rightPadding ?? 0
            : Kirigami.Units.gridUnit + Kirigami.Units.smallSpacing
        // Not editable and never focused; presses go to the box.
        enabled: false
        readOnly: true
        text: closedMetrics.elidedText
        font: root.font
        // The box's theme, not this field's: Kirigami gives a disabled
        // item the disabled text colour.
        color: root.enabled ? root.Kirigami.Theme.textColor : root.Kirigami.Theme.disabledTextColor
        verticalAlignment: Text.AlignVCenter
        background: null

        TextMetrics {
            id: closedMetrics
            font: closedField.font
            text: root.currentLabel
            elide: Text.ElideRight
            elideWidth: Math.max(0, closedField.width - closedField.leftPadding - closedField.rightPadding)
        }
    }

    // A plain combo box of the same style, never shown, whose own text
    // field says how far this style indents a combo box's text.
    QQC2.ComboBox {
        id: styleReference
        visible: false
        model: []
    }

    popup: QQC2.Popup {
        id: fontPopup

        y: root.height
        width: Math.max(root.width, Kirigami.Units.gridUnit * 20)
        // Keeps the popup inside the dialog; it flips above the box when
        // there is no room below.
        margins: Kirigami.Units.smallSpacing
        padding: Kirigami.Units.smallSpacing
        // The same colours as the rows, so the empty "No matching fonts"
        // state does not show a different background.
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false
        focus: true

        onAboutToShow: {
            searchField.clear();
            root.rebuild();
            // Deferred until the popup is visible and laid out: before
            // that the list has no height to centre anything in.
            Qt.callLater(() => entryList.positionViewAtIndex(Math.max(0, entryList.currentIndex), ListView.Center));
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.SearchField {
                id: searchField
                Layout.fillWidth: true
                focus: true
                placeholderText: i18n("Search fonts")
                onTextChanged: root.rebuild()
                // Focus stays here, so the list is driven from the field.
                Keys.onUpPressed: root.moveHighlight(-1)
                Keys.onDownPressed: root.moveHighlight(1)
                Keys.onReturnPressed: root.pickHighlighted()
                Keys.onEnterPressed: root.pickHighlighted()
                Keys.onEscapePressed: fontPopup.close()
            }

            Item {
                Layout.fillWidth: true
                // A bounded height is what keeps the ListView creating
                // delegates only for the rows in view.
                Layout.preferredHeight: Kirigami.Units.gridUnit * 16

                QQC2.ScrollView {
                    anchors.fill: parent

                    ListView {
                        id: entryList

                        property var entries: []
                        // How many rows at the top are pinned entries
                        // rather than families; the last one draws a
                        // separator.
                        property int pinnedCount: 0

                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: entryList.entries

                        delegate: QQC2.ItemDelegate {
                            id: entryDelegate

                            required property var modelData
                            required property int index

                            width: ListView.view.width
                            // Not drawn, the contentItem below is; this is
                            // the row's name for screen readers.
                            text: entryDelegate.modelData.text
                            highlighted: ListView.isCurrentItem
                            onClicked: root.pick(entryDelegate.modelData)

                            contentItem: Item {
                                implicitHeight: Math.max(nameLabel.implicitHeight, alternatesLabel.implicitHeight)

                                QQC2.Label {
                                    id: nameLabel
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    // The name keeps its full width whenever
                                    // it fits; the other names get what is
                                    // left.
                                    width: Math.min(implicitWidth, parent.width)
                                    text: entryDelegate.modelData.text
                                    font.family: entryDelegate.modelData.renderFamily
                                    elide: Text.ElideRight
                                    color: entryDelegate.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                                }
                                QQC2.Label {
                                    id: alternatesLabel
                                    anchors.left: nameLabel.right
                                    anchors.leftMargin: Kirigami.Units.largeSpacing
                                    anchors.right: parent.right
                                    anchors.baseline: nameLabel.baseline
                                    visible: text.length > 0 && width > 0
                                    text: entryDelegate.modelData.kind === "font"
                                        ? root.fontCatalog.alternateNames(entryDelegate.modelData.family).join(", ")
                                        : ""
                                    elide: Text.ElideRight
                                    // Same styling as the formDescription
                                    // labels in AppearanceSection.
                                    color: entryDelegate.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                                    font: Kirigami.Theme.smallFont
                                }
                            }

                            Kirigami.Separator {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                visible: entryDelegate.index === entryList.pinnedCount - 1
                            }
                        }
                    }
                }

                Kirigami.PlaceholderMessage {
                    objectName: "noMatchMessage"
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 2
                    visible: entryList.count === 0
                    text: i18n("No matching fonts")
                }
            }
        }
    }
}
