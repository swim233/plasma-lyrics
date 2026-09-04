#pragma once

#include "mpristypes.h"

#include <QList>

namespace PlasmaLyrics {

struct PolicyConfig {
    QStringList serviceBlacklist;
    QStringList musicUrlPrefixes;
    bool useMetadataHeuristic = true;
};

class MprisPolicy
{
public:
    static QString fingerprint(const MprisState &state);
    static bool isMusic(const MprisState &state, const PolicyConfig &config);
    static bool isBlacklisted(const QString &service, const PolicyConfig &config);
    static QString choosePlayer(const QList<MprisState> &players,
                                const QString &currentService,
                                const PolicyConfig &config);
    static bool isPositionJump(qint64 previousPositionUs,
                               qint64 previousMonotonicNs,
                               qint64 positionUs,
                               qint64 monotonicNs,
                               double rate,
                               const QString &status);
};

} // namespace PlasmaLyrics

