// SPDX-License-Identifier: GPL-2.0-only
//
// Guards contents/config/main.xml, the applet's KConfig schema. Plasma loads
// it through KConfigLoader, whose ConfigLoaderHandler::parse() stops at the
// first QXmlStreamReader error and returns false WITHOUT logging anything.
// Every <entry> after the error then silently ceases to exist: the applet
// reads undefined for those keys, the config dialog initialises their cfg_
// properties to 0/false/"" and writes those back on Save, and nothing in
// qmllint or the QML test suite notices because neither of them opens this
// file. A stray "--" inside an XML comment shipped exactly that way once
// (v0.4.0's first cut), taking every panel* key with it.
//
// The parse below mirrors ConfigLoaderHandler::parse() -- same reader, same
// loop shape -- so what passes here is what plasmashell will see.

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QXmlStreamReader>

namespace {

const QString packageDir = QStringLiteral(PLASMOID_PACKAGE_DIR);

QString schemaPath()
{
    return packageDir + QStringLiteral("/contents/config/main.xml");
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

// Names of every <entry> the reader gets to before it stops. Error details
// come back through the out-parameters so the assertion can quote them.
QStringList parsedEntryNames(const QByteArray &xml, bool *hadError, QString *errorText, qint64 *errorLine)
{
    QStringList names;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.hasError()) {
            break;
        }
        if (reader.isStartElement() && reader.name() == QLatin1String("entry")) {
            names << reader.attributes().value(QLatin1String("name")).toString();
        }
    }
    *hadError = reader.hasError();
    *errorText = reader.errorString();
    *errorLine = reader.lineNumber();
    return names;
}

struct SchemaEntry {
    QString type;
    QString defaultValue;
};

QHash<QString, SchemaEntry> parsedEntries(const QByteArray &xml)
{
    QHash<QString, SchemaEntry> entries;
    QXmlStreamReader reader(xml);
    QString current;
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) {
            continue;
        }
        if (reader.name() == QLatin1String("entry")) {
            current = reader.attributes().value(QLatin1String("name")).toString();
            entries.insert(current, {reader.attributes().value(QLatin1String("type")).toString(), QString()});
        } else if (reader.name() == QLatin1String("default") && !current.isEmpty()) {
            entries[current].defaultValue = reader.readElementText();
        }
    }
    return entries;
}

// One row of ThemePolicy.js's darkDefaults table: a suffix of one form
// factor and its dark key's default, unquoted.
struct ThemedKey {
    QString form;
    QString suffix;
    QString defaultValue;
};

// The darkDefaults table, read line by line between its marker comments --
// the layout ThemePolicy.js promises to keep. Empty when the markers are gone.
QList<ThemedKey> themeTable()
{
    const QString policy = QString::fromUtf8(readAll(packageDir + QStringLiteral("/contents/ui/ThemePolicy.js")));
    const qsizetype begin = policy.indexOf(QStringLiteral("// BEGIN darkDefaults"));
    const qsizetype end = policy.indexOf(QStringLiteral("// END darkDefaults"));
    if (begin < 0 || end <= begin) {
        return {};
    }

    const QRegularExpression formLine(QStringLiteral("^ {4}(desktop|panel): \\{$"));
    const QRegularExpression suffixLine(QStringLiteral("^ {8}([A-Za-z]+): (.+),$"));
    QList<ThemedKey> table;
    QString form;
    const QStringList lines = policy.mid(begin, end - begin).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (const auto match = formLine.match(line); match.hasMatch()) {
            form = match.captured(1);
            continue;
        }
        const auto match = suffixLine.match(line);
        if (!match.hasMatch() || form.isEmpty()) {
            continue;
        }
        QString value = match.captured(2);
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) {
            value = value.mid(1, value.size() - 2);
        }
        table.append({form, match.captured(1), value});
    }
    return table;
}

