#pragma once

#include "daemon/src/snapshot.h"
#include "providers/provider.h"

#include <QList>
#include <QObject>
#include <memory>

namespace PlasmaLyrics {

class LyricStore;

class Resolver : public QObject
{
    Q_OBJECT

public:
    Resolver(LyricStore &store, QList<Provider *> providers, bool filterCredits = true,
             QObject *parent = nullptr);
    void resolve(const MprisState &state);
    void cancel();
    static QStringList legacyWaylyricsIds(const MprisState &state);

Q_SIGNALS:
    void resolved(const QString &fingerprint, const PlasmaLyrics::ResolvedLyric &lyric);

private:
    struct Request;
    std::optional<LyricDocument> overridden(const TrackRef &ref) const;
    LyricDocument forDisplay(LyricDocument document, const TrackRef &ref) const;
    void continueWithProvider(const std::shared_ptr<Request> &request);
    void finish(const std::shared_ptr<Request> &request, ResolvedLyric lyric);

    LyricStore &m_store;
    QList<Provider *> m_providers;
    bool m_filterCredits;
    quint64 m_generation = 0;
};

} // namespace PlasmaLyrics
