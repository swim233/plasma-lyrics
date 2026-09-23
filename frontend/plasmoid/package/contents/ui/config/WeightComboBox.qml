import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

import "../FontPolicy.js" as FontPolicy

// The weight row for one section: the faces `family` really has,
// lightest first, so no entry is one fontconfig would have to synthesize
// (see `faces` for the one case where the family's faces are unknown).
// It shows the face FontPolicy.renderWeight() draws, which for a stored
// weight the family lacks is the nearest face it has -- displayed only;
// the stored value is written by a pick here or in the font picker
// above, never by this binding.
QQC2.ComboBox {
    id: weightBox

    required property var fontCatalog
    property string family
    property int storedWeight: Font.Normal

    signal weightPicked(int weight)

    // weights() is empty for a family whose faces FontCatalog does not
    // know, such as one of Qt's generic names ("Sans Serif") set as the
    // Plasma font. The widget then renders the stored weight as it is
    // (snapWeight() of an empty list returns it), so the row offers the
    // six steps it offered before the list followed the family, plus the
    // stored weight when it is none of them, so that the row never reads
    // as a weight other than the one drawn.
    readonly property var fallbackWeights: [300, 400, 500, 600, 700, 900]
    readonly property var faces: {
        const listed = weightBox.fontCatalog.weights(weightBox.family);
        if (listed.length > 0) {
            return listed;
        }
        const steps = weightBox.fallbackWeights.includes(weightBox.storedWeight)
            ? weightBox.fallbackWeights
            : weightBox.fallbackWeights.concat([weightBox.storedWeight]).sort((a, b) => a - b);
        return steps.map(weight => ({ weight: weight, styleName: "" }));
    }
    readonly property int shownWeight: FontPolicy.renderWeight(weightBox.fontCatalog, weightBox.family, weightBox.storedWeight)

    // The nine CSS weight names for the standard steps; any other
    // weight is a face its designer named, so it keeps that name.
    function label(face) {
        switch (face.weight) {
        case 100:
            return i18nc("@item:inlistbox font weight", "Thin");
        case 200:
            return i18nc("@item:inlistbox font weight", "Extra light");
        case 300:
            return i18nc("@item:inlistbox font weight", "Light");
        case 400:
            return i18nc("@item:inlistbox font weight", "Regular");
        case 500:
            return i18nc("@item:inlistbox font weight", "Medium");
        case 600:
            return i18nc("@item:inlistbox font weight", "Demi bold");
        case 700:
            return i18nc("@item:inlistbox font weight", "Bold");
        case 800:
            return i18nc("@item:inlistbox font weight", "Extra bold");
        case 900:
            return i18nc("@item:inlistbox font weight", "Black");
        default:
            return face.styleName || String(face.weight);
        }
    }

    // A family's own style names can be long; the cap keeps one from
    // widening the page, as for the font picker.
    Layout.maximumWidth: Kirigami.Units.gridUnit * 14
    model: weightBox.faces.map(face => weightBox.label(face))
    currentIndex: weightBox.faces.findIndex(face => face.weight === weightBox.shownWeight)
    // A single face leaves nothing to choose.
    enabled: weightBox.faces.length > 1
    onActivated: index => weightBox.weightPicked(weightBox.faces[index].weight)
}
