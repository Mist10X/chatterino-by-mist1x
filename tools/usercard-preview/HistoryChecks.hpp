// SPDX-License-Identifier: MIT
// Actual-widget, synthetic local-IPC checks. No accounts, HTTP or application start.
#pragma once

#include "Application.hpp"
#include "common/Channel.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "singletons/Settings.hpp"
#include "widgets/buttons/LabelButton.hpp"
#include "widgets/dialogs/UserInfoPopup.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/Scrollbar.hpp"
#include "widgets/splits/Split.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <QVector>
#include <QWheelEvent>

#include <algorithm>
#include <functional>
#include <memory>

namespace chatterino::preview {

class HistoryFixtureServer final : public QObject
{
public:
    enum class Mode { Basic, WrongUid, Delayed, Overlap, Silent };
    explicit HistoryFixtureServer(QObject *parent = nullptr)
        : QObject(parent)
        , server_(this)
        , name_(QStringLiteral("LeafyrinoArchive-test-history-%1-%2")
                    .arg(QCoreApplication::applicationPid())
                    .arg(QUuid::createUuid().toString(QUuid::Id128)))
    {
        this->server_.setSocketOptions(QLocalServer::UserAccessOption);
        this->listening_ = this->server_.listen(this->name_);
        QObject::connect(
            &this->server_, &QLocalServer::newConnection, this, [this] {
                while (this->server_.hasPendingConnections())
                {
                    auto *socket = this->server_.nextPendingConnection();
                    auto bytes = std::make_shared<QByteArray>();
                    QObject::connect(
                        socket, &QLocalSocket::readyRead, this,
                        [this, socket, bytes] {
                            bytes->append(socket->readAll());
                            if (bytes->size() > 65536)
                            {
                                socket->abort();
                                return;
                            }
                            while (bytes->contains('\n'))
                            {
                                auto end = bytes->indexOf('\n');
                                auto request =
                                    QJsonDocument::fromJson(bytes->left(end))
                                        .object();
                                bytes->remove(0, end + 1);
                                this->requests_.append(request);
                                this->respond(socket, request);
                            }
                        });
                    QObject::connect(socket, &QLocalSocket::disconnected, this,
                                     [this, socket] {
                                         ++this->disconnections_;
                                         socket->deleteLater();
                                     });
                }
            });
    }
    bool listening() const
    {
        return this->listening_;
    }
    QString socketName() const
    {
        return this->name_;
    }
    QVector<QJsonObject> requests() const
    {
        return this->requests_;
    }
    int disconnections() const
    {
        return this->disconnections_;
    }
    void clearRequests()
    {
        this->requests_.clear();
    }
    void setMode(Mode mode)
    {
        this->mode_ = mode;
        this->polls_.clear();
        this->delayed_ = false;
        this->errored_ = false;
    }
    int count(const QString &source) const
    {
        return std::count_if(this->requests_.cbegin(), this->requests_.cend(),
                             [&](const auto &r) {
                                 return r.value("op") == "history" &&
                                        r.value("source") == source;
                             });
    }
    static QJsonObject row(QString id, QString when, QString uid = "910000001")
    {
        return {{"id", id},
                {"user", "archive_demo"},
                {"user_id", uid},
                {"channel", "archive_room"},
                {"time_utc", when},
                {"at_ms", QDateTime::fromString(when, Qt::ISODateWithMs)
                              .toMSecsSinceEpoch()},
                {"text", "Synthetic history " + id},
                {"automod", false},
                {"source", "synthetic-fixture"}};
    }
    static QJsonObject cursor(const QJsonObject &message)
    {
        return {{"time_utc", message.value("time_utc")},
                {"id", message.value("id")}};
    }

private:
    static QJsonObject base()
    {
        return {{"protocol", "archive-card"},
                {"version", 2},
                {"dataset_id", "leafyrino-card-demo-v1"}};
    }
    static void write(QLocalSocket *socket, const QJsonObject &request,
                      QJsonObject data)
    {
        if (socket->state() != QLocalSocket::ConnectedState)
            return;
        socket->write(QJsonDocument(QJsonObject{{"id", request.value("id")},
                                                {"ok", true},
                                                {"data", data}})
                          .toJson(QJsonDocument::Compact) +
                      '\n');
        socket->flush();
    }
    QJsonObject page(const QJsonObject &request, QJsonArray rows,
                     bool more = false, QString status = "found")
    {
        QVector<QJsonObject> ordered;
        for (const auto &value : rows)
            ordered.append(value.toObject());
        std::sort(
            ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
                return a.value("time_utc") == b.value("time_utc")
                           ? a.value("id").toString() > b.value("id").toString()
                           : a.value("time_utc").toString() >
                                 b.value("time_utc").toString();
            });
        rows = {};
        for (const auto &value : ordered)
            rows.append(value);
        auto data = base();
        data.insert("identity",
                    QJsonObject{{"user_id", request.value("user_id")},
                                {"user", "archive_demo"}});
        data.insert("channel", request.value("channel"));
        data.insert("scope", "channel");
        data.insert("source", request.value("source") == "public"
                                  ? "public_logs"
                                  : "local_archive");
        data.insert("status", status);
        data.insert("total", QJsonValue::Null);
        data.insert("rows", rows);
        data.insert("more", more);
        data.insert("truncated", false);
        data.insert("next_cursor",
                    more && !rows.isEmpty()
                        ? QJsonValue(cursor(rows.last().toObject()))
                        : QJsonValue(QJsonValue::Null));
        return data;
    }
    void respond(QLocalSocket *socket, const QJsonObject &request)
    {
        const auto op = request.value("op").toString();
        if (op == "hello")
        {
            write(socket, request, base());
            return;
        }
        if (op != "history")
        {
            socket->write(
                QJsonDocument(QJsonObject{{"id", request.value("id")},
                                          {"ok", false},
                                          {"error", "not_configured"}})
                    .toJson(QJsonDocument::Compact) +
                '\n');
            return;
        }
        if (this->mode_ == Mode::Silent)
            return;
        const auto uid = request.value("user_id").toString();
        if (this->mode_ == Mode::WrongUid)
        {
            write(socket, request,
                  this->page(request,
                             {row("wrong-uid", "2026-10-18T12:00:00.000Z",
                                  "910000002")}));
            return;
        }
        if (this->mode_ == Mode::Delayed)
        {
            const bool delay = uid == "910000001" && !this->delayed_;
            this->delayed_ |= delay;
            auto data = this->page(
                request, {row(delay ? "stale-delayed-A" : "fresh-" + uid,
                              "2026-10-18T12:00:00.000Z", uid)});
            if (delay)
            {
                QPointer<QLocalSocket> target(socket);
                QTimer::singleShot(400, this, [target, request, data] {
                    if (target)
                        write(target, request, data);
                });
            }
            else
                write(socket, request, data);
            return;
        }
        const auto before = request.value("before").toObject();
        if (this->mode_ == Mode::Overlap)
        {
            int high = before.isEmpty()
                           ? 1049
                           : before.value("id").toString().mid(8).toInt() - 1;
            QJsonArray rows;
            const auto start = QDateTime::fromString("2026-10-19T12:00:00.000Z",
                                                     Qt::ISODateWithMs);
            for (int i = high; i >= std::max(0, high - 49); --i)
                rows.append(row(
                    QStringLiteral("overlap-%1").arg(i, 4, 10, QChar('0')),
                    start.addSecs(i).toString("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"),
                    uid));
            write(socket, request, this->page(request, rows, high >= 50));
            return;
        }
        if (request.value("source") != "public")
        {
            if (before.isEmpty())
                write(socket, request,
                      this->page(
                          request,
                          {row("demo-30", "2026-10-19T20:00:30.000Z", uid),
                           row("local-18", "2026-10-18T12:00:00.000Z", uid)},
                          true));
            else
                write(socket, request,
                      this->page(request,
                                 {row("local-2024", "2024-01-10T12:00:00.000Z",
                                      uid)}));
            return;
        }
        const auto key = uid + QString::fromUtf8(QJsonDocument(before).toJson(
                                   QJsonDocument::Compact));
        if (before.isEmpty() && ++this->polls_[key] == 1)
        {
            write(socket, request, this->page(request, {}, false, "loading"));
            return;
        }
        if (before.isEmpty())
            write(socket, request,
                  this->page(request,
                             {row("public-19", "2026-10-19T12:00:00.000Z", uid),
                              row("local-18", "2026-10-18T12:00:00.000Z", uid)},
                             true));
        else if (before.value("id") == "local-18")
            write(
                socket, request,
                this->page(request,
                           {row("local-2024", "2024-01-10T12:00:00.000Z", uid)},
                           true));
        else if (!this->errored_)
        {
            this->errored_ = true;
            auto data = this->page(request, {}, false, "error");
            data.insert("error", "timeout");
            write(socket, request, data);
        }
        else
            write(socket, request,
                  this->page(request, {row("public-2023",
                                           "2023-01-10T12:00:00.000Z", uid)}));
    }
    QLocalServer server_;
    QString name_;
    bool listening_ = false;
    Mode mode_ = Mode::Basic;
    QVector<QJsonObject> requests_;
    QHash<QString, int> polls_;
    int disconnections_ = 0;
    bool delayed_ = false, errored_ = false;
};

