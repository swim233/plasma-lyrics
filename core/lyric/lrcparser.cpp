#include "lrcparser.h"

#include "timeline.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>

namespace PlasmaLyrics {
namespace {

qint64 fractionToMs(const QStringView fraction)
{
    if (fraction.size() == 1) {
        return fraction.toInt() * 100;
    }
    if (fraction.size() == 2) {
        return fraction.toInt() * 10;
    }
    return fraction.left(3).toInt();
}

std::optional<LyricLine> parseJsonCredit(const QString &rawLine)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(rawLine.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const auto object = document.object();
    if (!object.contains(QStringLiteral("t")) || !object.value(QStringLiteral("c")).isArray()) {
        return std::nullopt;
    }
    QString text;
    for (const auto &part : object.value(QStringLiteral("c")).toArray()) {
        text += part.toObject().value(QStringLiteral("tx")).toString();
    }
    if (text.trimmed().isEmpty()) {
        return std::nullopt;
    }
    return LyricLine{object.value(QStringLiteral("t")).toInteger(), 0, text.trimmed(),
                     std::nullopt, std::nullopt, true};
}

} // namespace

ParsedLrc LrcParser::parse(QStringView source)
{
    static const QRegularExpression offsetExpression(
        QStringLiteral(R"(^\s*\[offset\s*:\s*([+-]?\d+)\]\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression timestampExpression(
        QStringLiteral(R"(\[(\d{1,3}):(\d{2})(?:[\.:](\d{1,3}))?\])"));
    static const QRegularExpression metadataExpression(
        QStringLiteral(R"(^\s*\[(ar|al|ti|by|re|ve|length)\s*:)") ,
        QRegularExpression::CaseInsensitiveOption);

    ParsedLrc result;
    const auto rawLines = source.toString().split(QLatin1Char('\n'));
    for (QString rawLine : rawLines) {
        if (rawLine.endsWith(QLatin1Char('\r'))) {
            rawLine.chop(1);
        }
        const auto offsetMatch = offsetExpression.match(rawLine);
        if (offsetMatch.hasMatch()) {
            result.embeddedOffsetMs = offsetMatch.capturedView(1).toInt();
            continue;
        }
        if (metadataExpression.match(rawLine).hasMatch()) {
            continue;
        }
        if (rawLine.trimmed().startsWith(QLatin1Char('{'))) {
            if (auto credit = parseJsonCredit(rawLine.trimmed())) {
                result.lines.append(std::move(*credit));
            }
            continue;
        }

        QList<qint64> starts;
        auto iterator = timestampExpression.globalMatch(rawLine);
        while (iterator.hasNext()) {
            const auto match = iterator.next();
            const qint64 minutes = match.capturedView(1).toInt();
            const qint64 seconds = match.capturedView(2).toInt();
            const qint64 fraction = fractionToMs(match.capturedView(3));
            starts.append((minutes * 60 + seconds) * 1000 + fraction);
        }
        if (starts.isEmpty()) {
            continue;
        }
        const QString text = QString(rawLine).remove(timestampExpression).trimmed();
        if (text.isEmpty()) {
            continue;
        }
        for (const qint64 start : starts) {
            result.lines.append({start, 0, text, std::nullopt, std::nullopt});
        }
    }

    std::stable_sort(result.lines.begin(), result.lines.end(), [](const auto &left, const auto &right) {
        return left.startMs < right.startMs;
    });
    if (result.embeddedOffsetMs != 0) {
        for (auto &line : result.lines) {
            line.startMs = std::max<qint64>(0, line.startMs + result.embeddedOffsetMs);
        }
    }
    finalizeEndTimes(result.lines);
    return result;
}

LyricDocument LrcParser::merge(QStringView origin, QStringView translation)
{
    auto parsedOrigin = parse(origin);
    const auto parsedTranslation = parse(translation);

    for (auto &line : parsedOrigin.lines) {
        const auto translated = std::find_if(parsedTranslation.lines.cbegin(), parsedTranslation.lines.cend(),
                                             [&line](const auto &candidate) {
                                                 return candidate.startMs == line.startMs;
                                             });
        if (translated != parsedTranslation.lines.cend() && translated->text != line.text) {
            line.translation = translated->text;
        }
    }
    // The embedded tag has already been applied to the normalized line times.
    // offsetMs is reserved for the user's persistent per-track adjustment.
    return {parsedOrigin.lines, 0, false};
}

} // namespace PlasmaLyrics
