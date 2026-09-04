#include "snapshot.h"

#include "core/lyric/lyricmodel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <unistd.h>

namespace PlasmaLyrics {

SnapshotWriter::SnapshotWriter(QString path)
    : m_path(path.isEmpty() ? defaultPath() : std::move(path))
{
    restoreSequence();
}

QString SnapshotWriter::defaultPath()
{
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty()) {
        runtime = QDir::tempPath() + QStringLiteral("/runtime-") + QString::number(getuid());
    }
    return runtime + QStringLiteral("/plasma-lyricsd/state.json");
}

void SnapshotWriter::restoreSequence()
{
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly)) {
        m_sequence = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("seq")).toInteger();
    }
}

bool SnapshotWriter::write(const std::optional<MprisState> &player,
                           const ResolvedLyric &lyric,
                           QString *error)
{
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        if (error) {
            *error = QStringLiteral("cannot create snapshot directory");
        }
        return false;
    }
    QJsonObject root{{QStringLiteral("schema"), 1},
                     {QStringLiteral("seq"), static_cast<qint64>(++m_sequence)},
                     {QStringLiteral("daemon"), QJsonObject{{QStringLiteral("pid"), QCoreApplication::applicationPid()}}}};
    if (player) {
        root.insert(QStringLiteral("player"),
                    QJsonObject{{QStringLiteral("service"), player->service},
                                {QStringLiteral("identity"), player->identity},
                                {QStringLiteral("kdePid"), player->kdePid}});
        QJsonObject ref;
        if (lyric.ref) {
            ref = {{QStringLiteral("provider"), lyric.ref->provider},
                   {QStringLiteral("trackId"), lyric.ref->trackId}};
        }
        root.insert(QStringLiteral("track"),
                    QJsonObject{{QStringLiteral("fingerprint"), player->fingerprint},
                                {QStringLiteral("title"), player->title},
                                {QStringLiteral("artists"), QJsonArray::fromStringList(player->artists)},
                                {QStringLiteral("album"), player->album},
                                {QStringLiteral("lengthUs"), player->lengthUs},
                                {QStringLiteral("ref"), ref}});
        root.insert(QStringLiteral("playback"),
                    QJsonObject{{QStringLiteral("status"), player->playbackStatus},
                                {QStringLiteral("positionUs"), player->positionUs},
                                {QStringLiteral("anchorMonotonicNs"), player->anchorMonotonicNs},
                                {QStringLiteral("rate"), player->rate}});
    } else {
        root.insert(QStringLiteral("player"), QJsonValue::Null);
        root.insert(QStringLiteral("track"), QJsonValue::Null);
        root.insert(QStringLiteral("playback"),
                    QJsonObject{{QStringLiteral("status"), QStringLiteral("Stopped")},
                                {QStringLiteral("positionUs"), 0},
                                {QStringLiteral("anchorMonotonicNs"), 0},
                                {QStringLiteral("rate"), 1.0}});
    }
    QJsonArray lines;
    for (const auto &line : lyric.document.lines) {
        lines.append(lineToJson(line));
    }
    root.insert(QStringLiteral("lyric"),
                QJsonObject{{QStringLiteral("state"), lyric.state},
                            {QStringLiteral("offsetMs"), lyric.document.offsetMs},
                            {QStringLiteral("lines"), lines}});

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0
        || !file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

quint64 SnapshotWriter::sequence() const
{
    return m_sequence;
}

QString SnapshotWriter::path() const
{
    return m_path;
}

} // namespace PlasmaLyrics
