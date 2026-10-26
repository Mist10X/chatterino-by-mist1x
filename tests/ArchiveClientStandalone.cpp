// Standalone local protocol regression test: Qt Core/Network only, no application,
// accounts, HTTP, Twitch, plugins or production data directories.
#include "providers/archive/ArchiveClient.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <cstdio>
#include <functional>
#include <memory>

using namespace chatterino;
namespace {
int passed = 0;
int failed = 0;
bool until(const std::function<bool()> &done, int ms = 1000)
{
    QElapsedTimer time;
    time.start();
    while (!done() && time.elapsed() < ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return done();
}
void check(bool value, const char *label)
{
    std::printf("%s %s\n", value ? "PASS" : "FAIL", label);
    value ? ++passed : ++failed;
}
QJsonObject base()
{
    return {{"protocol", "archive-card"},
            {"version", 2},
            {"dataset_id", "synthetic-v1"}};
}
struct Peer {
    QLocalServer server;
    QString name =
        "LeafyrinoArchive-test-" + QUuid::createUuid().toString(QUuid::Id128);
    int received = 0;
    int operations = 0;
    std::function<void(QLocalSocket *, const QJsonObject &)> behavior;
    Peer()
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
                        socket, &QLocalSocket::readyRead, &server,
                        [this, socket, buffer] {
                            buffer->append(socket->readAll());
                            while (buffer->contains('\n'))
                            {
                                const auto end = buffer->indexOf('\n');
                                const auto r =
                                    QJsonDocument::fromJson(buffer->left(end))
                                        .object();
                                buffer->remove(0, end + 1);
                                ++received;
                                if (r.value("op") != "hello")
                                    ++operations;
                                if (behavior)
                                    behavior(socket, r);
                                else
                                    respond(socket, r);
                            }
                        });
                    QObject::connect(socket, &QLocalSocket::disconnected,
                                     socket, &QObject::deleteLater);
                }
            });
    }
    static void write(QLocalSocket *socket, const QJsonObject &request,
                      QJsonObject data)
    {
        socket->write(QJsonDocument(QJsonObject{{"id", request.value("id")},
                                                {"ok", true},
                                                {"data", data}})
                          .toJson(QJsonDocument::Compact) +
                      '\n');
    }
    static void respond(QLocalSocket *socket, const QJsonObject &request)
    {
        auto data = base();
        if (request.value("op") != "hello")
        {
            data.insert("identity",
                        QJsonObject{{"user_id", request.value("user_id")}});
            if (request.value("op") == "card")
            {
                data.insert("state", "empty");
                data.insert("generated_at_ms", 0);
                data.insert("truncated", false);
                data.insert("coverage", QJsonObject{{"status", "unknown"}});
                for (auto key : {"messages", "moderation", "dossier"})
                    data.insert(key, QJsonObject{{"status", "empty"},
                                                 {"total", 0},
                                                 {"rows", QJsonArray{}},
                                                 {"more", false}});
            }
            if (request.value("op") == "open")
            {
                data.insert("view", request.value("view"));
                data.insert("opened", true);
            }
            if (request.value("op") == "history")
            {
                data.insert("channel", request.value("channel"));
                data.insert("scope", "channel");
                data.insert("source", request.value("source") == "public" ? "public_logs" : "local_archive");
                data.insert("status", "empty");
                data.insert("total", QJsonValue::Null);
                data.insert("rows", QJsonArray{});
                data.insert("more", false);
                data.insert("next_cursor", QJsonValue::Null);
                data.insert("truncated", false);
            }
        }
        write(socket, request, data);
    }
    ArchiveEndpoint endpoint(int timeout = 150) const
    {
        return {name, "synthetic-v1", timeout};
    }
};
struct Results {
    int cards = 0;
    int opens = 0;
    int errors = 0;
    int histories = 0;
    int loading = 0;
    QJsonObject history;
    QString code;
    QString uid;
    void bind(ArchiveClient &client)
    {
        QObject::connect(&client, &ArchiveClient::historyReady, &client, [this](auto data) {
            ++histories;
            history = data;
        });
        QObject::connect(&client, &ArchiveClient::historyLoading, &client, [this] { ++loading; });
        QObject::connect(&client, &ArchiveClient::cardReady, &client,
                         [this](auto data) {
                             ++cards;
                             uid = data.value("identity")
                                       .toObject()
                                       .value("user_id")
                                       .toString();
                         });
        QObject::connect(&client, &ArchiveClient::viewOpened, &client, [this] {
            ++opens;
        });
        QObject::connect(&client, &ArchiveClient::failed, &client,
                         [this](auto value, auto) {
                             ++errors;
                             code = value;
                         });
    }
};
void responseError(
    const char *label, const QString &expected,
    const std::function<void(QLocalSocket *, const QJsonObject &)> &mutate,
    bool handshake = false)
{
    Peer peer;
    peer.behavior = [&](auto *socket, auto r) {
        if ((r.value("op") == "hello") == handshake)
            mutate(socket, r);
        else
            Peer::respond(socket, r);
    };
    ArchiveClient client(peer.endpoint());
    Results result;
    result.bind(client);
    client.requestCard("101", "viewer_a", "channel_a");
    check(until([&] {
              return result.errors != 0;
          }) &&
              result.code == expected && result.cards == 0,
          label);
}
}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    {
        QTemporaryDir directory;
        check(directory.isValid(),
              "sidecar fixture uses a temporary directory");
        if (!directory.isValid())
            return 1;
        const auto path = directory.filePath("archive-bridge.json");
        auto config =
            QJsonObject{{"protocol", "archive-card"},
                        {"version", ArchiveClient::ProtocolVersion},
                        {"socket_name", "LeafyrinoArchive-test-sidecar"},
                        {"dataset_id", "synthetic-sidecar-v1"}};
        auto write = [&](const QByteArray &bytes) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                file.write(bytes) != bytes.size())
                std::abort();
            file.close();
            return readArchiveEndpointFile(path);
        };
        auto writeObject = [&](const QJsonObject &object) {
            return write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        };
        auto disabled = [](const ArchiveEndpoint &endpoint) {
            return endpoint.socketName.isEmpty() &&
                   endpoint.datasetId.isEmpty();
        };
        const auto endpoint = writeObject(config);
        ArchiveClient configured(endpoint);
        check(
            configured.configured() &&
                endpoint.socketName ==
                    QStringLiteral("LeafyrinoArchive-test-sidecar") &&
                endpoint.datasetId == QStringLiteral("synthetic-sidecar-v1") &&
                endpoint.timeoutMs == 1800,
            "explicit valid sidecar enables only its declared endpoint");
        check(disabled(
                  readArchiveEndpointFile(directory.filePath("missing.json"))),
              "missing sidecar stays disabled");
        check(disabled(readArchiveEndpointFile(directory.path())),
              "sidecar must be a regular file");
        auto invalid = config;
        invalid.insert("protocol", "other-protocol");
        check(disabled(writeObject(invalid)),
              "sidecar wrong protocol disabled");
        invalid = config;
        invalid.insert("version", 1);
        check(disabled(writeObject(invalid)), "sidecar wrong version disabled");
        invalid.insert("version", QStringLiteral("2"));
        check(disabled(writeObject(invalid)),
              "sidecar string version disabled");
        invalid.insert("version", 2.5);
        check(disabled(writeObject(invalid)),
              "sidecar fractional version disabled");
        invalid = config;
        invalid.insert("dataset_id", "../wrong");
        check(disabled(writeObject(invalid)),
              "sidecar invalid dataset disabled");
        invalid.insert("dataset_id", true);
        check(disabled(writeObject(invalid)),
              "sidecar non-string dataset disabled");
        invalid = config;
        invalid.insert("socket_name", "../wrong");
        check(disabled(writeObject(invalid)),
              "sidecar invalid socket disabled");
        invalid.insert("socket_name", "LeafyrinoArchive-test-sidecar\n");
        check(disabled(writeObject(invalid)),
              "sidecar socket newline disabled");
        invalid.insert("socket_name", QStringLiteral(""));
        check(disabled(writeObject(invalid)),
              "sidecar empty endpoint disabled");
        check(disabled(write(QByteArray(4097, ' '))),
              "sidecar over 4096 bytes disabled");
        check(disabled(write("{broken}")), "sidecar malformed JSON disabled");
        check(disabled(write("[]")), "sidecar non-object JSON disabled");
        auto boundary = QJsonDocument(config).toJson(QJsonDocument::Compact);
        boundary.append(QByteArray(4096 - boundary.size(), ' '));
        check(!disabled(write(boundary)),
              "sidecar accepts valid 4096-byte boundary");
    }
    {
        Peer peer;
        ArchiveClient client(peer.endpoint());
        Results result;
        result.bind(client);
        for (int i = 0; i < 12; ++i)
        {
            client.requestCard(QString::number(101 + i), "viewer_a",
                               "channel_a");
            check(until([&] {
                      return result.cards == i + 1;
                  }) &&
                      result.uid == QString::number(101 + i),
                  "sequential user");
        }
        client.openView("context", "101", "viewer_a", "channel_a", "msg-1");
        check(until([&] {
                  return result.opens == 1;
              }),
              "open requires confirmed ack");
        check(peer.received == 26 && peer.operations == 13,
              "hello before every operation");
    }
    {
        Peer peer;
        QPointer<QLocalSocket> delayed;
        QJsonObject old;
        peer.behavior = [&](auto *socket, auto r) {
            if (r.value("op") == "card" && delayed.isNull())
            {
                delayed = socket;
                old = r;
            }
            else
                Peer::respond(socket, r);
        };
        ArchiveClient client(peer.endpoint(400));
        Results result;
        result.bind(client);
        client.requestCard("101", "viewer_a", "channel_a");
        check(until([&] {
                  return !old.isEmpty();
              }),
              "delayed request received");
        // Keep a separate count: the stale request may disconnect before B arrives.
        peer.behavior = [](auto *socket, auto r) {
            Peer::respond(socket, r);
        };
        client.requestCard("102", "viewer_a", "channel_a");
        client.requestCard("101", "viewer_a", "channel_a");
        check(until([&] {
                  return result.cards == 1;
              }) &&
                  result.uid == "101" && result.errors == 0,
              "A B A ignores cancelled stale response");
    }
    responseError(
        "wrong dataset rejected before card", "dataset_mismatch",
        [](auto *s, auto r) {
            auto d = base();
            d.insert("dataset_id", "other");
            Peer::write(s, r, d);
        },
        true);
    responseError(
        "wrong protocol rejected", "protocol_mismatch",
        [](auto *s, auto r) {
            auto d = base();
            d.insert("version", 1);
            Peer::write(s, r, d);
        },
        true);
    responseError("wrong UID rejected", "identity_mismatch",
                  [](auto *s, auto r) {
                      auto d = base();
                      d.insert("identity", QJsonObject{{"user_id", "999"}});
                      Peer::write(s, r, d);
                  });
    responseError("malformed JSON rejected", "malformed", [](auto *s, auto) {
        s->write("{broken}\n");
    });
    responseError("wrong response id rejected", "malformed",
                  [](auto *s, auto r) {
                      r.insert("id", -1);
                      Peer::respond(s, r);
                  });
    responseError(
        "oversized response bounded", "response_too_large", [](auto *s, auto) {
            s->write(QByteArray(ArchiveClient::MaxResponseBytes + 2, 'x'));
        });
    responseError("unsolicited duplicate frame rejected", "malformed",
                  [](auto *s, auto r) {
                      auto d = base();
                      d.insert("identity", QJsonObject{{"user_id", "101"}});
                      Peer::write(s, r, d);
                      s->write("{}\n");
                  });
    responseError("server busy handled", "busy", [](auto *s, auto r) {
        s->write(QJsonDocument(QJsonObject{{"id", r.value("id")},
                                           {"ok", false},
                                           {"error", "busy"}})
                     .toJson(QJsonDocument::Compact) +
                 '\n');
    });
    responseError("incomplete valid JSON is not a successful card", "malformed",
                  [](auto *s, auto r) {
                      auto d = base();
                      d.insert("identity", QJsonObject{{"user_id", "101"}});
                      Peer::write(s, r, d);
                  });
    responseError("out of range generated timestamp rejected", "malformed",
                  [](auto *s, auto r) {
                      auto d = base();
                      d.insert("identity", QJsonObject{{"user_id", "101"}});
                      d.insert("state", "empty");
                      d.insert("generated_at_ms", 1e300);
                      Peer::write(s, r, d);
                  });
    {
        Peer peer;
        peer.behavior = [](auto *s, auto r) {
            Peer::respond(s, r);
            if (r.value("op") == "card")
                s->disconnectFromServer();
        };
        ArchiveClient client(peer.endpoint());
        Results result;
        result.bind(client);
        client.requestCard("101", "viewer_a", "channel_a");
        check(until([&] {
                  return result.cards || result.errors;
              }) &&
                  result.cards == 1 && result.errors == 0,
              "complete final frame survives server immediate close");
    }
    {
        Peer peer;
        peer.behavior = [](auto *, auto) {};
        ArchiveClient client(peer.endpoint(30));
        Results result;
        result.bind(client);
        client.requestCard("101", "viewer_a", "channel_a");
        check(until([&] {
                  return result.errors == 1;
              }) &&
                  result.code == "timeout",
              "bounded timeout");
        peer.behavior = {};
        client.requestCard("102", "viewer_a", "channel_a");
        check(until([&] {
                  return result.cards == 1;
              }),
              "pipeline usable after timeout");
    }
    {
        Peer peer;
        ArchiveClient client(peer.endpoint());
        Results result;
        result.bind(client);
        client.requestCard("", "viewer_a", "channel_a");
        check(result.code == "identity_required" && peer.received == 0,
              "missing UID never falls back to nickname");
        client.openView("delete", "101", "viewer_a", "channel_a");
        check(result.code == "invalid_request" && peer.received == 0,
              "mutating navigation rejected locally");
        client.openView("message", "101", "viewer_a", "channel_a",
                        "../../file");
        check(result.code == "identity_required" && peer.received == 0,
              "invalid target id rejected");
    }
    {
        ArchiveClient client({"", ""});
        Results result;
        result.bind(client);
        client.requestCard("101", "viewer_a", "channel_a");
        check(result.code == "not_configured", "default connection disabled");
    }
    {
        Peer peer;
        peer.behavior = [](auto *, auto) {};
        auto *client = new ArchiveClient(peer.endpoint());
        client->requestCard("101", "viewer_a", "channel_a");
        until([&] {
            return peer.received == 1;
        });
        QElapsedTimer time;
        time.start();
        delete client;
        check(time.elapsed() < 50, "close during loading has no wait/join");
    }
    {
        ArchiveClient client(
            {"LeafyrinoArchive-test-no-such-server", "synthetic-v1", 50});
        Results result;
        result.bind(client);
        client.requestCard("101", "viewer_a", "channel_a");
        check(until([&] {
                  return result.errors > 0;
              }) &&
                  result.code == "unavailable",
              "offline Archive");
    }
    {
        Peer peer;
        ArchiveClient client(peer.endpoint());
        Results result;result.bind(client);
        QJsonObject seen;
        peer.behavior = [&](auto *socket, auto r) {
            if (r.value("op") == "history") seen = r;
            Peer::respond(socket, r);
        };
        const QJsonObject before{{"time_utc", "2026-10-20T01:00:00.000Z"}, {"id", "msg-z"}};
        client.requestHistory("101", "viewer_a", "channel_a", before);
        check(until([&] { return result.histories == 1; }) &&
                  seen.value("user_id") == "101" && seen.value("channel") == "channel_a" &&
                  seen.value("before").toObject() == before && seen.value("source") == "local",
              "history is async and binds UID/channel/exclusive cursor");
    }
    {
        Peer peer;
        ArchiveClient client(peer.endpoint());
        Results result;result.bind(client);
        int polls = 0;
        peer.behavior = [&](auto *socket, auto r) {
            if (r.value("op") != "history") { Peer::respond(socket, r);return; }
            auto data = base();
            data.insert("identity", QJsonObject{{"user_id", r.value("user_id")}});
            data.insert("channel", r.value("channel"));data.insert("scope", "channel");
            data.insert("source", "public_logs");
            data.insert("status", ++polls == 1 ? "loading" : "empty");
            data.insert("rows", QJsonArray{});data.insert("more", false);
            data.insert("next_cursor", QJsonValue::Null);data.insert("truncated", false);
            Peer::write(socket, r, data);
        };
        client.requestHistory("101", "viewer_a", "channel_a", {}, true);
        check(until([&] { return result.histories == 1; }, 700) && polls == 2 &&
                  result.loading == 1 && result.errors == 0,
              "public loading polls on same pipe and delivers final page");
        polls = 0;result.histories = 0;result.loading = 0;
        client.requestHistory("101", "viewer_a", "channel_a", {}, true);
        until([&] { return result.loading == 1; });client.cancel();
        QElapsedTimer wait;wait.start();
        until([&] { return wait.elapsed() > 350; }, 400);
        check(polls == 1 && result.histories == 0, "cancel public history stops pending poll");
    }
    {
        Peer peer;
        ArchiveClient client(peer.endpoint());
        Results result;result.bind(client);
        client.requestHistory("101", "viewer_a", "channel_a", {{"id", "bad"}});
        check(result.code == "invalid_request" && peer.received == 0,
              "invalid history cursor rejected before connection");
    }
    for (const auto &bad : {QStringLiteral("uid"), QStringLiteral("channel"), QStringLiteral("duplicate"), QStringLiteral("cursor")})
    {
        Peer peer;
        ArchiveClient client(peer.endpoint());Results result;result.bind(client);
        peer.behavior = [bad](auto *socket, auto r) {
            if (r.value("op") != "history") { Peer::respond(socket, r);return; }
            auto data = base();data.insert("identity", QJsonObject{{"user_id", r.value("user_id")}});
            data.insert("channel", r.value("channel"));data.insert("scope", "channel");
            data.insert("source", "local_archive");data.insert("status", "found");
            QJsonObject row{{"id", "msg-a"}, {"user_id", bad == "uid" ? "202" : "101"},
                            {"channel", bad == "channel" ? "other_room" : "channel_a"},
                            {"at_ms", 1}, {"time_utc", "2026-10-20T00:00:00.000Z"}, {"text", "older"}};
            QJsonArray rows{row};if (bad == "duplicate") rows.append(row);
            data.insert("rows", rows);data.insert("more", bad == "cursor");
            data.insert("next_cursor", bad == "cursor" ? QJsonValue(QJsonObject{{"id", "x"}}) : QJsonValue(QJsonValue::Null));
            data.insert("truncated", false);Peer::write(socket, r, data);
        };
        client.requestHistory("101", "viewer_a", "channel_a");
        check(until([&] { return result.errors == 1; }) && result.code == "malformed" && result.histories == 0,
              qPrintable("history rejects invalid " + bad));
    }
    {
        Peer peer;ArchiveClient client(peer.endpoint());Results result;result.bind(client);
        QJsonObject seen;
        peer.behavior = [&](auto *socket, auto r) {
            if (r.value("op") == "history") seen = r.value("before").toObject();
            Peer::respond(socket, r);
        };
        client.requestHistory("101", "viewer_a", "channel_a",
            {{"time_utc", "2026-09-15T10:28:14.33-03:00"}, {"id", "msg-a"}}, true);
        check(until([&] { return result.histories == 1; }) &&
                  seen.value("time_utc") == "2026-09-15T13:28:14.330Z",
              "public cursor canonicalizes offset and fractional width before validation");
    }
    std::printf("RESULT %d passed %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
