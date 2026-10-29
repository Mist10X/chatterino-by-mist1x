// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

// Public chat log checks against local stand-ins for the rustlog mirrors
// (fictional lines only). Uses the real PublicChatLogs and NetworkManager;
// nothing leaves 127.0.0.1.

#include "common/network/NetworkManager.hpp"
#include "providers/twitch/PublicChatLogs.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

namespace publiclogchecks {

constexpr qint64 BASE_MS = 1767225600000;  // 2026-01-01T00:00:00Z

struct Line {
    qint64 at;
    QByteArray raw;
};

inline QByteArray line(const QString &id, qint64 at, const QString &userId,
                       const QString &login, const QString &text,
                       bool first = false)
{
    return QStringLiteral("@badges=subscriber/12;color=#1E90FF;display-name=%1;"
                          "first-msg=%2;id=%3;tmi-sent-ts=%4;user-id=%5 "
                          ":%6!%6@%6.tmi.twitch.tv PRIVMSG #chan :%7")
        .arg(login.toUpper(), first ? "1" : "0", id, QString::number(at),
             userId, login, text)
        .toUtf8();
}

/// Three fake mirrors under /m0, /m1, /m2 of one local server.
class FakeMirrors final : public QTcpServer
{
public:
    std::vector<std::vector<Line>> lines{3};
    // Per mirror: a status answered instead of lines (0 = lines).
    std::vector<int> status{0, 0, 0};
    std::vector<QByteArray> statusBody{3};
    std::vector<std::vector<qint64>> arrivals{3};
    QElapsedTimer clock;

