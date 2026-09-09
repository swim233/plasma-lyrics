#include "ttmlparser.h"

#include <QJsonArray>
#include <QSet>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>

namespace PlasmaLyrics {
namespace {

QString attribute(const QXmlStreamAttributes &attributes, QStringView name)
{
    for (const auto &item : attributes) {
        if (item.name() == name) {
            return item.value().toString();
        }
    }
    return {};
}

QString roleOf(const QXmlStreamAttributes &attributes)
{
    return attribute(attributes, QStringLiteral("role")).toCaseFolded();
}

bool isTranslationRole(const QString &role)
{
    return role == QStringLiteral("x-translation") || role == QStringLiteral("translation");
}

bool isIgnoredRole(const QString &role)
{
    return role == QStringLiteral("x-bg") || role == QStringLiteral("x-roman")
        || role == QStringLiteral("x-romanization") || role == QStringLiteral("x-pronunciation");
}

QString collectElementText(QXmlStreamReader &reader)
{
    QString text;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString role = roleOf(reader.attributes());
            if (isIgnoredRole(role)) {
                collectElementText(reader);
            } else if (reader.name() == QStringLiteral("br")) {
                text += QLatin1Char('\n');
                collectElementText(reader);
            } else {
                text += collectElementText(reader);
            }
        } else if (reader.isEndElement()) {
            return text;
        } else if (reader.isCharacters()) {
            const QString characters = reader.text().toString();
            if (!reader.isWhitespace()
                || (!characters.contains(QLatin1Char('\n'))
                    && !characters.contains(QLatin1Char('\r')))) {
                // Preserve an intentional single-line gap between spans in
                // auxiliary text as well as in the main lyric. Indentation
                // that contains a line break remains formatting only.
                text += characters;
            }
        }
    }
    return text;
}

struct Translation {
    QString language;
    QString text;
};

struct Paragraph {
    QString key;
    LyricLine line;
    QList<Translation> translations;
};

int languagePriority(QString language)
{
    language.replace(QLatin1Char('_'), QLatin1Char('-'));
    language = language.toCaseFolded();
    const QStringList subtags = language.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    const QString primary = subtags.value(0);
    if (primary == QStringLiteral("zh")) {
        // BCP-47 permits both script and region after the primary language,
        // for example zh-Hans-CN. Prefer Simplified Chinese regardless of
        // where a later region subtag appears, then generic Chinese, then
        // Traditional Chinese.
        if (subtags.contains(QStringLiteral("hans"))
            || subtags.contains(QStringLiteral("cn"))
            || subtags.contains(QStringLiteral("sg"))) {
            return 0;
        }
        if (subtags.contains(QStringLiteral("hant"))
            || subtags.contains(QStringLiteral("tw"))
            || subtags.contains(QStringLiteral("hk"))
            || subtags.contains(QStringLiteral("mo"))) {
            return 2;
        }
        return 1;
    }
    if (primary == QStringLiteral("en")) return 3;
    if (primary == QStringLiteral("ja")) return 4;
    return 10;
}

std::optional<QString> chooseTranslation(QList<Translation> translations)
{
    QSet<QString> seen;
    translations.erase(std::remove_if(translations.begin(), translations.end(), [&seen](const auto &item) {
        const QString text = item.text.simplified();
        if (text.isEmpty() || seen.contains(text)) {
            return true;
        }
        seen.insert(text);
        return false;
    }), translations.end());
    if (translations.isEmpty()) {
        return std::nullopt;
    }
    std::sort(translations.begin(), translations.end(), [](const auto &left, const auto &right) {
        const int leftPriority = languagePriority(left.language);
        const int rightPriority = languagePriority(right.language);
        if (leftPriority != rightPriority) return leftPriority < rightPriority;
        if (left.language != right.language) return left.language < right.language;
        return left.text < right.text;
    });
    return translations.first().text.simplified();
}

QString consumeMainSpan(QXmlStreamReader &reader, QList<Translation> &translations)
{
    QString text;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isEndElement()) {
            return text;
        }
        if (reader.isCharacters()) {
            // Whitespace inside a timed span is content (AMLL commonly puts
            // the inter-word space at the start of the next span).
            text += reader.text();
            continue;
        }
        if (!reader.isStartElement()) {
            continue;
        }
        const auto attributes = reader.attributes();
        const QString role = roleOf(attributes);
        if (isTranslationRole(role)) {
            translations.append({attribute(attributes, QStringLiteral("lang")),
                                 collectElementText(reader)});
        } else if (isIgnoredRole(role)) {
            collectElementText(reader);
        } else if (reader.name() == QStringLiteral("br")) {
            text += QLatin1Char('\n');
            collectElementText(reader);
        } else {
            text += consumeMainSpan(reader, translations);
        }
    }
    return text;
}

