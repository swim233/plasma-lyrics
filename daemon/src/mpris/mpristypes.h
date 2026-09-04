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
    QString playbackStatus = QStringLiteral("Stopped");
    qint64 positionUs = 0;
    qint64 anchorMonotonicNs = 0;
    double rate = 1.0;
    quint64 playingSerial = 0;
    bool music = false;
};

} // namespace PlasmaLyrics

Q_DECLARE_METATYPE(PlasmaLyrics::MprisState)
