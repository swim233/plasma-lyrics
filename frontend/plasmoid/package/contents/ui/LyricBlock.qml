import QtQuick
import QtQuick.Effects
import org.kde.kirigami as Kirigami

Item {
    id: root

    property string lyricText: ""
    // The secondary lyrics the user picked -- translation or romanization.
    property string secondaryLyricText: ""
    property color textColor: "white"
    // Defaults to the alpha the secondary lyrics take from the text colour
    // while their own colour is off.
    property color secondaryLyricColor: Qt.rgba(root.textColor.r, root.textColor.g,
                                                root.textColor.b, 0.68)
    property bool strokeEnabled: false
    property color strokeColor: "black"
    property string fontFamily: Kirigami.Theme.defaultFont.family
    property int fontSize: 34
    property int fontWeight: Font.Normal
    // The secondary lyrics' font as drawn (DESIGN.md decision 78): LyricsView
    // passes the lyric's own unless the secondary lyrics have one of their
    // own, so by default they follow it. Only these are ever italic.
    property string secondaryLyricFontFamily: root.fontFamily
    property int secondaryLyricFontSize: root.fontSize
    property int secondaryLyricFontWeight: root.fontWeight
    property bool secondaryLyricFontItalic: false
    property string overflowMode: "fit"

    property var words: []
    property real positionMs: 0
    property color unsungColor: root.textColor
    property color activeColor: root.textColor
    property color sungColor: root.textColor
    property bool liftEnabled: false
    property real liftEm: 0.14
    property bool brightnessEnabled: false
    property real brightnessStrength: 0.6
    property bool blurGlowEnabled: false
    property real lineHeightFactor: 1.25

    // The next line's shape and look (DESIGN.md decision 80), for the lyric
    // itself: LyricLine's staticFit, and the share of nextLineColor and
    // nextLineStrokeColor it is drawn in. The secondary lyrics are never
    // shown on a next line; secondaryLyricOpacity fades them in as the line
    // becomes the current one.
    property bool staticFit: false
    property color nextLineColor: root.textColor
    property color nextLineStrokeColor: root.strokeColor
    property real nextLineShare: 0
    property real secondaryLyricOpacity: 1

    // How blurred the block is, both lines of it, as the standard deviation
    // (logical pixels, in this block's own unscaled coordinates) of the
    // Gaussian it is meant to look like. 0 is no effect at all: no layer, no
    // MultiEffect.
    property real blurSigma: 0
    // Room left around the lines in the blur's source, so the blur fades out
    // inside the texture instead of stopping at its edge. Fixed while the
    // settings are, since resizing the source reallocates every texture of
    // the effect.
    property real blurPadding: 0

    implicitHeight: origin.implicitHeight + (secondaryLyric.visible ? secondaryLyric.implicitHeight : 0)
    height: implicitHeight
    // One line box of the lyric, whatever it holds right now.
    readonly property real lineHeight: origin.lineHeight

    // For AnimatedLyric's particle layer (DESIGN.md decision 77). Only the
    // lyric itself spawns particles, never the secondary lyrics; its line
    // sits at this block's top left. particlesWanted only on the current
    // block.
    property bool particlesWanted: false
    readonly property var particleLine: origin.particleLine
    readonly property real particleScrollOffset: origin.particleScrollOffset

    // MultiEffect does not blur the item it is given in place: it draws a
    // blurred copy over its own geometry and leaves the source to be drawn
    // too, so the source is made transparent while the effect draws it (its
    // content still reaches the effect; opacity 0 hides it from the scene,
    // not from the effect, and unlike `visible` it leaves LyricLine's marquee
    // condition alone). The copy is exactly as large as the source, and
    // autoPaddingEnabled and paddingRect both scale the source down into the
    // effect's geometry rather than widening what is sampled (the finding
    // recorded on LyricLine's halo), so the room for the blur is part of the
    // source itself: this holder, larger than the block by blurPadding on
    // every side.
    Item {
        id: lyricHolder
        x: -root.blurPadding
        y: -root.blurPadding
        width: root.width + 2 * root.blurPadding
        height: root.height + 2 * root.blurPadding
        opacity: blurLoader.active ? 0 : 1

        LyricLine {
            id: origin
            x: root.blurPadding
            y: root.blurPadding
            width: root.width
            lineText: root.lyricText
            textColor: root.textColor
            strokeEnabled: root.strokeEnabled
            strokeColor: root.strokeColor
            fontFamily: root.fontFamily
            fontSize: root.fontSize
            fontWeight: root.fontWeight
            overflowMode: root.overflowMode
            staticFit: root.staticFit
            nextLineColor: root.nextLineColor
            nextLineStrokeColor: root.nextLineStrokeColor
            nextLineShare: root.nextLineShare
            words: root.words
            positionMs: root.positionMs
            unsungColor: root.unsungColor
            activeColor: root.activeColor
            sungColor: root.sungColor
            liftEnabled: root.liftEnabled
            liftEm: root.liftEm
            brightnessEnabled: root.brightnessEnabled
            brightnessStrength: root.brightnessStrength
            blurGlowEnabled: root.blurGlowEnabled
            lineHeightFactor: root.lineHeightFactor
            particlesWanted: root.particlesWanted
        }

        // The secondary lyrics stay whole-line on purpose: word timings belong
        // to the lyric itself, and neither a translation nor a romanization line
        // is aligned to them (DESIGN.md 26 already treats the secondary lyrics as
        // the weaker of the two).
        LyricLine {
            id: secondaryLyric
            x: root.blurPadding
            y: root.blurPadding + origin.height
            width: root.width
            visible: root.secondaryLyricText.length > 0
            opacity: root.secondaryLyricOpacity
            lineText: root.secondaryLyricText
            textColor: root.secondaryLyricColor
            strokeEnabled: root.strokeEnabled
            strokeColor: root.strokeColor
            fontFamily: root.secondaryLyricFontFamily
            fontSize: root.secondaryLyricFontSize
            fontWeight: root.secondaryLyricFontWeight
            fontItalic: root.secondaryLyricFontItalic
            overflowMode: root.overflowMode
            lineHeightFactor: root.lineHeightFactor
        }
    }

    // MultiEffect's blur is not a Gaussian: it mixes the source with three to
    // five successively halved copies of it, each blurred with a small
    // kernel, with weights set by blur × blurMax (qtdeclarative v6.11.2
    // src/effects/qquickmultieffect.cpp:1610-1631, :1723-1733), so neither
    // number is a radius. This table gives, for each standard deviation, the
    // blur × blurMax (blurMax 64, five copies) that looked most like it,
    // measured on the next line itself: 34 px Noto Sans CJK SC, drawn at
    // 0.75× under Xvfb with real GL, at every product from 0.25 to 64 in
    // steps of 0.25, against the same line at 0% put through ImageMagick's
    // -gaussian-blur 0x<0.75 σ> -- the product whose frame differed least
    // (root-mean-square, on luminance), σ being in the block's own pixels.
    // The same sweep at 68 px picked products up to 3.5 away (σ 2: 11
    // against 14.5; σ 16: 57 against 60.25), so the table holds exactly at
    // the default size only. Past σ 18 there is nothing stronger: the fifth
    // copy is as blurred as MultiEffect gets without raising blurMultiplier,
    // which widens every copy and so moves every other point of the table.
    readonly property var blurCalibration: [
        [0, 0], [0.5, 0.25], [0.75, 1.75], [1, 4.75], [1.25, 7.25], [1.5, 9.75],
        [1.75, 11.75], [2, 14.5], [2.5, 18.5], [3, 21.75], [4, 26.25], [5, 28.25],
        [6, 30.75], [7, 33.75], [8, 36.5], [9, 39.75], [10, 42.25], [12, 46.5],
        [14, 53.75], [16, 60.25], [18, 64]
    ]
    readonly property int blurMax: 64
    function blurProduct(sigma) {
        const table = root.blurCalibration;
        if (sigma <= 0) {
            return 0;
        }
        for (let i = 1; i < table.length; ++i) {
            if (sigma <= table[i][0]) {
                const a = table[i - 1], b = table[i];
                return a[1] + (b[1] - a[1]) * (sigma - a[0]) / (b[0] - a[0]);
            }
        }
        return table[table.length - 1][1];
    }

    Loader {
        id: blurLoader
        objectName: "nextLineBlur"
        active: root.blurSigma > 0
        x: lyricHolder.x
        y: lyricHolder.y
        width: lyricHolder.width
        height: lyricHolder.height
        sourceComponent: MultiEffect {
            source: lyricHolder
            autoPaddingEnabled: false
            blurEnabled: true
            blurMax: root.blurMax
            blur: root.blurProduct(root.blurSigma) / root.blurMax
        }
    }
}