QString paragraphKey(const QXmlStreamAttributes &attributes)
{
    const QString key = attribute(attributes, QStringLiteral("key"));
    return key.isEmpty() ? attribute(attributes, QStringLiteral("id")) : key;
}

void parseOfficialTranslation(QXmlStreamReader &reader,
                              QHash<QString, QList<Translation>> &translations)
{
    const QString language = attribute(reader.attributes(), QStringLiteral("lang"));
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isEndElement() && reader.name() == QStringLiteral("translation")) {
            return;
        }
        if (!reader.isStartElement() || reader.name() != QStringLiteral("text")) {
            continue;
        }
        const QString key = attribute(reader.attributes(), QStringLiteral("for"));
        const QString text = collectElementText(reader).trimmed();
        if (!key.isEmpty() && !text.isEmpty()) {
            translations[key].append({language, text});
        }
    }
}

std::optional<Paragraph> parseMainParagraph(QXmlStreamReader &reader, bool *timelineError)
{
    const auto attributes = reader.attributes();
    const auto begin = TtmlParser::parseTime(attribute(attributes, QStringLiteral("begin")));
    const auto end = TtmlParser::parseTime(attribute(attributes, QStringLiteral("end")));
    Paragraph paragraph;
    paragraph.key = paragraphKey(attributes);
    QString text;
    QString pendingInlineWhitespace;
    QList<LyricWord> words;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isEndElement()) {
            break;
        }
        if (reader.isCharacters()) {
            const QString characters = reader.text().toString();
            if (!reader.isWhitespace()) {
                text += characters;
            } else if (!characters.contains(QLatin1Char('\n'))
                       && !characters.contains(QLatin1Char('\r'))) {
                // A literal space between adjacent timed spans is lyric
                // content. Formatting indentation contains a line break and
                // remains ignored.
                pendingInlineWhitespace += characters;
            }
            continue;
        }
        if (!reader.isStartElement()) continue;

        const auto childAttributes = reader.attributes();
        const QString role = roleOf(childAttributes);
        if (isTranslationRole(role)) {
            paragraph.translations.append({attribute(childAttributes, QStringLiteral("lang")),
                                           collectElementText(reader)});
            continue;
        }
        if (isIgnoredRole(role)) {
            collectElementText(reader);
            continue;
        }
        if (reader.name() == QStringLiteral("br")) {
            text += pendingInlineWhitespace;
            pendingInlineWhitespace.clear();
            text += QLatin1Char('\n');
            collectElementText(reader);
            continue;
        }
        const auto wordBegin = TtmlParser::parseTime(attribute(childAttributes, QStringLiteral("begin")));
        const auto wordEnd = TtmlParser::parseTime(attribute(childAttributes, QStringLiteral("end")));
        QString wordText = consumeMainSpan(reader, paragraph.translations);
        if (!pendingInlineWhitespace.isEmpty()) {
            wordText.prepend(pendingInlineWhitespace);
            pendingInlineWhitespace.clear();
        }
        text += wordText;
        if (wordBegin && wordEnd && *wordEnd > *wordBegin && !wordText.isEmpty()) {
            words.append({*wordBegin, *wordEnd, wordText});
        }
    }
    text = text.trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    if (!begin || !end || *begin < 0 || *end <= *begin) {
        *timelineError = true;
        return std::nullopt;
    }
    paragraph.line.startMs = *begin;
    paragraph.line.endMs = *end;
    paragraph.line.text = text;
    if (!words.isEmpty()) {
        paragraph.line.words = words;
    }
    return paragraph;
}

void appendMetadata(QJsonObject &metadata, const QString &key, const QString &value)
{
    if (key.isEmpty() || value.isEmpty()) return;
    QJsonArray values = metadata.value(key).toArray();
    if (!values.contains(value)) {
        values.append(value);
        metadata.insert(key, values);
    }
}

} // namespace

