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

    // DESIGN.md decision 75. The light set is read through keys built at run
    // time (AppearanceTheme.value(), the pages' sync), which the scan above
    // cannot see, so ThemePolicy.js's table stands in for them: every suffix
    // it lists needs its dark key, with the default the table gives for it,
    // and a light key of the same type; and no light key may exist without a
    // suffix in the table.
    void themeTableMatchesSchema()
    {
        const QHash<QString, SchemaEntry> entries = parsedEntries(readAll(schemaPath()));
        QVERIFY(!entries.isEmpty());

        const QString policyPath = packageDir + QStringLiteral("/contents/ui/ThemePolicy.js");
        const QString policy = QString::fromUtf8(readAll(policyPath));
        const qsizetype begin = policy.indexOf(QStringLiteral("// BEGIN darkDefaults"));
        const qsizetype end = policy.indexOf(QStringLiteral("// END darkDefaults"));
        QVERIFY2(begin >= 0 && end > begin, "ThemePolicy.js lost its darkDefaults markers");

        const QRegularExpression formLine(QStringLiteral("^ {4}(desktop|panel): \\{$"));
        const QRegularExpression suffixLine(QStringLiteral("^ {8}([A-Za-z]+): (.+),$"));
        QHash<QString, QStringList> suffixes;
        QStringList problems;
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
            const QString suffix = match.captured(1);
            QString value = match.captured(2);
            if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) {
                value = value.mid(1, value.size() - 2);
            }
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
};

QTEST_GUILESS_MAIN(ConfigSchemaTest)

#include "tst_configschema.moc"
