#include "core/lyric/lrcparser.h"
#include "core/lyric/timeline.h"
#include "core/store/lyricstore.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>

using namespace PlasmaLyrics;

namespace {

struct ImportStats {
    int imported = 0;
    int skipped = 0;
    int failed = 0;
};

struct CachedLines {
    LyricLines lines;
    bool supported = false;
};

QString firstString(const QJsonObject &object, const QStringList &keys)
{
    for (const auto &key : keys) {
        const auto value = object.value(key);
        if (value.isString() && !value.toString().isEmpty()) {
            return value.toString();
        }
        if (value.isDouble()) {
            return QString::number(value.toInteger());
        }
    }
    return {};
}

CachedLines parseCachedLines(const QJsonValue &value)
{
    if (value.isString()) {
        return {LrcParser::parse(value.toString()).lines, true};
    }
    if (!value.isObject()) {
        return {};
    }
    const auto object = value.toObject();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("None")) {
        return {{}, true};
    }
    if (type != QStringLiteral("LineTimestamp") || !object.value(QStringLiteral("content")).isArray()) {
        // NoTimestamp cannot be synchronized and is deliberately not imported.
        return {};
    }

    LyricLines lines;
    for (const auto &entry : object.value(QStringLiteral("content")).toArray()) {
        const auto line = entry.toObject();
        const auto start = line.value(QStringLiteral("start_time")).toObject();
        const qint64 seconds = start.value(QStringLiteral("secs")).toInteger();
        const qint64 nanos = start.value(QStringLiteral("nanos")).toInteger();
        const QString text = line.value(QStringLiteral("text")).toString().trimmed();
        if (!text.isEmpty()) {
            lines.append({seconds * 1000 + nanos / 1000000, 0, text, std::nullopt, std::nullopt});
        }
    }
    std::stable_sort(lines.begin(), lines.end(), [](const auto &left, const auto &right) {
        return left.startMs < right.startMs;
    });
    finalizeEndTimes(lines);
    return {lines, true};
}

std::optional<LyricDocument> parseCache(const QJsonObject &object)
{
    auto origin = parseCachedLines(object.value(QStringLiteral("olyric")));
    auto translation = parseCachedLines(object.value(QStringLiteral("tlyric")));
    if (!origin.supported && !translation.supported) {
        return std::nullopt;
    }
    if (origin.lines.isEmpty() && !translation.lines.isEmpty()) {
        origin.lines = translation.lines;
        translation.lines.clear();
    }
    for (auto &line : origin.lines) {
        const auto translated = std::find_if(translation.lines.cbegin(), translation.lines.cend(),
                                             [&line](const auto &candidate) {
                                                 return candidate.startMs == line.startMs;
                                             });
        if (translated != translation.lines.cend() && translated->text != line.text) {
            line.translation = translated->text;
        }
    }
    return LyricDocument{origin.lines, 0, false};
}

bool importCache(const QJsonObject &object, const QString &fallbackId, LyricStore &store, bool dryRun)
{
    const auto document = parseCache(object);
    if (!document) {
        return false;
    }
    QString trackId = firstString(object, {QStringLiteral("song_id"), QStringLiteral("songId"),
                                            QStringLiteral("track_id"), QStringLiteral("id")});
    if (trackId.isEmpty()) {
        trackId = fallbackId;
    }
    if (trackId.isEmpty()) {
        return false;
    }
    if (dryRun) {
        return true;
    }
    const TrackRef ref{QStringLiteral("waylyrics"), trackId, 1.0};
    return store.putLyric(ref, *document)
        && store.setOffset(ref, object.value(QStringLiteral("offset")).toInt());
}

void importValue(const QJsonValue &value, const QString &fallbackId,
                 LyricStore &store, bool dryRun, ImportStats &stats)
{
    if (value.isObject()) {
        const auto object = value.toObject();
        if (object.contains(QStringLiteral("olyric")) || object.contains(QStringLiteral("tlyric"))) {
            if (importCache(object, fallbackId, store, dryRun)) {
                ++stats.imported;
            } else {
                ++stats.skipped;
            }
            return;
        }
        for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
            importValue(iterator.value(), iterator.key(), store, dryRun, stats);
        }
    } else if (value.isArray()) {
        for (const auto &entry : value.toArray()) {
            importValue(entry, fallbackId, store, dryRun, stats);
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("import-waylyrics"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Import waylyrics JSON cache into plasma-lyrics"));
    parser.addHelpOption();
    parser.addOption({{QStringLiteral("s"), QStringLiteral("source")}, QStringLiteral("waylyrics cache file or directory"),
                      QStringLiteral("PATH"), QString(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                                                       + QStringLiteral("/waylyrics"))});
    parser.addOption({QStringLiteral("database"), QStringLiteral("destination SQLite database"),
                      QStringLiteral("PATH"), LyricStore::defaultPath()});
    parser.addOption({QStringLiteral("dry-run"), QStringLiteral("parse without changing the plasma-lyrics database")});
    parser.process(application);

    const bool dryRun = parser.isSet(QStringLiteral("dry-run"));
    LyricStore store(dryRun ? QStringLiteral(":memory:") : parser.value(QStringLiteral("database")));
    QString error;
    if (!store.open(&error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 2;
    }
    const QString source = parser.value(QStringLiteral("source"));
    QStringList files;
    if (QFileInfo(source).isDir()) {
        QDirIterator iterator(source, {QStringLiteral("*.json")}, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            files.append(iterator.next());
        }
    } else {
        files.append(source);
    }

    ImportStats stats;
    for (const auto &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            ++stats.failed;
            continue;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            ++stats.failed;
            continue;
        }
        const QString fallbackId = QFileInfo(path).completeBaseName();
        importValue(document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object()),
                    fallbackId, store, dryRun, stats);
    }
    QTextStream(stdout) << "imported=" << stats.imported << " skipped=" << stats.skipped
                        << " failed=" << stats.failed << Qt::endl;
    return stats.failed == 0 ? 0 : 1;
}
