#pragma once

#include "mpristypes.h"

#include <QList>

namespace PlasmaLyrics {

struct PolicyConfig {
    QStringList serviceBlacklist;
    QStringList musicUrlPrefixes;   // 保留原义：用户自定义的 URL 前缀
    bool useMetadataHeuristic = true;
    // 设置里勾选的来源平台，如 {"netease", "apple"}。与 useMetadataHeuristic 交换了
    // SPEC.md §3.1 给出的字段顺序——那份顺序把这个字段插在 musicUrlPrefixes 与
    // useMetadataHeuristic 之间，会让 tst_mprispolicy.cpp 里现有的三段位置初始化
    // `PolicyConfig{blacklist, prefixes, true}` 编译失败（第三个初始值 `true` 落到一个
    // QStringList 成员上）。挪到最后并给上与 Config::policy() 相同的默认值，
    // 现有测试不改一个字节也能编译、行为不变。
    QStringList enabledPlatforms{QStringLiteral("netease"), QStringLiteral("apple")};
};

class MprisPolicy
{
public:
    static QString fingerprint(const MprisState &state);
    static bool isMusic(const MprisState &state, const PolicyConfig &config);
    static bool isBlacklisted(const QString &service, const PolicyConfig &config);
    // 返回平台 id（"netease" / "apple"），无法判定时返回空字符串。
    static QString platformFor(const MprisState &state);
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