// The `{ ... }` block whose opening brace is at `open`, braces included, or
// empty when it never closes. Plain counting, which a brace inside a string
// or comment would throw off -- main.qml has none, and one would make the
// test that reads the block fail rather than pass.
QString braceBlock(const QString &text, qsizetype open)
{
    int depth = 0;
    for (qsizetype i = open; i >= 0 && i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('{')) {
            ++depth;
        } else if (text.at(i) == QLatin1Char('}') && --depth == 0) {
            return text.mid(open, i - open + 1);
        }
    }
    return {};
}

QStringList filesUnder(const QString &dir, const QStringList &nameFilters)
{
    QStringList result;
    QDirIterator it(dir, nameFilters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        result << it.next();
    }
    result.sort();
    return result;
}

} // namespace

class ConfigSchemaTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void schemaParsesToTheLastEntry()
    {
        const QByteArray xml = readAll(schemaPath());
        QVERIFY2(!xml.isEmpty(), qPrintable(QStringLiteral("cannot read %1").arg(schemaPath())));

        bool hadError = false;
        QString errorText;
        qint64 errorLine = 0;
        const QStringList parsed = parsedEntryNames(xml, &hadError, &errorText, &errorLine);
        QVERIFY2(!hadError,
                 qPrintable(QStringLiteral("main.xml line %1: %2 -- KConfigLoader stops here and every later "
                                           "<entry> is silently dropped (%3 parsed so far, last one \"%4\")")
                                .arg(errorLine)
                                .arg(errorText)
                                .arg(parsed.size())
                                .arg(parsed.isEmpty() ? QString() : parsed.constLast())));

        // Textual count as the reference: independent of the XML parser, so a
        // parser that quietly stops early (rather than erroring) is caught too.
        const QRegularExpression entryTag(QStringLiteral("<entry\\s"));
        const int textual = static_cast<int>(QString::fromUtf8(xml).count(entryTag));
        QVERIFY(textual > 0);
        QCOMPARE(parsed.size(), textual);

        for (const QString &name : parsed) {
            QVERIFY2(!name.isEmpty(), "an <entry> without a name attribute");
        }
        QCOMPARE(QSet<QString>(parsed.cbegin(), parsed.cend()).size(), parsed.size());
    }

    // Every key the QML side asks for has to be declared, or it reads as
    // undefined at runtime with the same silence as a truncated schema.
    // Covers Plasmoid.configuration.<key> reads in every ui/ QML file,
    // configuration.<key> in ui/ JS files (TextPolicy.js takes the map as a
    // parameter), and cfg_<key> property declarations in the config pages.
    // Deliberately not the reverse direction: a key can legitimately be read
    // only through an indirect path this scan cannot see.
    void everyReferencedKeyIsDeclared()
    {
        const QByteArray xml = readAll(schemaPath());
        bool hadError = false;
        QString errorText;
        qint64 errorLine = 0;
        const QStringList parsed = parsedEntryNames(xml, &hadError, &errorText, &errorLine);
        QVERIFY(!hadError);
        const QSet<QString> declared(parsed.cbegin(), parsed.cend());

        const QString uiDir = packageDir + QStringLiteral("/contents/ui");
        const QRegularExpression qmlRead(QStringLiteral("\\bPlasmoid\\.configuration\\.([A-Za-z_][A-Za-z0-9_]*)"));
        const QRegularExpression jsRead(QStringLiteral("\\bconfiguration\\.([A-Za-z_][A-Za-z0-9_]*)"));
        const QRegularExpression cfgDeclaration(
            QStringLiteral("\\bproperty\\s+(?:alias\\s+)?[A-Za-z_][A-Za-z0-9_.]*\\s+cfg_([A-Za-z_][A-Za-z0-9_]*)"));

        QStringList missing;
        int referencesSeen = 0;
        const auto scan = [&](const QString &path, const QRegularExpression &pattern) {
            const QString text = QString::fromUtf8(readAll(path));
            auto it = pattern.globalMatch(text);
            while (it.hasNext()) {
                const QString key = it.next().captured(1);
                ++referencesSeen;
                if (!declared.contains(key)) {
                    missing << QStringLiteral("%1: %2").arg(QDir(packageDir).relativeFilePath(path), key);
                }
            }
        };
        for (const QString &path : filesUnder(uiDir, {QStringLiteral("*.qml")})) {
            scan(path, qmlRead);
            scan(path, cfgDeclaration);
        }
        for (const QString &path : filesUnder(uiDir, {QStringLiteral("*.js")})) {
            scan(path, jsRead);
        }

        QVERIFY2(referencesSeen > 0, "the scan found no configuration references at all -- pattern or path drift");
        missing.removeDuplicates();
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("referenced but not declared in main.xml:\n  %1")
                                .arg(missing.join(QStringLiteral("\n  ")))));
    }

    // DESIGN.md decision 76. The light set is read through keys built at run
    // time (AppearanceTheme.value(), the pages' sync), which the scan above
    // cannot see, so ThemePolicy.js's table stands in for them: every suffix
    // it lists needs its dark key, with the default the table gives for it,
    // and a light key of the same type; and no light key may exist without a
    // suffix in the table.
    void themeTableMatchesSchema()
    {
        const QHash<QString, SchemaEntry> entries = parsedEntries(readAll(schemaPath()));
        QVERIFY(!entries.isEmpty());

        const QList<ThemedKey> table = themeTable();
        QVERIFY2(!table.isEmpty(), "ThemePolicy.js lost its darkDefaults markers");

        QHash<QString, QStringList> suffixes;
        QStringList problems;
        for (const ThemedKey &row : table) {
            const QString &form = row.form;
            const QString &suffix = row.suffix;
            const QString &value = row.defaultValue;
            suffixes[form] << suffix;

            const QString darkKey = form + suffix;
            const QString lightKey = form + QStringLiteral("Light") + suffix;
            if (!entries.contains(darkKey)) {
                problems << QStringLiteral("%1: not declared").arg(darkKey);
                continue;
            }
            if (entries.value(darkKey).defaultValue != value) {
                problems << QStringLiteral("%1: main.xml default \"%2\", ThemePolicy.js \"%3\"")
                                .arg(darkKey, entries.value(darkKey).defaultValue, value);
            }
            if (!entries.contains(lightKey)) {
                problems << QStringLiteral("%1: not declared").arg(lightKey);
            } else if (entries.value(lightKey).type != entries.value(darkKey).type) {
                problems << QStringLiteral("%1: type %2, %3 is %4")
                                .arg(lightKey, entries.value(lightKey).type, darkKey, entries.value(darkKey).type);
            }
        }
        QCOMPARE(suffixes.value(QStringLiteral("desktop")).size(), 28);
        QCOMPARE(suffixes.value(QStringLiteral("panel")).size(), 26);

        for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
            for (const QString &prefix : {QStringLiteral("desktopLight"), QStringLiteral("panelLight")}) {
                if (it.key().startsWith(prefix)) {
                    const QString formOf = prefix.chopped(5);
                    if (!suffixes.value(formOf).contains(it.key().mid(prefix.size()))) {
                        problems << QStringLiteral("%1: no suffix in ThemePolicy.js").arg(it.key());
                    }
                }
            }
        }
        for (const QString &key : {QStringLiteral("desktopColorSchemeMode"), QStringLiteral("panelColorSchemeMode")}) {
            if (entries.value(key).defaultValue != QLatin1String("auto")) {
                problems << QStringLiteral("%1: missing or not defaulting to auto").arg(key);
            }
        }

        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral("\n  ")).prepend(QStringLiteral("\n  "))));
    }

    // DESIGN.md decision 76's light defaults, derived from the dark ones so
    // that a light key whose default drifts is caught -- nothing else reads
    // them, and every other test passes with any value there. Non-colour
    // keys match their dark key, except that brightening is off (it fades
    // towards opaque white, which on dark text makes the sung word the
    // palest one). The plate is #99ffffff and both strokes #ccffffff; every
    // other colour is the warm near-black #1f1b16 at the dark key's alpha,
    // opaque when the dark key has none.
    void lightDefaultsFollowTheDarkOnes()
    {
        const QHash<QString, SchemaEntry> entries = parsedEntries(readAll(schemaPath()));
        const QList<ThemedKey> table = themeTable();
        QVERIFY(!entries.isEmpty());
        QVERIFY(!table.isEmpty());

        const QHash<QString, QString> fixed = {
            {QStringLiteral("SolidColor"), QStringLiteral("#99ffffff")},
            {QStringLiteral("StrokeColor"), QStringLiteral("#ccffffff")},
            {QStringLiteral("TrackInfoStrokeColor"), QStringLiteral("#ccffffff")},
            {QStringLiteral("WordBrightness"), QStringLiteral("false")},
        };
        const QRegularExpression colour(QStringLiteral("^#([0-9a-f]{2})?[0-9a-f]{6}$"));
        QStringList problems;
        for (const ThemedKey &row : table) {
            QString expected = fixed.value(row.suffix, row.defaultValue);
            if (!fixed.contains(row.suffix)) {
                if (const auto match = colour.match(row.defaultValue); match.hasMatch()) {
                    expected = QLatin1Char('#') + match.captured(1) + QStringLiteral("1f1b16");
                }
            }
            const QString lightKey = row.form + QStringLiteral("Light") + row.suffix;
            const QString actual = entries.value(lightKey).defaultValue;
            if (actual != expected) {
                problems << QStringLiteral("%1: default \"%2\", expected \"%3\"").arg(lightKey, actual, expected);
            }
        }
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral("\n  ")).prepend(QStringLiteral("\n  "))));
    }

    // DESIGN.md decision 76, main.qml's side. main.qml is a PlasmoidItem the
    // QML suite cannot instantiate, and qmllint cannot tell one key name from
    // another, so this reads its text. Each form factor has one
    // AppearanceTheme, id <form>Theme, over Plasmoid.configuration and fed
    // the root's plasmaStyleDark -- Kirigami.Theme reports the Plasma style
    // only on an Item of the widget, which a QtObject is not; every
    // value() call on it passes a literal suffix from that form's table; each
    // representation reads every suffix of its own form and nothing from the
    // other form's theme; and no themed key, dark or light, is read straight
    // off Plasmoid.configuration, which would render that one set whatever
    // the Plasma style.
    void mainReadsThemedKeysThroughItsTheme()
    {
        const QList<ThemedKey> table = themeTable();
        QVERIFY(!table.isEmpty());
        QHash<QString, QStringList> suffixes;
        QSet<QString> themedKeys;
        for (const ThemedKey &row : table) {
            suffixes[row.form] << row.suffix;
            themedKeys << row.form + row.suffix << row.form + QStringLiteral("Light") + row.suffix;
        }

        const QString mainPath = packageDir + QStringLiteral("/contents/ui/main.qml");
        const QString main = QString::fromUtf8(readAll(mainPath));
        QVERIFY2(!main.isEmpty(), qPrintable(QStringLiteral("cannot read %1").arg(mainPath)));
        QStringList problems;

        const QRegularExpression themeDeclaration(QStringLiteral("\\bAppearanceTheme\\s*\\{"));
        const QRegularExpression idLine(QStringLiteral("\\bid:\\s*(\\w+)"));
        const QRegularExpression formFactorLine(QStringLiteral("\\bformFactor:\\s*\"(\\w*)\""));
        const QRegularExpression configurationLine(QStringLiteral("\\bconfiguration:\\s*Plasmoid\\.configuration\\s*\\n"));
        const QRegularExpression styleDarkLine(QStringLiteral("\\bstyleDark:\\s*root\\.plasmaStyleDark\\s*\\n"));
        QStringList themeIds;
        for (auto it = themeDeclaration.globalMatch(main); it.hasNext();) {
            const QString block = braceBlock(main, it.next().capturedEnd() - 1);
            const QString id = idLine.match(block).captured(1);
            const QString form = formFactorLine.match(block).captured(1);
            if (id != form + QStringLiteral("Theme")) {
                problems << QStringLiteral("AppearanceTheme id \"%1\" with formFactor \"%2\"").arg(id, form);
            }
            if (!configurationLine.match(block).hasMatch()) {
                problems << QStringLiteral("%1: configuration is not Plasmoid.configuration").arg(id);
            }
            if (!styleDarkLine.match(block).hasMatch()) {
                problems << QStringLiteral("%1: styleDark is not root.plasmaStyleDark").arg(id);
            }
            themeIds << id;
        }
        themeIds.sort();
        QCOMPARE(themeIds, QStringList({QStringLiteral("desktopTheme"), QStringLiteral("panelTheme")}));

        // Every call, literal argument or not, so a suffix built at run time
        // is reported rather than skipped.
        const QRegularExpression valueCall(QStringLiteral("\\b(desktop|panel)Theme\\.value\\(([^)]*)\\)"));
        const QRegularExpression literalSuffix(QStringLiteral("^\"([A-Za-z]+)\"$"));
        for (auto it = valueCall.globalMatch(main); it.hasNext();) {
            const auto call = it.next();
            const auto suffix = literalSuffix.match(call.captured(2));
            if (!suffix.hasMatch()) {
                problems << QStringLiteral("%1: not a literal suffix").arg(call.captured(0));
            } else if (!suffixes.value(call.captured(1)).contains(suffix.captured(1))) {
                problems << QStringLiteral("%1: no such suffix in the %2 table").arg(call.captured(0), call.captured(1));
            }
        }

        struct Representation {
            QString property;
            QString form;
            QString otherForm;
        };
        const Representation representations[] = {
            {QStringLiteral("compactRepresentation"), QStringLiteral("panel"), QStringLiteral("desktop")},
            {QStringLiteral("fullRepresentation"), QStringLiteral("desktop"), QStringLiteral("panel")},
        };
        for (const Representation &representation : representations) {
            const QString opening = representation.property + QStringLiteral(": LyricsView {");
            const qsizetype at = main.indexOf(opening);
            const QString block = at < 0 ? QString() : braceBlock(main, at + opening.size() - 1);
            if (block.isEmpty()) {
                problems << QStringLiteral("no complete \"%1\" block").arg(opening);
                continue;
            }
            QSet<QString> read;
            for (auto it = valueCall.globalMatch(block); it.hasNext();) {
                const auto call = it.next();
                if (call.captured(1) == representation.form) {
                    read << literalSuffix.match(call.captured(2)).captured(1);
                }
            }
            for (const QString &suffix : suffixes.value(representation.form)) {
                if (!read.contains(suffix)) {
                    problems << QStringLiteral("%1 never reads %2Theme.value(\"%3\")")
                                    .arg(representation.property, representation.form, suffix);
                }
            }
            if (block.contains(representation.otherForm + QStringLiteral("Theme."))) {
                problems << QStringLiteral("%1 reads %2Theme").arg(representation.property, representation.otherForm);
            }
            for (const QString &wiring : {QStringLiteral("animateColors: %1Theme.transitioning\n"),
                                          QStringLiteral("colorTransitionMs: %1Theme.transitionMs\n")}) {
                if (!block.contains(wiring.arg(representation.form))) {
                    problems << QStringLiteral("%1 lacks \"%2\"").arg(representation.property, wiring.arg(representation.form).trimmed());
                }
            }
        }

        const QRegularExpression configurationRead(QStringLiteral("\\bPlasmoid\\.configuration\\.([A-Za-z_][A-Za-z0-9_]*)"));
        for (auto it = configurationRead.globalMatch(main); it.hasNext();) {
            const QString key = it.next().captured(1);
            if (themedKeys.contains(key)) {
                problems << QStringLiteral("Plasmoid.configuration.%1: read past the theme").arg(key);
            }
        }

        problems.removeDuplicates();
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral("\n  ")).prepend(QStringLiteral("\n  "))));
    }
};

QTEST_GUILESS_MAIN(ConfigSchemaTest)

#include "tst_configschema.moc"
