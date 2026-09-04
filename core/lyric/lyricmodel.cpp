#include "lyricmodel.h"

#include <QJsonArray>
#include <QJsonValue>

namespace PlasmaLyrics {

QJsonObject wordToJson(const LyricWord &word)
{
    return {{QStringLiteral("startMs"), word.startMs},
            {QStringLiteral("endMs"), word.endMs},
            {QStringLiteral("text"), word.text}};
}

std::optional<LyricWord> wordFromJson(const QJsonObject &object)
{
    if (!object.contains(QStringLiteral("startMs")) || !object.contains(QStringLiteral("text"))) {
        return std::nullopt;
    }
    return LyricWord{object.value(QStringLiteral("startMs")).toInteger(),
                     object.value(QStringLiteral("endMs")).toInteger(),
                     object.value(QStringLiteral("text")).toString()};
}

QJsonObject lineToJson(const LyricLine &line)
{
    QJsonObject object{{QStringLiteral("startMs"), line.startMs},
                       {QStringLiteral("endMs"), line.endMs},
                       {QStringLiteral("text"), line.text}};
    object.insert(QStringLiteral("translation"), line.translation ? QJsonValue(*line.translation)
                                                                    : QJsonValue::Null);
    if (line.words) {
        QJsonArray words;
        for (const auto &word : *line.words) {
            words.append(wordToJson(word));
        }
        object.insert(QStringLiteral("words"), words);
    } else {
        object.insert(QStringLiteral("words"), QJsonValue::Null);
    }
    if (line.credit) {
        object.insert(QStringLiteral("credit"), true);
    }
    return object;
}

std::optional<LyricLine> lineFromJson(const QJsonObject &object)
{
    if (!object.contains(QStringLiteral("startMs")) || !object.contains(QStringLiteral("text"))) {
        return std::nullopt;
    }
    LyricLine line;
    line.startMs = object.value(QStringLiteral("startMs")).toInteger();
    line.endMs = object.value(QStringLiteral("endMs")).toInteger();
    line.text = object.value(QStringLiteral("text")).toString();
    const auto translation = object.value(QStringLiteral("translation"));
    if (translation.isString()) {
        line.translation = translation.toString();
    }
    const auto wordsValue = object.value(QStringLiteral("words"));
    if (wordsValue.isArray()) {
        QList<LyricWord> words;
        for (const auto &value : wordsValue.toArray()) {
            if (const auto word = wordFromJson(value.toObject())) {
                words.append(*word);
            }
        }
        line.words = words;
    }
    line.credit = object.value(QStringLiteral("credit")).toBool();
    return line;
}

} // namespace PlasmaLyrics

