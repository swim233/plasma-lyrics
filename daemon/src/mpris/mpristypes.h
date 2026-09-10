#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>

namespace PlasmaLyrics {

struct MprisState {
    QString service;
    QString identity;
    qint64 kdePid = 0;
    QString title;
    QStringList artists;
    QString album;
    qint64 lengthUs = 0;
    QString trackId;
    QString url;
    QString mediaSrc;
    QString fingerprint;
    QString platform;   // 由 MprisPolicy::platformFor 填充，未知为空
    QString playbackStatus = QStringLiteral("Stopped");
    qint64 positionUs = 0;
    // This is the monotonic timestamp at which positionUs was sampled.
    // MprisPlayer must update the pair together even when a poll does not
    // represent a seek and therefore does not emit changed().
    qint64 anchorMonotonicNs = 0;
    double rate = 1.0;
    quint64 playingSerial = 0;
    bool music = false;
};

} // namespace PlasmaLyrics

Q_DECLARE_METATYPE(PlasmaLyrics::MprisState)
