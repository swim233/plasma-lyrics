#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <optional>

namespace PlasmaLyrics {

struct LyricWord {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    /// Word-level romanization, currently only from QQ Music's contentroma.
    /// It is stored whether or not anything renders it: the source carries it
    /// per mora and re-deriving it later would mean another fetch.
    std::optional<QString> romanization;

    bool operator==(const LyricWord &) const = default;
};

struct LyricLine {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    std::optional<QString> translation;
    /// The whole line's romanization. Where words carry their own, this is
    /// those joined in order, so the two can never disagree.
    std::optional<QString> romanization;
    std::optional<QList<LyricWord>> words;
    /// Set for lines the provider itself marked as production credits, which is
    /// stronger evidence than any shape heuristic. See DESIGN.md 6.1.
    bool credit = false;

    bool operator==(const LyricLine &) const = default;
};

using LyricLines = QList<LyricLine>;

struct LyricDocument {
    LyricLines lines;
    int offsetMs = 0;
    bool hasWords = false;
    // Provider provenance that must survive the SQLite and snapshot round
    // trips.  Values are deliberately JSON rather than provider-specific
    // members so core stays independent of every integration.
    QJsonObject metadata;
};

QJsonObject wordToJson(const LyricWord &word);
std::optional<LyricWord> wordFromJson(const QJsonObject &object);
QJsonObject lineToJson(const LyricLine &line);
std::optional<LyricLine> lineFromJson(const QJsonObject &object);

} // namespace PlasmaLyrics

Q_DECLARE_METATYPE(PlasmaLyrics::LyricLine)
Q_DECLARE_METATYPE(PlasmaLyrics::LyricLines)
