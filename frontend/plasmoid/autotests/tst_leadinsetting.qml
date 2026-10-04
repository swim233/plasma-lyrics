import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import QtTest
import org.kde.kirigami as Kirigami
import "../package/contents/ui/config" as LyricsConfig

// DESIGN.md decision 81 on the config pages: the desktop page's "Behavior"
// section opens with the next line lead, and the panel page keeps its
// "Auto-hide" section as it was. The helpers are tst_appearance.qml's,
// trimmed to what these tests use.
//
// Every top-level object below is a Component, for the load-time reason
// tst_appearance.qml's header gives.
TestCase {
    name: "LeadInSetting"
    when: windowShown

    Component {
        id: desktopPageComponent
        LyricsConfig.ConfigDesktopAppearance {}
    }

    Component {
        id: panelPageComponent
        LyricsConfig.ConfigPanelAppearance {}
    }

    // A shown window for a page: Item.visible is ancestor-combined, and a
    // click or a key needs one. `overlay` is for applicationWindow() below.
    Component {
        id: pageWindowComponent
        Window {
            readonly property Item overlay: QQC2.Overlay.overlay
            width: Kirigami.Units.gridUnit * 38
            height: Kirigami.Units.gridUnit * 34
            visible: true
        }
    }

    // What a Label given Kirigami.Theme.smallFont renders in, for the
    // description's font; tst_appearance.qml's
    // test_descriptionsAreStyledAsSecondaryCopy has why it takes a Label.
    Component {
        id: smallFontLabelComponent
        QQC2.Label {
            font: Kirigami.Theme.smallFont
        }
    }

    // The theme tabs' sync dialog parents itself to applicationWindow()'s
    // overlay; see tst_appearance.qml.
    property Window dialogWindow: null

    function applicationWindow() {
        return dialogWindow || { overlay: null };
    }

    function findAll(item, predicate, found) {
        const out = found || [];
        const kids = item ? (item.children || []) : [];
        for (let i = 0; i < kids.length; ++i) {
            if (predicate(kids[i])) {
                out.push(kids[i]);
            }
            findAll(kids[i], predicate, out);
        }
        return out;
    }

    function named(root, objectName) {
        const found = findAll(root, o => o.objectName === objectName);
        compare(found.length, 1, objectName);
        return found[0];
    }

    // The page pinned to the dark set, so that it does not read the Plasma
    // style of the machine running the suite.
    function createWindowedPage(component, form, properties) {
        const win = createTemporaryObject(pageWindowComponent, this);
        verify(win !== null);
        dialogWindow = win;
        const props = Object.assign({ width: win.width, height: win.height, styleDark: false }, properties || {});
        props["cfg_" + form + "ThemeMode"] = "dark";
        const page = createTemporaryObject(component, win.contentItem, props);
        verify(page !== null, form);
        tryVerify(() => page.visible);
        return page;
    }

    // Waits until `item` stops moving: FormLayout lays its rows out from
    // zero-interval timers.
    function settle(item) {
        let last = "";
        tryVerify(() => {
            const corner = item.mapToItem(null, 0, 0);
            const now = [corner.x, corner.y, item.width, item.height].join(",");
            const unchanged = now === last;
            last = now;
            return unchanged;
        });
    }

    function scrollIntoView(page, item) {
        const flickable = page.flickable;
        const top = item.mapToItem(flickable.contentItem, 0, 0).y;
        flickable.contentY = Math.max(0, Math.min(top - Kirigami.Units.gridUnit * 2,
            flickable.contentHeight - flickable.height));
        settle(item);
    }

    // The children of the FormLayout `item` is a row of, as an array: its
    // rows in declaration order, after the layout items the form adds.
    function rowsBeside(item) {
        const kids = item.parent.children;
        const rows = [];
        for (let i = 0; i < kids.length; ++i) {
            rows.push(kids[i]);
        }
        return rows;
    }

    // The section headings of a page, in declaration order.
    function sectionHeadings(page) {
        return findAll(page, o => o.Kirigami.FormData.isSection).map(o => o.Kirigami.FormData.label);
    }

    // The heading, then the lead with its description, then the four
    // auto-hide rows. The lead shows whether auto-hide is on or not, reads
    // the stored value and writes what a step makes of it.
    function test_theBehaviorSectionOpensWithTheLead() {
        const page = createWindowedPage(desktopPageComponent, "desktop",
            { cfg_desktopLeadInMs: 1500, cfg_desktopAutoHide: false });
        const heading = named(page, "behaviorSeparator");
        const spinBox = named(page, "leadInSpinBox");
        verify(heading.Kirigami.FormData.isSection);
        compare(heading.Kirigami.FormData.label, i18n("Behavior"));
        verify(sectionHeadings(page).indexOf(i18n("Auto-hide")) < 0);

        const rows = rowsBeside(heading);
        const at = rows.indexOf(heading);
        verify(at >= 0);
        const description = rows[at + 2];
        verify(rows[at + 1] === spinBox);
        compare(description.objectName, "formDescription");
        compare(rows.slice(at + 3).map(row => row.Kirigami.FormData.label),
            [i18n("Auto-hide:"), i18n("Delay:"), i18n("Fade duration:"), i18n("Non-music media:")]);

        compare(spinBox.Kirigami.FormData.label, i18n("Next line lead:"));
        compare([spinBox.from, spinBox.to, spinBox.stepSize], [0, 5000, 100]);
        compare(spinBox.textFromValue(1500, Qt.locale()), i18n("%1 ms", 1500));
        compare(spinBox.value, 1500);
        compare(description.text, i18n("The next line becomes the current line up to this long before it is sung. If the gap between lines is shorter, it moves up as soon as the previous line ends. At 0 ms it moves up when it starts."));

        const delay = rows[at + 4];
        compare([spinBox.visible, description.visible, delay.visible], [true, true, false]);
        page.cfg_desktopAutoHide = true;
        compare([spinBox.visible, description.visible, delay.visible], [true, true, true]);
        page.cfg_desktopLeadInMs = 0;
        compare(spinBox.value, 0);
        page.cfg_desktopLeadInMs = 1500;

        settle(spinBox);
        scrollIntoView(page, spinBox);
        spinBox.forceActiveFocus();
        keyClick(Qt.Key_Up);
        compare(page.cfg_desktopLeadInMs, 1600);
        compare(spinBox.displayText, i18n("%1 ms", 1600));
    }

    // Secondary copy under its row, as every description on these pages is,
    // and capped so that it cannot widen the page (tst_appearance.qml's
    // test_aDescriptionCannotWidenTheConfigPage has the measurements).
    function test_theLeadsDescriptionIsSecondaryCopy() {
        const page = createWindowedPage(desktopPageComponent, "desktop");
        const spinBox = named(page, "leadInSpinBox");
        const rows = rowsBeside(spinBox);
        const description = rows[rows.indexOf(spinBox) + 1];
        compare(description.objectName, "formDescription");
        const reference = createTemporaryObject(smallFontLabelComponent, page, { visible: false });
        verify(reference !== null);
        settle(description);
        verify(Qt.colorEqual(description.color, description.Kirigami.Theme.disabledTextColor));
        compare(description.font.family, reference.font.family);
        compare(description.font.pointSize, reference.font.pointSize);
        compare(description.font.pixelSize, reference.font.pixelSize);
        compare(description.wrapMode, Text.WordWrap);
        verify(description.Layout.maximumWidth <= Kirigami.Units.gridUnit * 26);
        verify(description.width < spinBox.parent.width);
    }

    // The lead is timing, shared by both sets: the other tab shows the same
    // value, and syncing one set from the other leaves it alone.
    function test_theLeadIsTheSameOnBothTabs() {
        const page = createWindowedPage(desktopPageComponent, "desktop", { cfg_desktopLeadInMs: 3200 });
        const spinBox = named(page, "leadInSpinBox");
        compare(page.editingDark, true);
        named(page, "themeTabBar").currentIndex = 0;
        compare(page.editingDark, false);
        compare(spinBox.value, 3200);
        page.syncFrom(true);
        compare(page.cfg_desktopLeadInMs, 3200);
        page.syncFrom(false);
        compare(page.cfg_desktopLeadInMs, 3200);
    }

    // A panel has no lead: no key, no row, and its section is still called
    // "Auto-hide".
    function test_thePanelPageKeepsItsAutoHideSection() {
        const page = createWindowedPage(panelPageComponent, "panel");
        verify(!("cfg_panelLeadInMs" in page));
        verify(!("cfg_desktopLeadInMs" in page));
        compare(findAll(page, o => o.objectName === "leadInSpinBox").length, 0);
        compare(findAll(page, o => o.objectName === "behaviorSeparator").length, 0);
        const headings = sectionHeadings(page);
        verify(headings.indexOf(i18n("Auto-hide")) >= 0, headings.join(", "));
        verify(headings.indexOf(i18n("Behavior")) < 0, headings.join(", "));
    }
}
