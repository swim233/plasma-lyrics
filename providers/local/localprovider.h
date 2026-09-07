#pragma once

#include "providers/provider.h"

namespace PlasmaLyrics {

class LocalProvider final : public Provider
{
public:
    explicit LocalProvider(QString overrideDirectory = {});

    QString id() const override;
    bool isConfigured() const override;
    bool supportsSearch() const override;
    void search(const TrackQuery &query, SearchCallback callback) override;
    void fetch(const QString &trackId, FetchCallback callback) override;
    std::optional<LyricDocument> overrideFor(const QString &providerId,
                                             const QString &trackId) override;

private:
    QString m_overrideDirectory;
};

} // namespace PlasmaLyrics
