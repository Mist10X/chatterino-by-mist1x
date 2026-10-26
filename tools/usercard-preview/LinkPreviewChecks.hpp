// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

// Link-preview regression checks against a local stand-in for the link
// resolver (fictional answers only). Uses the real LinkResolver, LinkInfo,
// MessageBuilder and NetworkManager; nothing leaves 127.0.0.1.

#include "common/Env.hpp"
#include "common/LinkParser.hpp"
#include "common/network/NetworkManager.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "providers/links/LinkInfo.hpp"
#include "providers/links/LinkResolver.hpp"
#include "providers/twitch/ClipPreview.hpp"
#include "common/UiLanguage.hpp"
#include "singletons/Settings.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>

namespace linkpreview {

class FakeLinkResolverServer final : public QTcpServer
{
public:
    QHash<QString, int> requests;
    bool sawCredentials = false;

    FakeLinkResolverServer()
    {
        QObject::connect(this, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = this->nextPendingConnection())
            {
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                 [this, socket] {
                                     this->answer(socket);
                                 });
                QObject::connect(socket, &QTcpSocket::disconnected, socket,
                                 &QObject::deleteLater);
            }
        });
    }

private:
    void answer(QTcpSocket *socket)
    {
        const auto request = QString::fromUtf8(socket->readAll());
        const auto lower = request.toLower();
        if (lower.contains("authorization:") || lower.contains("oauth") ||
            lower.contains("client-id:"))
        {
            this->sawCredentials = true;
        }
        const auto path = request.section(' ', 1, 1);
        const auto target =
            QUrl::fromPercentEncoding(path.section("/r/", 1).toUtf8());
        this->requests[target] += 1;

        if (target.contains("slow.example"))
        {
            return;  // never answers: the client must time out by itself
        }
        QJsonObject body;
        auto tooltip = [](const QString &html) {
            return QString::fromUtf8(QUrl::toPercentEncoding(html));
        };
        if (target.contains("/clip/"))
        {
            body = {{"status", 200},
                    {"tooltip", tooltip("<b>Fictional clip title</b><br>"
                                        "<b>Channel:</b> fake_channel<br>"
                                        "<b>Duration:</b> 0:30")},
                    {"thumbnail", "https://clips-media.example.org/fake.jpg"}};
        }
        else if (target.contains("evilthumb"))
        {
            body = {{"status", 200},
                    {"tooltip", tooltip("<b>Page with a local thumbnail</b>")},
                    {"thumbnail", "http://127.0.0.1:1/private.png"}};
        }
        else if (target.contains("blocked.example"))
        {
            body = {{"status", 403}, {"message", "forbidden"}};
        }
        else
        {
            body = {{"status", 200},
                    {"tooltip", tooltip("<b>Fictional page title</b>")}};
        }
        const auto json = QJsonDocument(body).toJson(QJsonDocument::Compact);
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                      "Connection: close\r\nContent-Length: " +
                      QByteArray::number(json.size()) + "\r\n\r\n" + json);
        socket->disconnectFromHost();
    }
};

