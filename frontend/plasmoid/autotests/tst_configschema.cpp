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
};

QTEST_GUILESS_MAIN(ConfigSchemaTest)

#include "tst_configschema.moc"
