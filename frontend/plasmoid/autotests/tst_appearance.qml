import QtQuick
import QtTest
import "../package/contents/ui" as LyricsUi

TestCase {
    name: "Appearance"
    when: windowShown

    Component {
        id: lyricLineComponent
        LyricsUi.LyricLine {
            width: 320
            lineText: "A deliberately long lyric used to exercise overflow"
        }
    }

    Component {
        id: animatedLyricComponent
        LyricsUi.AnimatedLyric {
            width: 320
            height: 120
            lyricText: "first"
            translationText: "translation one"
            animationMode: "none"
        }
    }

    function test_overflowModes() {
        const line = createTemporaryObject(lyricLineComponent, this);
        verify(line !== null);
        compare(line.overflowMode, "fit");
        line.overflowMode = "wrap";
        verify(line.implicitHeight > 0);
        line.overflowMode = "marquee";
        compare(line.height, line.lineHeight);
    }

    function test_outlineIsOptIn() {
        const line = createTemporaryObject(lyricLineComponent, this);
        compare(line.strokeEnabled, false);
        compare(line.directions.length, 8);
    }

    function test_lineAndTranslationSwitchTogether() {
        const lyric = createTemporaryObject(animatedLyricComponent, this);
        verify(lyric !== null);
        compare(lyric.shownText, "first");
        lyric.lyricText = "second";
        lyric.translationText = "translation two";
        tryCompare(lyric, "shownText", "second");
        compare(lyric.shownTranslation, "translation two");
    }
}
