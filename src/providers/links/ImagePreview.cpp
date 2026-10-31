// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/links/ImagePreview.hpp"

#include "Application.hpp"
#include "common/Env.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "messages/Image.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>

namespace chatterino::imagepreview {

namespace {

constexpr int REQUEST_TIMEOUT_MS = 8000;
constexpr qsizetype MAX_CACHED = 300;

// Only used from the GUI thread.
QHash<QString, ImagePtr> &cache()
{
    static QHash<QString, ImagePtr> value;
    return value;
}

QSet<QString> &asked()
{
    static QSet<QString> value;
    return value;
}

QSet<QString> &deleted()
{
    static QSet<QString> value;
    return value;
}

void scheduleRelayout()
{
    // Several pictures arrive together: one relayout of the chats.
    static bool queued = false;
    if (queued || tryGetApp() == nullptr)
    {
        return;
    }
    queued = true;
    QTimer::singleShot(150, QCoreApplication::instance(), [] {
        queued = false;
        if (auto *app = tryGetApp(); app && app->getWindows())
        {
            app->getWindows()->forceLayoutChannelViews();
        }
    });
}

QUrl parse(const QString &url)
{
    return QUrl(url.contains(QStringLiteral("://"))
                    ? url
                    : QStringLiteral("https://") + url);
}

QString bareHost(const QUrl &url)
{
    auto host = url.host().toLower();
    if (host.startsWith(QStringLiteral("www.")))
    {
        host = host.mid(4);
    }
    return host;
}

bool isPublicHost(const QString &host)
{
    if (host.isEmpty() || !host.contains('.') ||
        host.endsWith(QStringLiteral(".local")) ||
        host.endsWith(QStringLiteral(".localhost")) ||
        host.endsWith(QStringLiteral(".lan")) ||
        host.endsWith(QStringLiteral(".internal")))
    {
        return false;
    }
    QHostAddress address;
    return !address.setAddress(host) ||
           !(address.isLoopback() || address.isPrivateUse() ||
             address.isLinkLocal() || address.isSiteLocal() ||
             address.isUniqueLocalUnicast());
}

}  // namespace

QStringList sites(const QString &setting)
{
    static const QRegularExpression separators(QStringLiteral("[\\s,;]+"));
    QStringList result;
    for (auto site : setting.split(separators, Qt::SkipEmptyParts))
    {
        // "https://www.kappa.lol/" -> "kappa.lol"
        site = bareHost(parse(site.trimmed()));
        if (!site.isEmpty() && !result.contains(site))
        {
            result.append(site);
        }
    }
    return result;
}

bool isImageLink(const QString &url, const QStringList &sites)
{
    const auto parsed = parse(url);
    const auto scheme = parsed.scheme().toLower();
    if (!parsed.isValid() ||
        (scheme != QStringLiteral("https") && scheme != QStringLiteral("http")) ||
        !parsed.userInfo().isEmpty())
    {
        return false;
    }
    const auto host = bareHost(parsed);
    if (!isPublicHost(host))
    {
        return false;
    }

    static const QRegularExpression imageFile(
        QStringLiteral("\\.(png|jpe?g|jfif|gif|webp|avif|bmp)$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto path = parsed.path();
    if (imageFile.match(path).hasMatch())
    {
        return true;
    }

    const auto parts = path.split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty())
    {
        return false;  // the site's main page
    }
    for (const auto &site : sites)
    {
        if (host == site || host.endsWith(u'.' + site))
        {
            // A post: kappa.lol/vCECFg, eblo.id/hH0WwpT, kappalol.fun/Y8zpTA
            // (or an album there, kappalol.fun/group/<id>). The sites' own
            // pages (eblo.id/videos, kappa.lol/uploaders) only have a logo.
            static const QRegularExpression postId(
                QStringLiteral("^[A-Za-z0-9_-]{4,16}$"));
            static const QRegularExpression notAWord(
                QStringLiteral("[A-Z0-9]"));
            const auto &id = parts.size() == 2 &&
                                     parts.first() == QStringLiteral("group")
                                 ? parts.last()
                                 : parts.first();
            return (parts.size() == 1 || &id == &parts.last()) &&
                   postId.match(id).hasMatch() &&
                   notAWord.match(id).hasMatch();
        }
    }
    return false;
}

bool isImageLink(const QString &url)
{
    return isImageLink(url, sites(getSettings()->imagePreviewSites));
}

QString key(const QString &url)
{
    const auto parsed = parse(url);
    auto path = parsed.path();
    while (path.endsWith('/'))
    {
        path.chop(1);
    }
    auto result = bareHost(parsed) + path;
    if (parsed.hasQuery())
    {
        result += u'?' + parsed.query();
    }
    return result;
}

ImagePtr get(const QString &url)
{
    const auto k = key(url);
    if (deleted().contains(k))
    {
        return nullptr;
    }
    return cache().value(k);
}

void request(const QString &url)
{
    const auto k = key(url);
    if (asked().contains(k) || deleted().contains(k) || tryGetApp() == nullptr)
    {
        return;
    }
    asked().insert(k);

    // Only the public link goes to the resolver; it hands back a link to a
    // small copy on its own server.
    NetworkRequest(Env::get().linkResolverUrl.arg(
                       QString::fromUtf8(QUrl::toPercentEncoding(url, {}, "/:"))))
        .timeout(REQUEST_TIMEOUT_MS)
        .onSuccess([k](const NetworkResult &result) {
            const auto root = result.parseJson();
            if (root.value("status").toInt() != 200)
            {
                return;
            }
            const QUrl thumbnail(root.value("thumbnail").toString());
            if (thumbnail.scheme() != QStringLiteral("https") ||
                !isPublicHost(thumbnail.host().toLower()))
            {
                return;
            }
            if (cache().size() >= MAX_CACHED)
            {
                cache().clear();  // rare; the pictures are simply loaded again
                asked().clear();
                asked().insert(k);
            }
            cache().insert(k, Image::fromUrl({thumbnail.toString()}));
            scheduleRelayout();
        })
        .execute();
}

void forget(const QString &url)
{
    const auto k = key(url);
    deleted().insert(k);
    cache().remove(k);
    scheduleRelayout();
}

void insertForTesting(const QString &url, ImagePtr thumbnail)
{
    const auto k = key(url);
    asked().insert(k);
    deleted().remove(k);
    cache().insert(k, std::move(thumbnail));
}

}  // namespace chatterino::imagepreview
