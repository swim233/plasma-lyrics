// SPDX-License-Identifier: GPL-2.0-or-later

#include "netease/NeteaseApi.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace Lyrics
{

NeteaseApi::NeteaseApi(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

NeteaseApi::~NeteaseApi() = default;

void NeteaseApi::search(const QString &keyword, int limit)
{
    QUrl url(QStringLiteral("https://music.163.com/api/search/get"));
    QUrlQuery query;
    query.addQueryItem(QLatin1String("s"), keyword);
    query.addQueryItem(QLatin1String("type"), QLatin1String("1"));
    query.addQueryItem(QLatin1String("limit"), QString::number(limit));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader(QByteArrayLiteral("User-Agent"),
                     QByteArrayLiteral("Mozilla/5.0 (X11; Linux x86_64)"));

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        SearchReply r;
        r.ok = reply->error() == QNetworkReply::NoError;
        r.errorText = reply->errorString();
        r.body = reply->readAll();
        Q_EMIT finished(r);
    });
}

} // namespace Lyrics
