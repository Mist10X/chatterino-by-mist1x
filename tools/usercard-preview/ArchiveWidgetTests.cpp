// Real ArchiveUserCard rendering with an explicitly synthetic pipe. No app
// initialization, accounts, databases, HTTP or real dataset are involved.
#include "Application.hpp"
#include "messages/Image.hpp"
#include "providers/twitch/UsercardBadges.hpp"
#include "widgets/helper/ArchiveUserCard.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QScrollBar>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <cstdio>
#include <functional>
#include <memory>

using namespace chatterino;
namespace {
int passed = 0;
int failed = 0;
struct GuiShutdownGuard {
    ~GuiShutdownGuard()
    {
        // This standalone test returns from main without Application::exec().
        // Match normal shutdown before QApplication and static images die.
        markAppAboutToQuit();
    }
};
void check(bool ok, const char *label)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    ok ? ++passed : ++failed;
}
bool until(const std::function<bool()> &predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 1500)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return predicate();
}
QJsonObject block(QJsonArray rows = {}, QString state = "found")
{
    return {{"status", state},
            {"total", rows.size()},
            {"rows", rows},
            {"more", false}};
}
QJsonObject card(const QString &uid)
{
    const QJsonObject message{
        {"user", "archive_demo"},
        {"user_id", uid},
        {"channel", "archive_room"},
        {"id", "msg-1"},
        {"at_ms", 1792497600000.0},
        {"text", "only-" + uid +
                     " <img src='https://invalid.example/secret'> & русский"},
        {"source", "synthetic"},
        {"automod", true}};
    auto event = message;
    event.insert("kind", "timeout");
    event.insert("duration", 600);
    event.insert("context", QJsonArray{message});
    event.insert("receipt", QJsonObject{{"state", "found"},
                                        {"checked_at_ms", 1792497600000.0}});
    return {{"protocol", "archive-card"},
            {"version", 2},
            {"dataset_id", "synthetic-v1"},
            {"identity", QJsonObject{{"user_id", uid}}},
            {"generated_at_ms", 1792497600000.0},
            {"state", "found"},
            {"truncated", false},
            {"messages", block({message})},
            {"moderation", block({event})},
            {"dossier", block({QJsonObject{{"channel", "archive_room"},
                                           {"at_ms", 1792497600000.0},
                                           {"fetched_at_ms", 1792497601000.0},
                                           {"context", QJsonArray{message}}}})},
            {"coverage", QJsonObject{{"status", "unknown"}}}};
}
struct Server {
    QLocalServer server;
    QString name =
        "LeafyrinoArchive-test-" + QUuid::createUuid().toString(QUuid::Id128);
    std::function<QJsonObject(QJsonObject, QJsonObject)> transform;
    int count = 0;
    int delay = 0;
    Server()
    {
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server.listen(name))
            std::abort();
        QObject::connect(
            &server, &QLocalServer::newConnection, &server, [this] {
                while (server.hasPendingConnections())
                {
                    auto *socket = server.nextPendingConnection();
                    auto buffer = std::make_shared<QByteArray>();
                    QObject::connect(
                        socket, &QLocalSocket::readyRead, socket,
                        [this, socket, buffer] {
                            buffer->append(socket->readAll());
                            while (buffer->contains('\n'))
                            {
                                const auto end = buffer->indexOf('\n');
                                const auto request =
                                    QJsonDocument::fromJson(buffer->left(end))
                                        .object();
                                buffer->remove(0, end + 1);
                                ++count;
                                auto data =
                                    card(request.value("user_id").toString());
                                const bool hello =
                                    request.value("op") == "hello";
                                if (!hello && transform)
                                    data = transform(request, data);
                                const QJsonObject reply{
                                    {"id", request.value("id")},
                                    {"ok", true},
                                    {"data", data}};
                                QTimer::singleShot(
                                    hello ? 0 : delay, socket, [socket, reply] {
                                        socket->write(
                                            QJsonDocument(reply).toJson(
                                                QJsonDocument::Compact) +
                                            '\n');
                                    });
                            }
                        });
                    QObject::connect(socket, &QLocalSocket::disconnected,
                                     socket, &QObject::deleteLater);
                }
            });
    }
    ArchiveEndpoint endpoint(int timeout = 1000) const
    {
        return {name, "synthetic-v1", timeout};
    }
};
}  // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    const GuiShutdownGuard shutdown;
    {
        QVector<GqlBadge> earned;
        for (int i = 0; i < 8; ++i)
        {
            earned.push_back(
                {.id = QString::number(i),
                 .setID = QStringLiteral("earned-%1").arg(i),
                 .version = "1",
                 .title = QStringLiteral("Badge %1").arg(i),
                 .image1x =
                     QStringLiteral("https://badges.invalid/%1/1").arg(i),
                 .image2x =
                     QStringLiteral("https://badges.invalid/%1/2").arg(i),
                 .image4x =
                     QStringLiteral("https://badges.invalid/%1/4").arg(i)});
        }
        check(
            usercardBadgeEmotes(earned).size() == 8,
            "earned collection includes eight badges, not only the worn badge");
        earned.push_back(earned.front());
        check(usercardBadgeEmotes(earned).size() == 8,
              "earned badge set/version duplicates are removed");
        earned.push_back({.id = "not-equal",
                          .setID = "earned-0",
                          .version = "2",
                          .title = "different earned tier",
                          .image4x = "https://badges.invalid/tier2"});
        check(usercardBadgeEmotes(earned).size() == 9,
              "different earned tiers remain visible");
        auto onlyLarge =
            usercardBadgeEmotes({{.id = "large",
                                  .title = "<b>literal</b>",
                                  .description = "x & y",
                                  .image4x = "https://badges.invalid/large"}});
        check(onlyLarge.size() == 1 && onlyLarge[0]->images.getImage1() &&
                  onlyLarge[0]->images.getImage1() ==
                      onlyLarge[0]->images.getImage3(),
              "single available badge image is usable at every scale");
        check(onlyLarge.size() == 1 &&
                  onlyLarge[0]->tooltip.string.contains("&lt;b&gt;") &&
                  onlyLarge[0]->tooltip.string.contains("x &amp; y"),
              "earned badge tooltip escapes API markup");
        check(usercardBadgeEmotes({{.id = "missing", .title = "Missing"}})
                  .empty(),
              "unavailable badge image does not produce an invisible "
              "placeholder");
        check(usercardBadgeEmotes(
                  {{.id = "unsafe1", .image1x = "file:///C:/not-a-badge"},
                   {.id = "unsafe2", .image2x = "http://badges.invalid/plain"},
                   {.id = "unsafe3",
                    .image4x = "https://user:password@badges.invalid/a"}})
                  .empty(),
              "badge images reject local files, plaintext transport and "
              "embedded credentials");
    }
    {
        Server server;
        server.delay = 30;
        ArchiveUserCard widget(server.endpoint());
        widget.resize(330, 500);
        widget.setTarget("101", "archive_demo", "archive_room");
        check(widget.property("archiveState") == "loading",
              "loading distinct from empty");
        check(until([&] {
                  return widget.property("archiveState") == "found";
              }),
              "real widget renders successful card");
        check(widget.plainText().contains("only-101") &&
                  widget.plainText().contains("русский"),
              "messages and Cyrillic");
        check(widget.plainText().contains("<img src=") &&
                  !widget.findChild<QTextBrowser *>()->toHtml().contains(
                      "<img src=\"https"),
              "saved HTML is literal text");
        check(widget.plainText().contains("МУТ") &&
                  widget.plainText().contains("10 мин"),
              "timeout and duration");
        check(widget.plainText().contains("AutoMod") &&
                  widget.plainText().contains("проверка"),
              "saved receipt is historical");
        check(widget.plainText().contains("Получено досье") &&
                  widget.plainText().contains("20.10.2026"),
              "dossier retrieval timestamp");
        check(widget.plainText().contains("неизвестно"),
              "unknown coverage not treated as complete");
        check(widget.findChild<QTextBrowser *>()->horizontalScrollBarPolicy() ==
                  Qt::ScrollBarAlwaysOff,
              "narrow content no horizontal scrollbar");
        widget.setTarget("102", "archive_demo", "archive_room");
        widget.setTarget("101", "archive_demo", "archive_room");
        check(until([&] {
                  return widget.property("archiveState") == "found";
              }) &&
                  widget.plainText().contains("only-101") &&
                  !widget.plainText().contains("only-102"),
              "same login A B A not mixed");
        widget.setIdentityUnavailable();
        check(widget.property("archiveState") == "found" &&
                  widget.plainText().contains("only-101"),
              "failed public profile lookup preserves known UID local data");
        widget.setTarget("", "archive_demo", "archive_room");
        const auto count = server.count;
        until([&] {
            return false;
        });
        check(server.count == count &&
                  widget.property("archiveState") == "loading" &&
                  widget.plainText().isEmpty(),
              "missing UID clears old data and sends no request");
        widget.setIdentityUnavailable();
        check(widget.property("archiveState") == "identity_unavailable" &&
                  widget.plainText().contains("не запрашивалась") &&
                  !widget.plainText().contains("only-101"),
              "final missing UID lookup failure is not perpetual loading");
    }
    {
        Server server;
        server.transform = [](auto, auto data) {
            data.insert("state", "empty");
            for (auto key : {"messages", "moderation", "dossier"})
                data.insert(key, block({}, "empty"));
            return data;
        };
        ArchiveUserCard widget(server.endpoint());
        widget.setTarget("103", "archive_demo", "archive_room");
        check(until([&] {
                  return widget.property("archiveState") == "empty";
              }) &&
                  widget.statusText().contains("нет"),
              "empty dataset is separate state");
        server.transform = [](auto, auto data) {
            data.insert("coverage", QJsonObject{{"status", "not_covered"}});
            return data;
        };
        widget.setTarget("104", "archive_demo", "archive_room");
        check(until([&] {
                  return widget.property("archiveState") == "not_covered";
              }) &&
                  widget.statusText().contains("не покрыт"),
              "known collection gap is separate state");
        server.transform = [](auto, auto data) {
            data.insert("state", "partial");
            data.insert("dossier", block({}, "unsupported_uid"));
            return data;
        };
        widget.setTarget("105", "archive_demo", "archive_room");
        check(until([&] {
                  return widget.property("archiveState") == "partial";
              }) &&
                  widget.plainText().contains("не подготовлен"),
              "unsupported UID not falsely empty");
        server.transform = [](auto, auto data) {
            auto messages = data.value("messages").toObject();
            auto row = messages.value("rows").toArray().at(0).toObject();
            row.insert("text", QString(20000, QChar(0x044F)));
            messages.insert("rows", QJsonArray{row});
            messages.insert("more", true);
            data.insert("messages", messages);
            return data;
        };
        widget.setTarget("106", "archive_demo", "archive_room");
        check(until([&] {
                  return widget.property("archiveState") == "found";
              }) &&
                  widget.plainText().size() < 14000 &&
                  widget.plainText().contains("Показана часть"),
              "long text bounded with continuation");
        server.transform = [](auto, auto data) {
            data.insert("identity", QJsonObject{{"user_id", "999"}});
            return data;
        };
        widget.setTarget("107", "archive_demo", "archive_room");
        check(until([&] {
                  return widget.property("archiveState") == "error";
              }) &&
                  !widget.plainText().contains("only-999"),
              "foreign UID cannot reach display");
    }
    {
        ArchiveUserCard widget(ArchiveEndpoint{});
        widget.setTarget("101", "archive_demo", "archive_room");
        check(widget.property("archiveState") == "unavailable",
              "unconfigured bridge never auto discovers");
    }
    {
        Server server;
        server.delay = 400;
        auto *widget = new ArchiveUserCard(server.endpoint());
        widget->setTarget("101", "archive_demo", "archive_room");
        until([&] {
            return server.count >= 2;
        });
        QElapsedTimer timer;
        timer.start();
        delete widget;
        check(timer.elapsed() < 100,
              "destroy card while request active is bounded");
    }
    std::printf("RESULT %d passed %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
