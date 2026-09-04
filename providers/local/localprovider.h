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
    QList<Candidate> search(const TrackQuery &query) override;
    std::optional<LyricDocument> fetch(const QString &trackId) override;
    QString lastError() const override;
    std::optional<LyricDocument> overrideFor(const QString &providerId,
                                             const QString &trackId) override;

private:
    QString m_overrideDirectory;
};

} // namespace PlasmaLyrics