std::optional<qint64> TtmlParser::parseTime(const QString &rawValue)
{
    const QString value = rawValue.trimmed();
    if (value.isEmpty()) return std::nullopt;
    bool ok = false;
    double milliseconds = 0;
    if (value.endsWith(QStringLiteral("ms"), Qt::CaseInsensitive)) {
        milliseconds = value.chopped(2).toDouble(&ok);
    } else if (value.endsWith(QLatin1Char('s'), Qt::CaseInsensitive)) {
        milliseconds = value.chopped(1).toDouble(&ok) * 1000.0;
    } else if (!value.contains(QLatin1Char(':'))) {
        // AMLL's canonical TTML also uses bare decimal seconds ("45.404")
        // in addition to TTML clock time and explicit s/ms suffixes.
        milliseconds = value.toDouble(&ok) * 1000.0;
    } else {
        const auto parts = value.split(QLatin1Char(':'));
        if (parts.size() != 2 && parts.size() != 3) return std::nullopt;
        const double seconds = parts.last().toDouble(&ok);
        if (!ok || seconds < 0 || seconds >= 60) return std::nullopt;
        bool minuteOk = false;
        const qint64 minutes = parts[parts.size() - 2].toLongLong(&minuteOk);
        if (!minuteOk || minutes < 0 || (parts.size() == 3 && minutes >= 60)) return std::nullopt;
        qint64 hours = 0;
        if (parts.size() == 3) {
            bool hourOk = false;
            hours = parts.first().toLongLong(&hourOk);
            if (!hourOk || hours < 0) return std::nullopt;
        }
        milliseconds = (static_cast<double>(hours * 3600 + minutes * 60) + seconds) * 1000.0;
        ok = true;
    }
    if (!ok || !std::isfinite(milliseconds) || milliseconds < 0
        || milliseconds > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return std::nullopt;
    }
    return static_cast<qint64>(std::llround(milliseconds));
}

std::optional<LyricDocument> TtmlParser::parse(const QByteArray &payload, QString *error)
{
    QXmlStreamReader reader(payload);
    QList<Paragraph> paragraphs;
    QHash<QString, QList<Translation>> associatedTranslations;
    QJsonObject metadata;
    bool inBody = false;
    bool sawRoot = false;
    bool timelineError = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == QStringLiteral("tt")) sawRoot = true;
            if (reader.name() == QStringLiteral("body")) inBody = true;
            if (reader.name() == QStringLiteral("meta")) {
                appendMetadata(metadata,
                               attribute(reader.attributes(), QStringLiteral("key")),
                               attribute(reader.attributes(), QStringLiteral("value")));
                continue;
            }
            if (!inBody && reader.name() == QStringLiteral("translation")) {
                parseOfficialTranslation(reader, associatedTranslations);
                continue;
            }
            if (reader.name() != QStringLiteral("p")) continue;

            const QString role = roleOf(reader.attributes());
            const QString key = paragraphKey(reader.attributes());
            if (!inBody || isTranslationRole(role)) {
                Translation translation{attribute(reader.attributes(), QStringLiteral("lang")),
                                        collectElementText(reader)};
                if (!key.isEmpty() && !translation.text.trimmed().isEmpty()) {
                    associatedTranslations[key].append(std::move(translation));
                }
                continue;
            }
            if (isIgnoredRole(role)) {
                collectElementText(reader);
                continue;
            }
            if (auto paragraph = parseMainParagraph(reader, &timelineError)) {
                paragraphs.append(std::move(*paragraph));
            }
        } else if (reader.isEndElement() && reader.name() == QStringLiteral("body")) {
            inBody = false;
        }
    }

    if (reader.hasError()) {
        if (error) *error = reader.errorString();
        return std::nullopt;
    }
    if (!sawRoot) {
        if (error) *error = QStringLiteral("document is not TTML");
        return std::nullopt;
    }
    if (timelineError) {
        if (error) *error = QStringLiteral("main lyric contains an invalid timeline");
        return std::nullopt;
    }
    if (paragraphs.isEmpty()) {
        if (error) *error = QStringLiteral("document has no valid main lyrics");
        return std::nullopt;
    }

    LyricDocument document;
    document.metadata = metadata;
    for (auto &paragraph : paragraphs) {
        paragraph.translations.append(associatedTranslations.value(paragraph.key));
        paragraph.line.translation = chooseTranslation(paragraph.translations);
        document.hasWords |= paragraph.line.words.has_value() && !paragraph.line.words->isEmpty();
        document.lines.append(std::move(paragraph.line));
    }
    std::stable_sort(document.lines.begin(), document.lines.end(), [](const auto &left, const auto &right) {
        if (left.startMs != right.startMs) return left.startMs < right.startMs;
        return left.endMs < right.endMs;
    });
    return document;
}

} // namespace PlasmaLyrics