class HistoryChecksRunner final : public QObject
{
public:
    using Fixture = std::function<UserInfoPreviewData(QString)>;
    using Check = std::function<void(QString, bool)>;
    HistoryChecksRunner(UserInfoPopup &popup, Split &split,
                        HistoryFixtureServer &server, Fixture fixture,
                        Check check, std::function<void()> done)
        : QObject(&popup)
        , popup_(&popup)
        , split_(&split)
        , server_(server)
        , fixture_(std::move(fixture))
        , check_(std::move(check))
        , done_(std::move(done))
    {
        heartbeat_.setInterval(10);
        QObject::connect(&heartbeat_, &QTimer::timeout, this, [this] {
            ++beats_;
        });
        heartbeat_.start();
    }
    void start()
    {
        this->basic();
    }

private:
    ChannelView *view() const
    {
        return this->popup_ ? this->popup_->findChild<ChannelView *>()
                            : nullptr;
    }
    MessagePtr find(QString id) const
    {
        auto *v = this->view();
        if (!v)
            return {};
        for (const auto &m : v->channel()->getMessageSnapshot())
            if (m->id == id)
                return m;
        return {};
    }
    LabelButton *button(UserInfoPopup *target = nullptr) const
    {
        if (!target)
            target = this->popup_;
        if (!target)
            return nullptr;
        for (auto *widget : target->findChildren<QWidget *>())
            if (widget->objectName() == "UsercardHistory")
                if (auto *b = dynamic_cast<LabelButton *>(widget))
                    return b;
        return nullptr;
    }
    void click(UserInfoPopup *target = nullptr)
    {
        if (auto *b = this->button(target))
            Q_EMIT b->leftClicked();
    }
    void up()
    {
        auto *v = this->view();
        if (!v)
            return;
        v->getScrollBar().scrollToTop(false);
        QWheelEvent event(QPointF(12, 12),
                          QPointF(v->mapToGlobal(QPoint(12, 12))), QPoint(),
                          QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(v, &event);
    }
    void wait(std::function<bool()> predicate, std::function<void()> next,
              QString label, int limit = 4000)
    {
        auto *timer = new QTimer(this);
        auto elapsed = std::make_shared<QElapsedTimer>();
        elapsed->start();
        QObject::connect(timer, &QTimer::timeout, this,
                         [this, timer, elapsed, predicate, next, label, limit] {
                             const bool ready = predicate();
                             if (!ready && elapsed->elapsed() < limit)
                                 return;
                             timer->stop();
                             timer->deleteLater();
                             this->check_(label, ready);
                             QTimer::singleShot(0, this, next);
                         });
        timer->start(10);
    }
    void install(QString name)
    {
        auto data = this->fixture_(name);
        data.allowPublicHistory = true;
        this->popup_->setOfflinePreviewData(data);
    }
    void basic()
    {
        this->server_.setMode(HistoryFixtureServer::Mode::Basic);
        this->server_.clearRequests();
        this->install("a");
        this->original_ = this->find("demo-30");
        this->click();
        this->wait(
            [this] {
                return bool(this->find("local-18"));
            },
            [this] {
                this->check_("history preserves original rich same-ID message",
                             this->original_ &&
                                 this->find("demo-30") == this->original_);
                // Saved rows carry no colour: they take the person's own (the
                // card's, from Twitch), not one picked from the user ID.
                const auto saved = this->find("local-18");
                this->check_("saved history rows use the person's chat colour",
                             saved && saved->usernameColor ==
                                          QColor(QStringLiteral("#E07700")));
                this->up();
                this->wait(
                    [this] {
                        return bool(this->find("local-2024"));
                    },
                    [this] {
                        this->publicFirst();
                    },
                    "actual up-wheel loads older local page");
            },
            "history button loads local saved text");
    }
    void publicFirst()
    {
        const auto beats = this->beats_;
        this->click();
        this->wait(
            [this] {
                return bool(this->find("public-19"));
            },
            [this, beats] {
                bool initialEmpty = false;
                for (const auto &r : this->server_.requests())
                    if (r.value("op") == "history" &&
                        r.value("source") == "public")
                    {
                        initialEmpty = !r.contains("before") ||
                                       r.value("before").toObject().isEmpty();
                        break;
                    }
                this->check_(
                    "public initial cursor independent of 2024 local cursor",
                    initialEmpty);
                this->check_(
                    "GUI heartbeat continues during async public polling",
                    this->beats_ - beats >= 10);
                int copies = 0, dates = 0;
                for (const auto &m :
                     this->view()->channel()->getMessageSnapshot())
                {
                    copies += m->id == "local-18";
                    dates += m->flags.has(MessageFlag::System) &&
                             m->flags.has(MessageFlag::Centered);
                }
                this->check_("date rows are centered System and duplicate ID "
                             "appears once",
                             copies == 1 && dates >= 2);
                this->click();
                this->wait(
                    [this] {
                        return this->server_.count("public") >= 3 &&
                               this->button()->enabled();
                    },
                    [this] {
                        this->click();
                        this->wait(
                            [this] {
                                return this->server_.count("public") >= 4 &&
                                       this->button()->enabled();
                            },
                            [this] {
                                this->click();
                                this->wait(
                                    [this] {
                                        return bool(this->find("public-2023"));
                                    },
                                    [this] {
                                        this->wrongUid();
                                    },
                                    "duplicate-only public page does not imply "
                                    "end; timeout can retry");
                            },
                            "public timeout does not permanently disable "
                            "history");
                    },
                    "public duplicate-only page advances cursor");
            },
            "public loading poll delivers older text");
    }
    void wrongUid()
    {
        this->server_.setMode(HistoryFixtureServer::Mode::WrongUid);
        this->install("a");
        this->click();
        this->wait(
            [this] {
                return this->button() && this->button()->enabled();
            },
            [this] {
                this->check_("other UID rejected before importing message",
                             !this->find("wrong-uid"));
                this->server_.setMode(HistoryFixtureServer::Mode::Basic);
                this->click();
                this->wait(
                    [this] {
                        return bool(this->find("local-18"));
                    },
                    [this] {
                        this->identities();
                    },
                    "valid history retry works after malformed UID page");
            },
            "wrong UID reply returns from loading");
    }
    void identities()
    {
        this->server_.setMode(HistoryFixtureServer::Mode::Delayed);
        this->install("a");
        this->click();
        QTimer::singleShot(40, this, [this] {
            this->install("b");
            this->click();
            this->wait(
                [this] {
                    return bool(this->find("fresh-910000002"));
                },
                [this] {
                    this->install("a");
                    this->click();
                    this->wait(
                        [this] {
                            return bool(this->find("fresh-910000001"));
                        },
                        [this] {
                            QTimer::singleShot(500, this, [this] {
                                this->check_(
                                    "A-B-A ignores delayed prior socket/UID "
                                    "result",
                                    !this->find("stale-delayed-A") &&
                                        !this->find("fresh-910000002") &&
                                        bool(this->find("fresh-910000001")));
                                this->overlap();
                            });
                        },
                        "second A target loads independently");
                },
                "B target does not wait for delayed A");
        });
    }
    void overlap()
    {
        this->server_.setMode(HistoryFixtureServer::Mode::Overlap);
        this->server_.clearRequests();
        this->oldSplitLimit_ = getSettings()->scrollbackSplitLimit.getValue();
        this->oldCardLimit_ = getSettings()->scrollbackUsercardLimit.getValue();
        getSettings()->scrollbackSplitLimit = 1500;
        getSettings()->scrollbackUsercardLimit = 1000;
        auto data = this->fixture_("a");
        data.allowPublicHistory = true;
        data.messages =
            std::make_shared<Channel>("archive_room", Channel::Type::None);
        data.sourceMessages.reset();
        const auto start = QDateTime::fromString("2026-10-19T12:00:00.000Z",
                                                 Qt::ISODateWithMs);
        for (int i = 50; i < 1050; ++i)
        {
            MessageBuilder b;
            auto &m = b.message();
            m.id = QStringLiteral("overlap-%1").arg(i, 4, 10, QChar('0'));
            m.userID = data.userId;
            m.loginName = data.login;
            m.displayName = data.login;
            m.channelName = "archive_room";
            m.serverReceivedTime = start.addSecs(i);
            m.messageText = "Synthetic overlap";
            b.emplace<TextElement>(m.messageText, MessageElementFlag::Text);
            data.messages->addMessage(b.release(), MessageContext::Repost);
        }
        this->popup_->setOfflinePreviewData(data);
        this->up();
        this->wait(
            [this] {
                return bool(this->find("overlap-0049"));
            },
            [this] {
                this->check_("1000 duplicate rows drain without 20 extra "
                             "gestures/public requests",
                             this->server_.count("local") >= 21 &&
                                 this->server_.count("public") == 0);
                auto *v = this->view();
                v->getScrollBar().scrollToBottom(false);
                const auto snapshot = v->channel()->getMessageSnapshot();
                const auto anchor =
                    snapshot.empty() ? QString{} : snapshot.back()->id;
                this->anchor_ = anchor;
                this->server_.setMode(HistoryFixtureServer::Mode::Basic);
                this->click();
                this->wait(
                    [this] {
                        return this->server_.count("public") >= 2 &&
                               this->button()->enabled();
                    },
                    [this] {
                        this->check_(
                            "bounded window retains near-bottom anchor ID",
                            this->anchor_.isEmpty() ||
                                bool(this->find(this->anchor_)));
                        const auto requests = this->server_.count("local") +
                                              this->server_.count("public");
                        this->up();
                        QTimer::singleShot(100, this, [this, requests] {
                            this->check_(
                                "cached older range is shown before another "
                                "IPC/source query",
                                bool(this->find("overlap-0001")) &&
                                    this->server_.count("local") +
                                            this->server_.count("public") ==
                                        requests);
                            this->closePending();
                        });
                    },
                    "new source page merges while the full window has a bottom "
                    "anchor");
            },
            "one actual scroll reaches older-than-live data through 20 local "
            "pages",
            8000);
    }
    void closePending()
    {
        getSettings()->scrollbackSplitLimit = this->oldSplitLimit_;
        getSettings()->scrollbackUsercardLimit = this->oldCardLimit_;
        this->server_.setMode(HistoryFixtureServer::Mode::Silent);
        auto *closing = new UserInfoPopup(false, this->split_, true);
        closing->setAttribute(Qt::WA_DeleteOnClose);
        auto data = this->fixture_("a");
        data.allowPublicHistory = true;
        closing->setOfflinePreviewData(data);
        auto destroyed = std::make_shared<bool>(false);
        QObject::connect(closing, &QObject::destroyed, this, [destroyed] {
            *destroyed = true;
        });
        const auto disconnects = this->server_.disconnections();
        this->click(closing);
        QTimer::singleShot(60, this, [closing] {
            closing->close();
        });
        this->wait(
            [this, destroyed, disconnects] {
                return *destroyed &&
                       this->server_.disconnections() > disconnects;
            },
            [this] {
                this->heartbeat_.stop();
                this->server_.setMode(HistoryFixtureServer::Mode::Basic);
                auto done = std::move(this->done_);
                this->deleteLater();
                done();
            },
            "closing pending history destroys popup and disconnects pipe", 500);
    }
    QPointer<UserInfoPopup> popup_;
    Split *split_;
    HistoryFixtureServer &server_;
    Fixture fixture_;
    Check check_;
    std::function<void()> done_;
    QTimer heartbeat_;
    int beats_ = 0;
    MessagePtr original_;
    QString anchor_;
    int oldSplitLimit_ = 6000, oldCardLimit_ = 6000;
};

inline void runHistoryChecks(
    UserInfoPopup &popup, Split &split, HistoryFixtureServer &server,
    std::function<UserInfoPreviewData(QString)> fixture,
    std::function<void(QString, bool)> check, std::function<void()> done)
{
    auto *runner =
        new HistoryChecksRunner(popup, split, server, std::move(fixture),
                                std::move(check), std::move(done));
    runner->start();
}

}  // namespace chatterino::preview