    FakeMirrors()
    {
        this->clock.start();
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

    QStringList bases() const
    {
        const auto root = QStringLiteral("http://127.0.0.1:%1/m").arg(this->serverPort());
        return {root + "0", root + "1", root + "2"};
    }

    int requests() const
    {
        int n = 0;
        for (const auto &a : this->arrivals)
        {
            n += int(a.size());
        }
        return n;
    }

private:
    void answer(QTcpSocket *socket)
    {
        const auto request = QString::fromUtf8(socket->readAll());
        const auto target = request.section(' ', 1, 1);
        const QUrl url(QStringLiteral("http://x") + target);
        const auto mirror = url.path().mid(2, 1).toInt();
        this->arrivals[size_t(mirror)].push_back(this->clock.elapsed());
        QByteArray body;
        QByteArray head = "HTTP/1.1 200 OK";
        if (this->status[size_t(mirror)] < 0)
        {
            return;  // a mirror that never answers
        }
        if (this->status[size_t(mirror)] != 0)
        {
            head = "HTTP/1.1 " +
                   QByteArray::number(this->status[size_t(mirror)]) + " X";
            body = this->statusBody[size_t(mirror)];
        }
        else
        {
            const QUrlQuery query(url);
            const auto to = QDateTime::fromString(query.queryItemValue("to", QUrl::FullyDecoded),
                                                  Qt::ISODateWithMs)
                                .toMSecsSinceEpoch();
            const auto limit = query.queryItemValue("limit").toInt();
            auto rows = this->lines[size_t(mirror)];
            std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
                return a.at > b.at;
            });
            int n = 0;
            for (const auto &row : rows)
            {
                if (row.at <= to && n < limit)
                {
                    body += row.raw + "\n";
                    ++n;
                }
            }
            if (n == 0)
            {
                head = "HTTP/1.1 404 Not Found";
                body = "No user logs found";
            }
        }
        socket->write(head + "\r\nContent-Type: text/plain\r\nConnection: close\r\n"
                             "Content-Length: " +
                      QByteArray::number(body.size()) + "\r\n\r\n" + body);
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

inline std::optional<QJsonObject> page(const QString &userId,
                                       const QJsonObject &before)
{
    std::optional<QJsonObject> result;
    chatterino::publiclogs::requestPage(
        userId, QStringLiteral("viewer"), QStringLiteral("chan"), before,
        nullptr, [&](const QJsonObject &value) {
            result = value;
        });
    waitUntil([&] {
        return result.has_value();
    }, 30000);
    return result;
}

/// Returns the number of failed checks; appends {name, ok} rows to `checks`.
inline int runPublicLogChecks(QJsonArray &checks)
{
    using namespace chatterino;
    int failed = 0;
    auto check = [&](const QString &name, bool ok) {
        failed += ok ? 0 : 1;
        checks.append(QJsonObject{{"name", name}, {"ok", ok}});
    };

    // Parsing: this person's message in this channel, nothing else.
    {
        const auto row = publiclogs::parseLine(
            line("x1", BASE_MS, "1001", "viewer", "hello there", true), "1001",
            "chan");
        check("a raw line becomes a row",
              row && row->value("id") == "x1" &&
                  row->value("text") == "hello there" &&
                  row->value("user") == "viewer" &&
                  row->value("display_name") == "VIEWER" &&
                  row->value("time_utc") == "2026-01-01T00:00:00.000Z" &&
                  row->value("first_msg").toBool() &&
                  row->value("badges") == "subscriber/12");
        check("another person's line is dropped",
              !publiclogs::parseLine(line("x2", BASE_MS, "2002", "other", "hi"),
                                     "1001", "chan"));
        check("another channel's line is dropped",
              !publiclogs::parseLine(line("x3", BASE_MS, "1001", "viewer", "hi"),
                                     "1001", "elsewhere"));
        check("a non-message line is dropped",
              !publiclogs::parseLine(
                  "@ban-duration=600;room-id=1;target-user-id=1001;"
                  "tmi-sent-ts=1 :tmi.twitch.tv CLEARCHAT #chan :viewer",
                  "1001", "chan"));
        check("opt-out bodies are told apart",
              publiclogs::refusalKind("User has opted out of being logged: "
                                      "requested user") == "user" &&
                  publiclogs::refusalKind("Channel has opted out of being "
                                          "logged") == "channel" &&
                  publiclogs::refusalKind("Forbidden").isEmpty());
    }

    FakeMirrors server;
    if (!server.listen(QHostAddress::LocalHost, 0))
    {
        check("fake mirrors listen on 127.0.0.1", false);
        return failed;
    }
    NetworkManager::init();
    publiclogs::setMirrorsForTesting(server.bases(), 300);

    // m0: 150 messages one second apart; m1: 30 others in between; m2: none.
    const auto now = QDateTime::currentMSecsSinceEpoch() - 60000;
    for (int i = 0; i < 150; ++i)
    {
        const auto at = now - qint64(i) * 1000;
        server.lines[0].push_back({at, line(QStringLiteral("a%1").arg(i, 3, 10, QChar('0')),
                                            at, "1001", "viewer",
                                            QStringLiteral("zonian %1").arg(i))});
    }
    for (int i = 0; i < 30; ++i)
    {
        const auto at = now - qint64(i) * 7000 - 500;
        server.lines[1].push_back({at, line(QStringLiteral("b%1").arg(i, 2, 10, QChar('0')),
                                            at, "1001", "viewer",
                                            QStringLiteral("spanix %1").arg(i))});
    }
    // A line of someone else that a careless mirror might attach.
    server.lines[1].push_back({now, line("z1", now, "2002", "other", "not theirs")});

    QSet<QString> seen;
    bool ordered = true;
    bool duplicate = false;
    bool sizes = true;
    QJsonObject cursor;
    int pages = 0;
    bool more = true;
    qint64 previous = std::numeric_limits<qint64>::max();
    while (more && pages < 10)
    {
        const auto result = page("1001", cursor);
        ++pages;
        if (!result || result->value("status") == "error")
        {
            more = false;
            sizes = false;
            break;
        }
        const auto rows = result->value("rows").toArray();
        sizes = sizes && rows.size() <= publiclogs::PAGE_SIZE;
        for (const auto &value : rows)
        {
            const auto row = value.toObject();
            const auto at = qint64(row.value("at_ms").toDouble());
            ordered = ordered && at <= previous;
            previous = at;
            duplicate = duplicate || seen.contains(row.value("id").toString());
            seen.insert(row.value("id").toString());
        }
        more = result->value("more").toBool();
        cursor = result->value("next_cursor").toObject();
    }
    check("pages cover both mirrors completely", seen.size() == 180 && !more);
    check("pages run newest to oldest without repeats", ordered && !duplicate);
    check("a page holds at most 100 messages", sizes && pages >= 2);
    check("someone else's line never shows", !seen.contains("z1"));

    // Spacing: two requests to one mirror never closer than its spacing.
    qint64 gap = std::numeric_limits<qint64>::max();
    for (const auto &times : server.arrivals)
    {
        for (size_t i = 1; i < times.size(); ++i)
        {
            gap = std::min(gap, times[i] - times[i - 1]);
        }
    }
    checks.append(QJsonObject{{"name", "smallest gap, ms"}, {"ok", true},
                              {"value", double(gap)}});
    check("requests to one mirror are spaced", gap >= 240);  // 300 ms, less delivery jitter

    // One mirror fails: the page says so and is not the end.
    server.status = {500, 0, 0};
    server.statusBody[0] = "oops";
    {
        const auto result = page("1001", {});
        check("a failed mirror makes the page partial",
              result && result->value("status") == "found" &&
                  result->value("error") == "partial_source");
    }

    // An opt-out is respected and remembered: no mirror is asked again.
    server.status = {0, 403, 0};
    server.statusBody[1] = "User has opted out of being logged: requested user";
    {
        const auto result = page("1001", {});
        check("an opted-out person is refused",
              result && result->value("status") == "refused" &&
                  result->value("rows").toArray().isEmpty());
        const auto before = server.requests();
        const auto again = page("1001", {});
        check("an opt-out is not asked again",
              again && again->value("status") == "refused" &&
                  server.requests() == before);
    }

    // Nothing anywhere: an empty, final answer.
    server.status = {0, 0, 0};
    {
        const auto result = page("3003", {});
        check("a person without logs gets an empty final page",
              result && result->value("status") == "empty" &&
                  !result->value("more").toBool());
    }
    // A silent mirror: the page does not wait for it long, and the next page
    // leaves it out instead of waiting again.
    server.status = {0, 0, -1};
    publiclogs::setMirrorsForTesting(server.bases(), 300);  // forget the failures above
    for (int i = 0; i < 5; ++i)
    {
        const auto at = now - qint64(i) * 1000;
        server.lines[0].push_back({at, line(QStringLiteral("q%1").arg(i), at,
                                            "4004", "quiet", "hi")});
    }
    {
        QElapsedTimer timer;
        timer.start();
        const auto first = page("4004", {});
        const auto firstMs = timer.elapsed();
        check("a silent mirror delays a page only briefly",
              first && first->value("rows").toArray().size() == 5 &&
                  first->value("error") == "partial_source" && firstMs < 5500);
        const auto silentBefore = server.arrivals[2].size();
        timer.restart();
        const auto second = page("4004", {});
        check("the silent mirror is then left out",
              second && second->value("rows").toArray().size() == 5 &&
                  !second->contains("error") && timer.elapsed() < 2500 &&
                  server.arrivals[2].size() == silentBefore);
    }
    check("bad input is rejected without a request",
          page("12ab", {}).value_or(QJsonObject{}).value("error") ==
              "invalid_request");
    return failed;
}

}  // namespace publiclogchecks