inline bool waitUntil(const std::function<bool()> &done, int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

/// Returns the number of failed checks; appends {name, ok} rows to `checks`.
inline int runLinkPreviewChecks(chatterino::Settings &settings,
                                QJsonArray &checks)
{
    using namespace chatterino;
    int failed = 0;
    auto check = [&](const QString &name, bool ok) {
        failed += ok ? 0 : 1;
        checks.append(QJsonObject{{"name", name}, {"ok", ok}});
    };

    FakeLinkResolverServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
    {
        check("fake resolver listens on 127.0.0.1", false);
        return failed;
    }
    // Env is read lazily, on the first link request below.
    qputenv("CHATTERINO2_LINK_RESOLVER_URL",
            QStringLiteral("http://127.0.0.1:%1/r/%2")
                .arg(server.serverPort())
                .arg(QStringLiteral("%1"))
                .toUtf8());
    check("resolver URL points at the local stand-in",
          Env::get().linkResolverUrl.startsWith(
              QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort())));
    NetworkManager::init();
    settings.linkInfoTooltip = true;
    settings.unshortLinks = false;
    settings.showLinkTitles = false;
    LinkResolver resolver;

    // 1. a message with a link does not ask anything by itself
    const auto clip =
        QStringLiteral("https://www.twitch.tv/fake_channel/clip/FakeClip-1");
    {
        MessageBuilder builder;
        const QString source = clip;
        builder.addLink(*linkparser::parse(source), source);
        waitUntil([] { return false; }, 300);
    }
    check("a message with a link sends no request on arrival",
          server.requests.value(clip) == 0);

    // 2. hover-time resolve: Twitch clip with title and thumbnail
    LinkInfo first(clip);
    resolver.resolve(&first);
    waitUntil([&] { return first.isLoaded(); }, 5000);
    check("clip preview resolves with title, channel and thumbnail",
          first.isResolved() &&
              first.tooltip().contains("Fictional clip title") &&
              first.tooltip().contains("Duration") && first.hasThumbnail());

    // 3. the same link elsewhere comes from the cache
    LinkInfo again(clip);
    resolver.resolve(&again);
    check("the same link again is answered from the cache",
          again.isResolved() && server.requests.value(clip) == 1);

    // 4. two hovers before an answer share one request
    const auto plain = QStringLiteral("https://plain.example.org/article");
    LinkInfo a(plain);
    LinkInfo b(plain);
    resolver.resolve(&a);
    resolver.resolve(&b);
    waitUntil([&] { return a.isLoaded() && b.isLoaded(); }, 5000);
    check("simultaneous hovers share one request",
          a.isResolved() && b.isResolved() &&
              server.requests.value(plain) == 1 &&
              a.tooltip().contains("Fictional page title") && !a.hasThumbnail());

    // 5. a thumbnail on a local address is not loaded
    LinkInfo evil(QStringLiteral("https://evilthumb.example.org/"));
    resolver.resolve(&evil);
    waitUntil([&] { return evil.isLoaded(); }, 5000);
    check("thumbnail on a local address is refused",
          evil.isResolved() && !evil.hasThumbnail());

    // 6. a site that refuses previews: simple card with site and address
    LinkInfo blocked(QStringLiteral("https://blocked.example.org/page"));
    resolver.resolve(&blocked);
    waitUntil([&] { return blocked.isLoaded(); }, 5000);
    check("site without preview gets a simple card with site and address",
          blocked.hasError() &&
              blocked.tooltip().contains("blocked.example.org") &&
              blocked.tooltip().contains("https://blocked.example.org/page"));

    // 7. a site that never answers times out
    LinkInfo slow(QStringLiteral("https://slow.example.org/"));
    QElapsedTimer timer;
    timer.start();
    resolver.resolve(&slow);
    waitUntil([&] { return slow.isLoaded(); }, 15000);
    check("an unanswered request ends with an error after the timeout",
          slow.hasError() && timer.elapsed() >= 7000 &&
              timer.elapsed() < 12000 &&
              slow.tooltip().contains("slow.example.org"));

    // 8. switched off: nothing is requested
    settings.linkInfoTooltip = false;
    const auto off = QStringLiteral("https://off.example.org/");
    LinkInfo disabled(off);
    resolver.resolve(&disabled);
    waitUntil([] { return false; }, 300);
    check("with previews off nothing is requested",
          disabled.state() == LinkInfo::State::Created &&
              server.requests.value(off) == 0);

    // 9. titles in chat (like Chatterino+): the link shows "title (domain)"
    settings.showLinkTitles = true;
    const auto titled =
        QStringLiteral("https://www.twitch.tv/fake_channel/clip/FakeClip-2");
    {
        MessageBuilder builder;
        const QString source = titled;
        builder.addLink(*linkparser::parse(source), source);
        auto message = builder.release();
        const LinkElement *link = nullptr;
        for (const auto &element : message->elements)
        {
            if (auto *candidate =
                    dynamic_cast<const LinkElement *>(element.get()))
            {
                link = candidate;
            }
        }
        waitUntil([&] { return link && !link->titleText().isEmpty(); }, 5000);
        check("with titles on, a link shows \"title (domain)\" in chat",
              link != nullptr && server.requests.value(titled) == 1 &&
                  link->titleText() ==
                      QStringLiteral("Fictional clip title (twitch.tv)"));
    }
    settings.showLinkTitles = false;

    // 10. clip cards (like Chatterino+): which links get one, and where
    check("clip links are recognised, other links are not",
          clippreview::clipSlug(QStringLiteral(
              "https://www.twitch.tv/morphe_ya/clip/"
              "PunchyStormyPenguinOhMyDog-oV2QpqBiymnVVR3r")) ==
                  QStringLiteral(
                      "PunchyStormyPenguinOhMyDog-oV2QpqBiymnVVR3r") &&
              clippreview::clipSlug(QStringLiteral(
                  "https://clips.twitch.tv/AbcDef-12")) ==
                  QStringLiteral("AbcDef-12") &&
              clippreview::clipSlug(
                  QStringLiteral("twitch.tv/x/clip/Some_Clip-1")) ==
                  QStringLiteral("Some_Clip-1") &&
              clippreview::clipSlug(QStringLiteral("https://www.twitch.tv/x"))
                  .isEmpty() &&
              clippreview::clipSlug(
                  QStringLiteral("https://clips.twitch.tv/create"))
                  .isEmpty() &&
              clippreview::clipSlug(
                  QStringLiteral("https://example.org/x/clip/Some_Clip-1"))
                  .isEmpty());
    {
        MessageBuilder builder;
        const QString text = clip;
        builder.emplace<TextElement>(QStringLiteral("look"),
                                     MessageElementFlag::Text);
        builder.addLink(*linkparser::parse(text), text);
        builder.addLink(*linkparser::parse(text), text);  // same clip twice
        builder.emplace<TextElement>(QStringLiteral("lol"),
                                     MessageElementFlag::Text);
        auto message = builder.release();
        int cards = 0;
        bool last = false;
        for (const auto &element : message->elements)
        {
            last = dynamic_cast<const ClipCardElement *>(element.get()) !=
                   nullptr;
            cards += last ? 1 : 0;
        }
        check("one card per clip, after the whole message text",
              cards == 1 && last);
    }
    const bool ru = activeUiLanguage().startsWith(QStringLiteral("ru"));
    check("card texts: duration, views, age",
          clippreview::formatDuration(6.4) == QStringLiteral("0:06") &&
              clippreview::formatDuration(3723) == QStringLiteral("1:02:03") &&
              clippreview::formatViews(1) == (ru ? QString::fromUtf8("1 просмотр") : QStringLiteral("1 view")) &&
              clippreview::formatViews(3) == (ru ? QString::fromUtf8("3 просмотра") : QStringLiteral("3 views")) &&
              clippreview::formatViews(25) == (ru ? QString::fromUtf8("25 просмотров") : QStringLiteral("25 views")) &&
              clippreview::formatViews(111) == (ru ? QString::fromUtf8("111 просмотров") : QStringLiteral("111 views")) &&
              clippreview::formatAge(
                  QDateTime::currentDateTimeUtc().addSecs(-10),
                  QDateTime::currentDateTimeUtc()) ==
                  (ru ? QString::fromUtf8("только что") : QStringLiteral("just now")) &&
              clippreview::formatAge(
                  QDateTime::currentDateTimeUtc().addSecs(-7200),
                  QDateTime::currentDateTimeUtc()) ==
                  (ru ? QString::fromUtf8("2 ч назад") : QStringLiteral("2 h ago")));
    check("without a signed-in account no clip request leaves the app",
          clippreview::get(QStringLiteral("FakeClip-1")) == nullptr);

    check("no token or client id is sent to the resolver",
          !server.sawCredentials);
    return failed;
}

}  // namespace linkpreview
