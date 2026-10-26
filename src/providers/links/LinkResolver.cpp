// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/UiLanguage.hpp"
#include "providers/links/LinkResolver.hpp"

#include "common/Env.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "providers/links/LinkInfo.hpp"
#include "singletons/Settings.hpp"

#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QPointer>
#include <QStringBuilder>
#include <QUrl>

#include <utility>

namespace chatterino {

namespace {

using State = LinkInfo::State;

/// One answer of the link resolver, shared by every message with that URL.
struct CachedLink {
    bool ok = false;
    QString tooltip;
    QString thumbnail;
    QString link;
    qint64 at = 0;
};

constexpr qsizetype MAX_CACHED = 300;
constexpr qint64 KEEP_RESOLVED_MS = 30LL * 60 * 1000;
constexpr qint64 KEEP_FAILED_MS = 3LL * 60 * 1000;
constexpr int REQUEST_TIMEOUT_MS = 8000;

// The resolver is only used from the GUI thread.
QHash<QString, CachedLink> &cache()
{
    static QHash<QString, CachedLink> value;
    return value;
}

QList<QString> &cacheOrder()
{
    static QList<QString> value;
    return value;
}

QHash<QString, QList<QPointer<LinkInfo>>> &waiting()
{
    static QHash<QString, QList<QPointer<LinkInfo>>> value;
    return value;
}

void remember(const QString &url, CachedLink entry)
{
    entry.at = QDateTime::currentMSecsSinceEpoch();
    auto &order = cacheOrder();
    order.removeAll(url);
    order.append(url);
    cache().insert(url, std::move(entry));
    while (order.size() > MAX_CACHED)
    {
        cache().remove(order.takeFirst());
    }
}

const CachedLink *fresh(const QString &url)
{
    auto it = cache().constFind(url);
    if (it == cache().cend())
    {
        return nullptr;
    }
    const auto age = QDateTime::currentMSecsSinceEpoch() - it->at;
    if (age > (it->ok ? KEEP_RESOLVED_MS : KEEP_FAILED_MS))
    {
        return nullptr;
    }
    return &*it;
}

/// Thumbnails are loaded by this client: only public web addresses, never
/// localhost / LAN / link-local / file and other local resources.
bool isPublicImageUrl(const QString &text)
{
    const QUrl url(text);
    const auto scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != "https" && scheme != "http") ||
        !url.userInfo().isEmpty())
    {
        return false;
    }
    const auto host = url.host().toLower();
    if (host.isEmpty() || host == "localhost" || host.endsWith(".localhost") ||
        host.endsWith(".local") || host.endsWith(".internal") ||
        host.endsWith(".lan") || !host.contains('.'))
    {
        return false;
    }
    QHostAddress address;
    if (address.setAddress(host))
    {
        if (address.isLoopback() || address.isLinkLocal() ||
            address.isSiteLocal() || address.isUniqueLocalUnicast() ||
            address.isMulticast() || address.isNull() ||
            address == QHostAddress::AnyIPv4 || address == QHostAddress::AnyIPv6 ||
            address.isPrivateUse())
        {
            return false;
        }
    }
    return true;
}

/// Simple card for when there is nothing better: site, address, a note.
QString fallbackTooltip(const QString &url, const QString &note)
{
    const QUrl parsed(url);
    auto host = parsed.host();
    if (host.startsWith(QStringLiteral("www.")))
    {
        host = host.mid(4);
    }
    auto shown = url;
    if (shown.size() > 160)
    {
        shown = shown.left(157) + QStringLiteral("…");
    }
    QString html = QStringLiteral("<div style=\"text-align: left;\">");
    if (!host.isEmpty())
    {
        html += u"<b>" % host.toHtmlEscaped() % u"</b><br>";
    }
    html += shown.toHtmlEscaped();
    if (!note.isEmpty())
    {
        html += u"<br><span style=\"color: #9a96a6;\">" % note.toHtmlEscaped() %
                u"</span>";
    }
    return html + QStringLiteral("</div>");
}

void apply(LinkInfo *info, const CachedLink &entry)
{
    if (entry.ok)
    {
        if (!entry.thumbnail.isEmpty() && isPublicImageUrl(entry.thumbnail))
        {
            info->setThumbnail(Image::fromUrl({entry.thumbnail}));
        }
        if (getSettings()->unshortLinks && !entry.link.isEmpty())
        {
            info->setResolvedUrl(entry.link);
        }
        info->setTooltip(entry.tooltip);
        info->setState(State::Resolved);
        return;
    }
    info->setTooltip(entry.tooltip);
    info->setState(State::Errored);
}

void finish(const QString &url, const CachedLink &entry)
{
    remember(url, entry);
    const auto infos = waiting().take(url);
    for (const auto &info : infos)
    {
        if (info && info->isLoading())
        {
            apply(info.data(), entry);
        }
    }
}

}  // namespace

void LinkResolver::resolve(LinkInfo *info)
{
    assert(info);

    if (info->state() != State::Created)
    {
        return;
    }

    if (!getSettings()->linkInfoTooltip && !getSettings()->showLinkTitles)
    {
        return;
    }

    const auto url = info->originalUrl();

    // Same link seen before (any message): no new request.
    if (const auto *hit = fresh(url))
    {
        info->setState(State::Loading);
        apply(info, *hit);
        return;
    }

    info->setTooltip(fallbackTooltip(url, uiText("Loading...")));
    info->setState(State::Loading);

    auto &queue = waiting()[url];
    queue.append(info);
    if (queue.size() > 1)
    {
        return;  // a request for this URL is already running
    }

    // Only the public link is sent to the resolver, never an account token.
    NetworkRequest(Env::get().linkResolverUrl.arg(
                       QString::fromUtf8(QUrl::toPercentEncoding(url, {}, "/:"))))
        .timeout(REQUEST_TIMEOUT_MS)
        .onSuccess([url](const NetworkResult &result) {
            const auto root = result.parseJson();
            CachedLink entry;
            if (root["status"].toInt() == 200)
            {
                entry.ok = true;
                entry.tooltip = QUrl::fromPercentEncoding(
                    root["tooltip"].toString().toUtf8());
                if (entry.tooltip.trimmed().isEmpty())
                {
                    entry.tooltip = fallbackTooltip(url, {});
                }
                entry.thumbnail = root["thumbnail"].toString();
                entry.link = root["link"].toString();
            }
            else
            {
                entry.tooltip = fallbackTooltip(
                    url, uiText("This site does not provide a preview"));
            }
            finish(url, entry);
        })
        .onError([url](const NetworkResult &result) {
            CachedLink entry;
            entry.tooltip = fallbackTooltip(
                url, uiText(u"Preview unavailable (") % result.formatError() %
                         u')');
            finish(url, entry);
        })
        .execute();
}

}  // namespace chatterino
