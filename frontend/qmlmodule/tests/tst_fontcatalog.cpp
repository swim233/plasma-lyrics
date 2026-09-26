#include "frontend/qmlmodule/fontcatalog.h"
#include "frontend/qmlmodule/fontmatching.h"

#include <QFont>
#include <QFontDatabase>
#include <QTest>

#include <algorithm>
#include <vector>

using FontMatching::FamilyName;

namespace
{

QString u8(const char *text)
{
    return QString::fromUtf8(text);
}

QList<FamilyName> pattern(std::initializer_list<std::pair<const char *, const char *>> names)
{
    QList<FamilyName> result;
    for (const auto &[name, lang] : names) {
        result.append({u8(name), u8(lang)});
    }
    return result;
}

QString describe(const QList<QStringList> &groups)
{
    QStringList parts;
    for (const QStringList &group : groups) {
        parts.append(QLatin1Char('[') + group.join(QLatin1String(", ")) + QLatin1Char(']'));
    }
    return parts.join(QLatin1Char(' '));
}

int score(const char *query, const char *candidate)
{
    const std::optional<int> result = FontMatching::fuzzyScore(u8(query), u8(candidate));
    return result ? *result : -1;
}

QVariantList weightList(std::initializer_list<int> weights)
{
    QVariantList result;
    for (const int weight : weights) {
        result.append(QVariantMap{{QStringLiteral("weight"), weight},
                                  {QStringLiteral("styleName"), QString::number(weight)}});
    }
    return result;
}

bool sortedCaseInsensitively(const QStringList &names)
{
    return std::is_sorted(names.cbegin(), names.cend(), [](const QString &a, const QString &b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
}

// What FcFontList returns under LANG=C for the fonts the pairing rule has to
// get right, and the names Qt lists for them there.
QList<QList<FamilyName>> englishPatterns()
{
    return {
        pattern({{"Microsoft YaHei", "en"}, {"微软雅黑", "zh-cn"}}),
        pattern({{"Microsoft YaHei UI", "en"}}),
        pattern({{"FZCuYuan-M03", "en"}, {"方正粗圆_GBK", "zh-cn"}}),
        pattern({{"A-OTF Ryumin Pr5", "en"}, {"A-OTF リュウミン Pr5", "ja"},
                 {"A-OTF Ryumin Pr5 EB-KL", "en"}, {"A-OTF リュウミン Pr5 EB-KL", "ja"}}),
        pattern({{"A-OTF Ryumin Pr5", "en"}, {"A-OTF リュウミン Pr5", "ja"},
                 {"A-OTF Ryumin Pr5 M-KL", "en"}, {"A-OTF リュウミン Pr5 M-KL", "ja"}}),
        pattern({{"DejaVu Sans", "en"}, {"DejaVu Sans Condensed", "en"}}),
        pattern({{"DejaVu Sans", "en"}}),
        pattern({{"Nimbus Sans", "en"}}),
        pattern({{"WenQuanYi Zen Hei", "en"}, {"文泉驛正黑", "zh-tw"}, {"文泉驿正黑", "zh-cn"}}),
    };
}

QStringList englishListed()
{
    return {
        u8("Microsoft YaHei"), u8("Microsoft YaHei UI"), u8("FZCuYuan-M03"), u8("A-OTF Ryumin Pr5"),
        u8("DejaVu Sans"), u8("DejaVu Sans Condensed"), u8("Nimbus Sans [URW ]"), u8("Nimbus Sans [UKWN]"),
        u8("WenQuanYi Zen Hei"), u8("Sans Serif"), u8("Serif"), u8("Monospace"),
    };
}

} // namespace

class FontCatalogTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // --- snapWeight: the CSS Fonts level 4 rule quoted in fontcatalog.h ---

    void snapWeight_data()
    {
        QTest::addColumn<QList<int>>("available");
        QTest::addColumn<int>("target");
        QTest::addColumn<int>("expected");

        QTest::newRow("empty list keeps the target") << QList<int>{} << 450 << 450;
        QTest::newRow("present 400 is kept over 500") << QList<int>{400, 500} << 400 << 400;
        QTest::newRow("present 500 is kept over 400") << QList<int>{400, 500} << 500 << 500;
        QTest::newRow("present 316 is kept") << QList<int>{100, 316, 400} << 316 << 316;

        QTest::newRow("400 prefers 500") << QList<int>{300, 500} << 400 << 500;
        QTest::newRow("500 prefers 400") << QList<int>{400, 600} << 500 << 400;
        QTest::newRow("400 without 500 goes below") << QList<int>{300, 700} << 400 << 300;
        QTest::newRow("500 goes below before above") << QList<int>{300, 700} << 500 << 300;
        QTest::newRow("450 takes the lightest up to 500") << QList<int>{316, 480, 500, 700} << 450 << 480;
        QTest::newRow("450 takes 500 over 400") << QList<int>{316, 400, 500, 700} << 450 << 500;
        QTest::newRow("450 with nothing up to 500 goes below") << QList<int>{316, 400, 700} << 450 << 400;
        QTest::newRow("450 goes below to 300") << QList<int>{100, 300, 700} << 450 << 300;
        QTest::newRow("450 with only weights past 500") << QList<int>{900, 700} << 450 << 700;
        QTest::newRow("Ryumin: 400 takes 450") << QList<int>{450, 600} << 400 << 450;

        QTest::newRow("below 400 takes the heaviest below") << QList<int>{100, 316, 400} << 350 << 316;
        QTest::newRow("200 takes 100") << QList<int>{100, 300, 700} << 200 << 100;
        QTest::newRow("below 400 with nothing lighter goes up") << QList<int>{316, 400} << 300 << 316;
        QTest::newRow("Ryumin: 300 goes up to 450") << QList<int>{450, 600} << 300 << 450;

        QTest::newRow("above 500 takes the lightest above") << QList<int>{316, 400, 800, 900} << 700 << 800;
        QTest::newRow("600 takes 700") << QList<int>{100, 300, 700} << 600 << 700;
        QTest::newRow("above 500 with nothing heavier goes down") << QList<int>{316, 400, 500} << 600 << 500;
        QTest::newRow("600 goes down to 300") << QList<int>{300} << 600 << 300;
        QTest::newRow("Ryumin: 700 goes down to 600") << QList<int>{450, 600} << 700 << 600;

        QTest::newRow("CJK set: 350 takes 316") << QList<int>{100, 300, 316, 400, 500, 700, 900} << 350 << 316;
        QTest::newRow("CJK set: 600 takes 700") << QList<int>{100, 300, 316, 400, 500, 700, 900} << 600 << 700;
    }

    void snapWeight()
    {
        QFETCH(QList<int>, available);
        QFETCH(int, target);
        QFETCH(int, expected);
        QCOMPARE(FontMatching::snapWeight(available, target), expected);

        // Order of the list must not matter.
        QList<int> reversed = available;
        std::reverse(reversed.begin(), reversed.end());
        QCOMPARE(FontMatching::snapWeight(reversed, target), expected);
    }

    // The QML-facing overload takes a weights() result: a list of maps.
    void snapWeightReadsWeightsResult()
    {
        const FontCatalog catalog;
        QCOMPARE(catalog.snapWeight(weightList({316, 500}), 400), 500);
        QCOMPARE(catalog.snapWeight(weightList({100, 900}), 400), 100);
        QCOMPARE(catalog.snapWeight(QVariantList{}, 700), 700);
        // A JS array hands its numbers over as doubles.
        const QVariantList fromJs{QVariantMap{{QStringLiteral("weight"), 316.0}},
                                  QVariantMap{{QStringLiteral("weight"), 700.0}}};
        QCOMPARE(catalog.snapWeight(fromJs, 350), 316);
    }

    // --- uprightWeights ---

    void uprightWeightsDropsSlantedFacesAndPrefersNormalWidth()
    {
        // DejaVu Sans as Qt reports it. QFontDatabase::italic() is false for
        // all of these, so `upright` comes from the built font's style.
        const QList<FontMatching::Face> faces = {
            {u8("ExtraLight"), 200, true},
            {u8("Book"), 400, true},
            {u8("Oblique"), 400, false},
            {u8("Bold Oblique"), 700, false},
            {u8("Condensed Bold"), 700, true},
            {u8("Condensed"), 400, true},
            {u8("Condensed Bold Oblique"), 700, false},
            {u8("Condensed Oblique"), 400, false},
            {u8("Bold"), 700, true},
        };
        const auto check = [](const QList<FontMatching::Weight> &weights) {
            QCOMPARE(weights.size(), 3);
            QCOMPARE(weights.at(0).weight, 200);
            QCOMPARE(weights.at(0).styleName, u8("ExtraLight"));
            QCOMPARE(weights.at(1).weight, 400);
            QCOMPARE(weights.at(1).styleName, u8("Book"));
            QCOMPARE(weights.at(2).weight, 700);
            QCOMPARE(weights.at(2).styleName, u8("Bold"));
        };
        check(FontMatching::uprightWeights(faces));
        QList<FontMatching::Face> reversed = faces;
        std::reverse(reversed.begin(), reversed.end());
        check(FontMatching::uprightWeights(reversed));
    }

    void uprightWeightsTieBreak()
    {
        // A width word loses even to a longer name.
        auto weights = FontMatching::uprightWeights({{u8("Narrow"), 400, true}, {u8("Regular"), 400, true}});
        QCOMPARE(weights.size(), 1);
        QCOMPARE(weights.first().styleName, u8("Regular"));
        weights = FontMatching::uprightWeights({{u8("SemiExpanded"), 400, true}, {u8("Wide"), 400, true},
                                                {u8("Normal"), 400, true}});
        QCOMPARE(weights.first().styleName, u8("Normal"));
        // Then the shorter name, then the alphabetically first.
        weights = FontMatching::uprightWeights({{u8("Regular"), 400, true}, {u8("Book"), 400, true}});
        QCOMPARE(weights.first().styleName, u8("Book"));
        weights = FontMatching::uprightWeights({{u8("Roman"), 400, true}, {u8("Plain"), 400, true}});
        QCOMPARE(weights.first().styleName, u8("Plain"));
        weights = FontMatching::uprightWeights({{u8("Plain"), 400, true}, {u8("Roman"), 400, true}});
        QCOMPARE(weights.first().styleName, u8("Plain"));
    }

    void uprightWeightsSkipsUnknownWeights()
    {
        const auto weights = FontMatching::uprightWeights({{u8("Mystery"), -1, true},
                                                           {u8("Zero"), 0, true},
                                                           {u8("DemiLight"), 316, true},
                                                           {u8("Italic"), 400, false}});
        QCOMPARE(weights.size(), 1);
        QCOMPARE(weights.first().weight, 316);
        QCOMPARE(weights.first().styleName, u8("DemiLight"));
        QVERIFY(FontMatching::uprightWeights({{u8("Italic"), 400, false}}).isEmpty());
        QVERIFY(FontMatching::uprightWeights({}).isEmpty());
    }

    // families() leaves out a family this is empty for.
    void uprightWeightsOfItalicOnlyFamilyIsEmpty()
    {
        // "Nimbus Sans [URW ]": the URW foundry ships only the italics.
        QVERIFY(FontMatching::uprightWeights({{u8("Bold Italic"), 700, false}, {u8("Italic"), 400, false}})
                    .isEmpty());
    }

    // --- italicWeights ---

    // The mirror of uprightWeights over the same DejaVu Sans faces: the
    // oblique ones count as italic, with the same width tie-break.
    void italicWeightsKeepsSlantedFacesAndPrefersNormalWidth()
    {
        const QList<FontMatching::Face> faces = {
            {u8("Book"), 400, true},
            {u8("Condensed Oblique"), 400, false},
            {u8("Oblique"), 400, false},
            {u8("Bold"), 700, true},
            {u8("Condensed Bold Oblique"), 700, false},
            {u8("Bold Oblique"), 700, false},
        };
        const auto weights = FontMatching::italicWeights(faces);
        QCOMPARE(weights.size(), 2);
        QCOMPARE(weights.at(0).weight, 400);
        QCOMPARE(weights.at(0).styleName, u8("Oblique"));
        QCOMPARE(weights.at(1).weight, 700);
        QCOMPARE(weights.at(1).styleName, u8("Bold Oblique"));
    }

    // A family's italic weights need not be its upright ones; an italic
    // weight is chosen among the italics alone.
    void italicWeightsAreTheirOwnSet()
    {
        const QList<FontMatching::Face> faces = {
            {u8("Light"), 300, true},
            {u8("Regular"), 400, true},
            {u8("Bold"), 700, true},
            {u8("Italic"), 400, false},
            {u8("Mystery Italic"), 0, false},
        };
        const auto italics = FontMatching::italicWeights(faces);
        QCOMPARE(italics.size(), 1);
        QCOMPARE(italics.first().weight, 400);
        QCOMPARE(FontMatching::uprightWeights(faces).size(), 3);
    }

    // CJK families have no italic faces at all: Qt slants an upright one.
    void italicWeightsOfAnUprightOnlyFamilyIsEmpty()
    {
        QVERIFY(FontMatching::italicWeights({{u8("Regular"), 400, true}, {u8("Bold"), 700, true}}).isEmpty());
        QVERIFY(FontMatching::italicWeights({}).isEmpty());
    }

    // --- groupFamilyNames ---

    void groupFamilyNames()
    {
        struct Row
        {
            const char *name;
            QList<FamilyName> input;
            QList<QStringList> expected;
        };
        const std::vector<Row> rows = {
            {"two languages pair up",
             pattern({{"FZCuYuan-M03", "en"}, {"方正粗圆_GBK", "zh-cn"}}),
             {{u8("FZCuYuan-M03"), u8("方正粗圆_GBK")}}},
            {"process language moved to the front still pairs",
             pattern({{"微软雅黑", "zh-cn"}, {"Microsoft YaHei", "en"}}),
             {{u8("微软雅黑"), u8("Microsoft YaHei")}}},
            {"three languages form one group",
             pattern({{"WenQuanYi Zen Hei", "en"}, {"文泉驛正黑", "zh-tw"}, {"文泉驿正黑", "zh-cn"}}),
             {{u8("WenQuanYi Zen Hei"), u8("文泉驛正黑"), u8("文泉驿正黑")}}},
            {"two levels in two languages make two pairs",
             pattern({{"A-OTF Ryumin Pr5", "en"}, {"A-OTF リュウミン Pr5", "ja"},
                      {"A-OTF Ryumin Pr5 EB-KL", "en"}, {"A-OTF リュウミン Pr5 EB-KL", "ja"}}),
             {{u8("A-OTF Ryumin Pr5"), u8("A-OTF リュウミン Pr5")},
              {u8("A-OTF Ryumin Pr5 EB-KL"), u8("A-OTF リュウミン Pr5 EB-KL")}}},
            {"the same two pairs after FcFontList moves ja to the front",
             pattern({{"A-OTF リュウミン Pr5", "ja"}, {"A-OTF Ryumin Pr5", "en"},
                      {"A-OTF Ryumin Pr5 EB-KL", "en"}, {"A-OTF リュウミン Pr5 EB-KL", "ja"}}),
             {{u8("A-OTF リュウミン Pr5"), u8("A-OTF Ryumin Pr5")},
              {u8("A-OTF Ryumin Pr5 EB-KL"), u8("A-OTF リュウミン Pr5 EB-KL")}}},
            {"one language twice is two families",
             pattern({{"DejaVu Sans", "en"}, {"DejaVu Sans Condensed", "en"}}),
             {{u8("DejaVu Sans")}, {u8("DejaVu Sans Condensed")}}},
            {"a second level in more languages than the first",
             pattern({{"Foo", "en"}, {"Foo Light", "en"}, {"フー Light", "ja"}}),
             {{u8("Foo")}, {u8("Foo Light"), u8("フー Light")}}},
            {"a single name",
             pattern({{"Noto Sans CJK SC", "en"}}),
             {{u8("Noto Sans CJK SC")}}},
            {"a name without a language pairs with nothing",
             pattern({{"Foo", "en"}, {"Bar", ""}, {"バズ", "ja"}}),
             {{u8("Foo")}, {u8("Bar")}, {u8("バズ")}}},
            {"no languages at all",
             pattern({{"Foo", ""}, {"Bar", ""}}),
             {{u8("Foo")}, {u8("Bar")}}},
            {"languages compare case-insensitively",
             pattern({{"Foo", "en"}, {"Foo Bold", "EN"}}),
             {{u8("Foo")}, {u8("Foo Bold")}}},
            {"a repeated name is dropped",
             pattern({{"Foo", "en"}, {"Foo", "ja"}, {"フー", "ja"}}),
             {{u8("Foo"), u8("フー")}}},
            {"nothing", {}, {}},
        };
        for (const Row &row : rows) {
            const QList<QStringList> actual = FontMatching::groupFamilyNames(row.input);
            QVERIFY2(actual == row.expected,
                     qPrintable(QStringLiteral("%1: got %2, expected %3")
                                    .arg(u8(row.name), describe(actual), describe(row.expected))));
        }
    }

    // --- buildNameIndex ---

    void nameIndexUnderEnglishLocale()
    {
        const FontMatching::NameIndex index = FontMatching::buildNameIndex(englishPatterns(), englishListed());

        QCOMPARE(index.alternates.value(u8("Microsoft YaHei")), QStringList{u8("微软雅黑")});
        QCOMPARE(index.alternates.value(u8("FZCuYuan-M03")), QStringList{u8("方正粗圆_GBK")});
        QCOMPARE(index.alternates.value(u8("A-OTF Ryumin Pr5")), QStringList{u8("A-OTF リュウミン Pr5")});
        QCOMPARE(index.alternates.value(u8("WenQuanYi Zen Hei")),
                 (QStringList{u8("文泉驛正黑"), u8("文泉驿正黑")}));
        // Different families in the same language are no alternate names.
        QVERIFY(!index.alternates.contains(u8("DejaVu Sans")));
        QVERIFY(!index.alternates.contains(u8("DejaVu Sans Condensed")));
        QVERIFY(!index.alternates.contains(u8("Microsoft YaHei UI")));
        // A bare name is no alternate of its own foundry-qualified listing.
        QVERIFY(!index.alternates.contains(u8("Nimbus Sans [UKWN]")));

        // Every listed name resolves to itself, generic ones included.
        for (const QString &listed : englishListed()) {
            QCOMPARE(index.listedByName.value(listed), listed);
        }
        QCOMPARE(index.listedByName.value(u8("微软雅黑")), u8("Microsoft YaHei"));
        QCOMPARE(index.listedByName.value(u8("方正粗圆_GBK")), u8("FZCuYuan-M03"));
        QCOMPARE(index.listedByName.value(u8("A-OTF リュウミン Pr5")), u8("A-OTF Ryumin Pr5"));
        QCOMPARE(index.listedByName.value(u8("文泉驿正黑")), u8("WenQuanYi Zen Hei"));
        // Qt folds the legacy-family group into the pattern's listed family.
        QCOMPARE(index.listedByName.value(u8("A-OTF Ryumin Pr5 EB-KL")), u8("A-OTF Ryumin Pr5"));
        QCOMPARE(index.listedByName.value(u8("A-OTF リュウミン Pr5 M-KL")), u8("A-OTF Ryumin Pr5"));
        QCOMPARE(index.listedByName.value(u8("Nimbus Sans")), u8("Nimbus Sans [UKWN]"));
        QVERIFY(!index.listedByName.contains(u8("Noto Sans")));

        for (const char *name : {"Microsoft YaHei", "Microsoft YaHei UI", "FZCuYuan-M03", "A-OTF Ryumin Pr5",
                                 "DejaVu Sans", "DejaVu Sans Condensed", "Nimbus Sans [URW ]",
                                 "Nimbus Sans [UKWN]", "WenQuanYi Zen Hei"}) {
            QVERIFY2(index.backed.contains(u8(name)), name);
        }
        for (const char *name : {"Sans Serif", "Serif", "Monospace"}) {
            QVERIFY2(!index.backed.contains(u8(name)), name);
        }
    }

    // FcFontList under zh_CN puts the zh-cn name first, and Qt lists that one.
    void nameIndexUnderChineseLocale()
    {
        const QList<QList<FamilyName>> patterns = {
            pattern({{"微软雅黑", "zh-cn"}, {"Microsoft YaHei", "en"}}),
            pattern({{"Microsoft YaHei UI", "en"}}),
            pattern({{"方正粗圆_GBK", "zh-cn"}, {"FZCuYuan-M03", "en"}}),
            pattern({{"文泉驿正黑", "zh-cn"}, {"WenQuanYi Zen Hei", "en"}, {"文泉驛正黑", "zh-tw"}}),
        };
        const QStringList listed = {u8("微软雅黑"), u8("Microsoft YaHei UI"), u8("方正粗圆_GBK"), u8("文泉驿正黑")};
        const FontMatching::NameIndex index = FontMatching::buildNameIndex(patterns, listed);

        QCOMPARE(index.alternates.value(u8("微软雅黑")), QStringList{u8("Microsoft YaHei")});
        QCOMPARE(index.alternates.value(u8("方正粗圆_GBK")), QStringList{u8("FZCuYuan-M03")});
        QCOMPARE(index.alternates.value(u8("文泉驿正黑")), (QStringList{u8("WenQuanYi Zen Hei"), u8("文泉驛正黑")}));
        QCOMPARE(index.listedByName.value(u8("Microsoft YaHei")), u8("微软雅黑"));
        QCOMPARE(index.listedByName.value(u8("FZCuYuan-M03")), u8("方正粗圆_GBK"));
        QCOMPARE(index.listedByName.value(u8("文泉驛正黑")), u8("文泉驿正黑"));
        QVERIFY(!index.alternates.contains(u8("Microsoft YaHei UI")));
    }

    void nameIndexUnderJapaneseLocale()
    {
        const QList<QList<FamilyName>> patterns = {
            pattern({{"A-OTF リュウミン Pr5", "ja"}, {"A-OTF Ryumin Pr5", "en"},
                     {"A-OTF Ryumin Pr5 EB-KL", "en"}, {"A-OTF リュウミン Pr5 EB-KL", "ja"}}),
        };
        const FontMatching::NameIndex index = FontMatching::buildNameIndex(patterns, {u8("A-OTF リュウミン Pr5")});
        QCOMPARE(index.alternates.value(u8("A-OTF リュウミン Pr5")), QStringList{u8("A-OTF Ryumin Pr5")});
        QCOMPARE(index.listedByName.value(u8("A-OTF Ryumin Pr5")), u8("A-OTF リュウミン Pr5"));
        QCOMPARE(index.listedByName.value(u8("A-OTF Ryumin Pr5 EB-KL")), u8("A-OTF リュウミン Pr5"));
        QCOMPARE(index.listedByName.value(u8("A-OTF リュウミン Pr5 EB-KL")), u8("A-OTF リュウミン Pr5"));
    }

    void nameIndexIgnoresPatternOrder()
    {
        QList<QList<FamilyName>> patterns = englishPatterns();
        // A name that is a listed name's group-mate in one pattern and only
        // folded into another pattern's family in the other.
        patterns.append(pattern({{"Foo", "en"}, {"Foo Bold", "en"}}));
        patterns.append(pattern({{"Foo Bold", "en"}, {"フー Bold", "ja"}}));
        QStringList listed = englishListed();
        listed.append({u8("Foo"), u8("フー Bold")});

        const FontMatching::NameIndex forward = FontMatching::buildNameIndex(patterns, listed);
        std::reverse(patterns.begin(), patterns.end());
        const FontMatching::NameIndex backward = FontMatching::buildNameIndex(patterns, listed);

        QCOMPARE(forward.listedByName.value(u8("Foo Bold")), u8("フー Bold"));
        QCOMPARE(forward.alternates.value(u8("フー Bold")), QStringList{u8("Foo Bold")});
        QVERIFY(!forward.alternates.contains(u8("Foo")));
        QVERIFY(forward.listedByName == backward.listedByName);
        QVERIFY(forward.alternates == backward.alternates);
        QVERIFY(forward.backed == backward.backed);
    }

    // A family's faces can carry different name sets; its alternates are the
    // union, sorted case-insensitively rather than in fontconfig's order.
    void nameIndexMergesAndSortsAlternates()
    {
        const FontMatching::NameIndex index = FontMatching::buildNameIndex(
            {pattern({{"Foo", "en"}, {"フー", "ja"}, {"bar", "de"}}),
             pattern({{"Foo", "en"}, {"Baz", "fr"}, {"フー", "ja"}})},
            {u8("Foo")});
        QCOMPARE(index.alternates.value(u8("Foo")), (QStringList{u8("bar"), u8("Baz"), u8("フー")}));
    }

    void nameIndexSkipsPatternsQtDoesNotList()
    {
        const FontMatching::NameIndex index = FontMatching::buildNameIndex(
            {pattern({{"Hidden", "en"}, {"隐藏", "zh-cn"}})}, {u8("Shown")});
        QVERIFY(!index.listedByName.contains(u8("Hidden")));
        QVERIFY(!index.listedByName.contains(u8("隐藏")));
        QVERIFY(index.alternates.isEmpty());
        QVERIFY(index.backed.isEmpty());
        QCOMPARE(index.listedByName.value(u8("Shown")), u8("Shown"));
    }

    // --- fuzzyScore / fuzzyRank ---

    void fuzzyMatches()
    {
        const std::vector<std::pair<const char *, const char *>> matches = {
            {"nscjk", "Noto Sans CJK SC"},
            {"雅黑", "微软雅黑"},
            {"yahei", "Microsoft YaHei"},
            {"YAHEI", "microsoft yahei"},
            {"noto  sans", "NotoSans"},
            {" yahei ", "Microsoft YaHei"},
            {"cuyuan", "FZCuYuan-M03"},
            {"m03", "FZCuYuan-M03"},
            {"リュウミン", "A-OTF リュウミン Pr5"},
        };
        for (const auto &[query, candidate] : matches) {
            QVERIFY2(score(query, candidate) > 0, qPrintable(u8(query) + u8(" in ") + u8(candidate)));
        }
        const std::vector<std::pair<const char *, const char *>> misses = {
            {"kjc", "Noto Sans CJK SC"},
            {"yaheix", "Microsoft YaHei"},
            {"雅黑", "方正兰亭粗黑_GBK"},
            {"abc", "ab"},
            {"x", ""},
        };
        for (const auto &[query, candidate] : misses) {
            QVERIFY2(score(query, candidate) < 0, qPrintable(u8(query) + u8(" in ") + u8(candidate)));
        }
        QCOMPARE(score("", "anything"), 0);
        QCOMPARE(score(" \t", "anything"), 0);
        QCOMPARE(score("", ""), 0);
    }

    // The constants documented in fontmatching.h, spelled out once.
    void fuzzyScoreValues()
    {
        QCOMPARE(score("a", "a"), 16 + 2 * 10);
        QCOMPARE(score("ab", "ab"), (16 + 2 * 10) + (16 + 10));
        QCOMPARE(score("ac", "abc"), (16 + 2 * 10) + (16 - 3));
        QCOMPARE(score("ad", "abcd"), (16 + 2 * 10) + (16 - 3 - 1));
        QCOMPARE(score("b", "ab"), 16);
        QCOMPARE(score("bc", "abc"), 16 + (16 + 4));
        QCOMPARE(score("b", "a b"), 16 + 2 * 8);
        QCOMPARE(score("b", "a-b"), 16 + 2 * 8);
        QCOMPARE(score("b", "a_b"), 16 + 2 * 8);
        QCOMPARE(score("h", "YaHei"), 16 + 2 * 7);
        QCOMPARE(score("0", "M03"), 16 + 2 * 7);
        QCOMPARE(score("3", "M03"), 16);
        QCOMPARE(score("黑", "微软雅黑"), 16 + 2 * 8);
        QCOMPARE(score("p", "リュウミンPr5"), 16 + 2 * 8);
    }

    void fuzzyScorePreferences()
    {
        // Prefix over word start over the middle of a word.
        QVERIFY(score("mono", "Monospace") > score("mono", "DejaVu Sans Mono"));
        QVERIFY(score("mono", "DejaVu Sans Mono") > score("mono", "Harmonoid"));
        // A run over the same letters spread across word starts.
        QVERIFY(score("sans", "Noto Sans") > score("sans", "Source Aurora Nova Serif"));
        // CJK: at the start over further in.
        QVERIFY(score("黑", "黑体") > score("黑", "微软雅黑"));
        // Case and query whitespace change nothing.
        QCOMPARE(score("YaHei", "Microsoft YaHei"), score("yahei", "Microsoft YaHei"));
        QCOMPARE(score("noto sans", "Noto Sans"), score("notosans", "Noto Sans"));
        // The best alignment, not the first one: "S" of Sans and "C" of CJK
        // would score less than the trailing "SC".
        QCOMPARE(score("sc", "Noto Sans CJK SC"), score("sc", "Noto SC"));
    }

    void fuzzyRank()
    {
        using List = QList<QStringList>;
        QCOMPARE(FontMatching::fuzzyRank(u8("mono"), List{{u8("Harmonoid")}, {u8("DejaVu Sans Mono")},
                                                            {u8("Monospace")}, {u8("Arial")}}),
                 (QList<qsizetype>{2, 1, 0}));
        // Equal scores keep the input order, whichever way round it is.
        QCOMPARE(FontMatching::fuzzyRank(u8("nscjk"), List{{u8("Noto Sans CJK HK")}, {u8("Noto Sans CJK SC")}}),
                 (QList<qsizetype>{0, 1}));
        QCOMPARE(FontMatching::fuzzyRank(u8("nscjk"), List{{u8("Noto Sans CJK SC")}, {u8("Noto Sans CJK HK")}}),
                 (QList<qsizetype>{0, 1}));
        // An entry matches through any of its names and scores its best.
        const List withAlternates = {{u8("微软雅黑"), u8("Microsoft YaHei")}, {u8("Arial")}};
        QCOMPARE(FontMatching::fuzzyRank(u8("yahei"), withAlternates), QList<qsizetype>{0});
        QCOMPARE(FontMatching::fuzzyRank(u8("雅黑"), withAlternates), QList<qsizetype>{0});
        QCOMPARE(FontMatching::fuzzyRank(u8("mono"), List{{u8("Harmonoid"), u8("Monospace")}, {u8("DejaVu Sans Mono")}}),
                 (QList<qsizetype>{0, 1}));
        QCOMPARE(FontMatching::fuzzyRank(u8(""), List{{u8("b")}, {u8("a")}}), (QList<qsizetype>{0, 1}));
        QVERIFY(FontMatching::fuzzyRank(u8("zzz"), List{{u8("Arial")}}).isEmpty());
    }

    // --- FontCatalog against the installed fonts ---
    // CI containers have few fonts or none, so these assert only what holds
    // for any font set, the empty one included.

    void installedFamilies()
    {
        const FontCatalog catalog;
        const QStringList families = catalog.families();
        const QStringList all = QFontDatabase::families();
        QVERIFY(sortedCaseInsensitively(families));
        QCOMPARE(QSet<QString>(families.cbegin(), families.cend()).size(), families.size());
        for (const QString &family : families) {
            QVERIFY2(all.contains(family), qPrintable(family));
            QCOMPARE(catalog.resolveFamily(family), family);
            const QStringList alternates = catalog.alternateNames(family);
            QVERIFY2(!alternates.contains(family), qPrintable(family));
            QCOMPARE(QSet<QString>(alternates.cbegin(), alternates.cend()).size(), alternates.size());
            for (const QString &alternate : alternates) {
                QVERIFY2(!catalog.resolveFamily(alternate).isEmpty(), qPrintable(alternate));
            }
        }
        for (const char *generic : {"Sans Serif", "Serif", "Monospace"}) {
            QVERIFY2(!families.contains(u8(generic)), generic);
        }
        QCOMPARE(catalog.resolveFamily(QString()), QString());
        QCOMPARE(catalog.resolveFamily(u8("No Such Family 7f3c")), QString());
        QVERIFY(catalog.alternateNames(u8("No Such Family 7f3c")).isEmpty());
        QVERIFY(catalog.weights(u8("No Such Family 7f3c")).isEmpty());
        QVERIFY(catalog.weights(QString()).isEmpty());
        QVERIFY(catalog.italicWeights(u8("No Such Family 7f3c")).isEmpty());
        QVERIFY(catalog.italicWeights(QString()).isEmpty());
    }

    void installedWeights()
    {
        const FontCatalog catalog;
        for (const QString &family : catalog.families()) {
            const QVariantList weights = catalog.weights(family);
            QVERIFY2(!weights.isEmpty(), qPrintable(family));
            int previous = 0;
            for (const QVariant &entry : weights) {
                const QVariantMap face = entry.toMap();
                const int weight = face.value(QStringLiteral("weight")).toInt();
                const QString style = face.value(QStringLiteral("styleName")).toString();
                QVERIFY2(weight > previous, qPrintable(family + u8(": ") + style));
                previous = weight;
                QVERIFY2(QFontDatabase::styles(family).contains(style), qPrintable(family + u8(": ") + style));
                QVERIFY2(QFontDatabase::font(family, style, 12).style() == QFont::StyleNormal,
                         qPrintable(family + u8(": ") + style));
            }
        }
    }

    // Whatever is installed: each italic entry is a real face of the family
    // that Qt builds slanted, one per weight, lightest first, and the
    // weights are exactly those of the family's slanted faces as
    // QFontDatabase reports them, so a family with italics lists them and
    // one without lists none. A machine with no italic face at all checks
    // only the latter; installedDejaVuSansMonoItalics pins one family.
    void installedItalicWeights()
    {
        const FontCatalog catalog;
        for (const QString &family : catalog.families()) {
            QList<int> slanted;
            for (const QString &style : QFontDatabase::styles(family)) {
                const int weight = QFontDatabase::weight(family, style);
                if (weight > 0 && QFontDatabase::font(family, style, 12).style() != QFont::StyleNormal
                    && !slanted.contains(weight)) {
                    slanted.append(weight);
                }
            }
            std::sort(slanted.begin(), slanted.end());
            QList<int> listed;
            for (const QVariant &entry : catalog.italicWeights(family)) {
                listed.append(entry.toMap().value(QStringLiteral("weight")).toInt());
            }
            QCOMPARE(listed, slanted);

            int previous = 0;
            for (const QVariant &entry : catalog.italicWeights(family)) {
                const QVariantMap face = entry.toMap();
                const int weight = face.value(QStringLiteral("weight")).toInt();
                const QString style = face.value(QStringLiteral("styleName")).toString();
                QVERIFY2(weight > previous, qPrintable(family + u8(": ") + style));
                previous = weight;
                QVERIFY2(QFontDatabase::styles(family).contains(style), qPrintable(family + u8(": ") + style));
                QVERIFY2(QFontDatabase::font(family, style, 12).style() != QFont::StyleNormal,
                         qPrintable(family + u8(": ") + style));
            }
        }
    }

    // One family's two lists spelled out, so the catalog's choice between
    // its upright and its slanted faces is checked against known data. The
    // Debian CI container has DejaVu Sans Mono with exactly these four faces
    // (fc-list there: Book, Bold, Oblique, Bold Oblique), so it runs there
    // too.
    void installedDejaVuSansMonoItalics()
    {
        const FontCatalog catalog;
        const QString family = u8("DejaVu Sans Mono");
        const QStringList styles = QFontDatabase::styles(family);
        if (!catalog.families().contains(family) || !styles.contains(u8("Oblique"))
            || !styles.contains(u8("Bold Oblique"))) {
            QSKIP("DejaVu Sans Mono with its Oblique faces is not installed");
        }
        const auto faces = [](const QVariantList &list) {
            QStringList result;
            for (const QVariant &entry : list) {
                const QVariantMap face = entry.toMap();
                result.append(QString::number(face.value(QStringLiteral("weight")).toInt()) + QLatin1Char(' ')
                              + face.value(QStringLiteral("styleName")).toString());
            }
            return result;
        };
        QCOMPARE(faces(catalog.weights(family)), (QStringList{u8("400 Book"), u8("700 Bold")}));
        QCOMPARE(faces(catalog.italicWeights(family)), (QStringList{u8("400 Oblique"), u8("700 Bold Oblique")}));
    }

    // A Plasma font set to one of Qt's generic names keeps its stored weight:
    // Qt's one synthetic 400 face is not offered. Debian's CI container has
    // "Sans Serif" as its default font.
    void installedGenericFamiliesHaveNoWeights()
    {
        const FontCatalog catalog;
        const QStringList all = QFontDatabase::families();
        for (const char *generic : {"Sans Serif", "Serif", "Monospace"}) {
            if (!all.contains(u8(generic))) {
                continue;
            }
            QCOMPARE(catalog.resolveFamily(u8(generic)), u8(generic));
            QVERIFY2(catalog.weights(u8(generic)).isEmpty(), generic);
            QVERIFY2(catalog.italicWeights(u8(generic)).isEmpty(), generic);
            QCOMPARE(catalog.snapWeight(catalog.weights(u8(generic)), 700), 700);
        }
    }

    void installedSearch()
    {
        const FontCatalog catalog;
        const QStringList families = catalog.families();
        QCOMPARE(catalog.search(QString()), families);
        QCOMPARE(catalog.search(u8(" \t ")), families);
        QVERIFY(catalog.search(u8("\x01no such family\x01")).isEmpty());
        if (families.isEmpty()) {
            return;
        }
        const QString first = families.first();
        const QStringList found = catalog.search(first);
        QVERIFY2(found.contains(first), qPrintable(first));
        for (const QString &family : found) {
            QVERIFY2(families.contains(family), qPrintable(family));
        }
    }

    void installedDejaVuPrefersBook()
    {
        const QStringList styles = QFontDatabase::styles(u8("DejaVu Sans"));
        if (!styles.contains(u8("Book")) || !styles.contains(u8("Condensed"))) {
            QSKIP("DejaVu Sans with both Book and Condensed faces is not installed");
        }
        const FontCatalog catalog;
        bool found = false;
        for (const QVariant &entry : catalog.weights(u8("DejaVu Sans"))) {
            const QVariantMap face = entry.toMap();
            if (face.value(QStringLiteral("weight")).toInt() == 400) {
                QCOMPARE(face.value(QStringLiteral("styleName")).toString(), u8("Book"));
                found = true;
            }
        }
        QVERIFY(found);
        QVERIFY(!catalog.alternateNames(u8("DejaVu Sans")).contains(u8("DejaVu Sans Condensed")));
    }

    // Runs under LC_ALL=C.UTF-8, where Qt lists the English name.
    void installedYaHeiAlias()
    {
        const FontCatalog catalog;
        if (!catalog.families().contains(u8("Microsoft YaHei"))) {
            QSKIP("Microsoft YaHei is not installed");
        }
        QVERIFY(catalog.alternateNames(u8("Microsoft YaHei")).contains(u8("微软雅黑")));
        QCOMPARE(catalog.resolveFamily(u8("微软雅黑")), u8("Microsoft YaHei"));
        QVERIFY(catalog.search(u8("雅黑")).contains(u8("Microsoft YaHei")));
        QVERIFY(!catalog.weights(u8("微软雅黑")).isEmpty());
    }
};

QTEST_MAIN(FontCatalogTest)
#include "tst_fontcatalog.moc"
