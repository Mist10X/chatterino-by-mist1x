// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/irc/IrcConnection2.hpp"

#include "Application.hpp"
#include "common/Env.hpp"
#include "common/QLogging.hpp"
#include "common/Version.hpp"
#include "providers/irc/IrcProxy.hpp"
#include "singletons/Paths.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QMutex>
#include <QSslCipher>
#include <QSslSocket>

#include <chrono>

using namespace std::chrono_literals;

namespace chatterino {

namespace {

const auto payload = "chatterino/" + CHATTERINO_VERSION;

QString socketErrorText(QAbstractSocket::SocketError error, QAbstractSocket *socket)
{
    switch (error)
    {
        case QAbstractSocket::ConnectionRefusedError:
            return QStringLiteral("сервер отклонил соединение");
        case QAbstractSocket::RemoteHostClosedError:
            return QStringLiteral("сервер или сеть оборвали соединение");
        case QAbstractSocket::HostNotFoundError:
            return QStringLiteral("адрес сервера не найден (DNS)");
        case QAbstractSocket::SocketTimeoutError:
            return QStringLiteral("сервер не ответил вовремя");
        case QAbstractSocket::NetworkError:
            return QStringLiteral("сеть недоступна");
        case QAbstractSocket::SslHandshakeFailedError:
            return QStringLiteral("не удалось установить защищённое соединение");
        case QAbstractSocket::ProxyConnectionRefusedError:
        case QAbstractSocket::ProxyConnectionClosedError:
        case QAbstractSocket::ProxyConnectionTimeoutError:
        case QAbstractSocket::ProxyNotFoundError:
            return QStringLiteral("прокси недоступен");
        default:
            break;
    }
    return socket ? socket->errorString() : QStringLiteral("ошибка сети");
}

}

IrcConnection::IrcConnection(QObject *parent)
    : Communi::IrcConnection(parent)
{
    QObject::connect(this, &Communi::IrcConnection::socketError, this,
                     [this](QAbstractSocket::SocketError error) {
                         qCDebug(chatterinoIrc) << "Connection error:" << error;
                         this->lastError_ = socketErrorText(error, this->socket());
                         this->logEvent(
                             QStringLiteral("error %1: %2 (%3)")
                                 .arg(int(error))
                                 .arg(this->lastError_,
                                      this->socket() ? this->socket()->errorString()
                                                     : QString()));
                     });
    QObject::connect(this, &Communi::IrcConnection::secureError, this, [this] {
        QStringList errors;
        if (auto *ssl = qobject_cast<QSslSocket *>(this->socket()))
        {
            for (const auto &e : ssl->sslHandshakeErrors())
            {
                errors.append(e.errorString());
            }
        }
        this->lastError_ = QStringLiteral("ошибка защищённого соединения");
        this->logEvent(QStringLiteral("tls error: ") + errors.join("; "));
    });
    QObject::connect(this, &Communi::IrcConnection::connecting, this, [this] {
        this->logEvent(QStringLiteral("connecting to %1:%2%3")
                           .arg(this->host())
                           .arg(this->port())
                           .arg(this->isSecure() ? " tls" : ""));
    });
    QObject::connect(this, &Communi::IrcConnection::connected, this, [this] {
        this->lastError_.clear();
        const auto *socket = this->socket();
        this->logEvent(QStringLiteral("connected, server %1")
                           .arg(socket ? socket->peerAddress().toString()
                                       : QString()));
    });
    QObject::connect(this, &Communi::IrcConnection::disconnected, this, [this] {
        this->logEvent(QStringLiteral("disconnected"));
    });
    // Each step of the way, to tell DNS / TCP / TLS / login problems apart.
    QObject::connect(
        this, &Communi::IrcConnection::socketStateChanged, this,
        [this](QAbstractSocket::SocketState state) {
            auto *socket = this->socket();
            switch (state)
            {
                case QAbstractSocket::HostLookupState:
                    this->logEvent(QStringLiteral("dns lookup"));
                    break;
                case QAbstractSocket::ConnectingState:
                    this->logEvent(QStringLiteral("tcp connecting"));
                    break;
                case QAbstractSocket::ConnectedState:
                    this->logEvent(
                        QStringLiteral("tcp connected to %1:%2")
                            .arg(socket ? socket->peerAddress().toString()
                                        : QString())
                            .arg(socket ? socket->peerPort() : 0));
                    if (auto *ssl = qobject_cast<QSslSocket *>(socket);
                        ssl && !ssl->property("leafyrinoDiagnostics").toBool())
                    {
                        ssl->setProperty("leafyrinoDiagnostics", true);
                        QObject::connect(ssl, &QSslSocket::encrypted, this,
                                         [this, ssl] {
                                             this->logEvent(
                                                 QStringLiteral("tls ok (%1)")
                                                     .arg(ssl->sessionCipher()
                                                              .name()));
                                         });
                    }
                    break;
                case QAbstractSocket::UnconnectedState:
                    this->logEvent(QStringLiteral("socket closed"));
                    break;
                default:
                    break;
            }
        });

    QObject::connect(this, &Communi::IrcConnection::socketStateChanged, this,
                     [this](QAbstractSocket::SocketState state) {
                         if (state == QAbstractSocket::UnconnectedState)
                         {
                             this->pingTimer_.stop();

                             if (!this->expectConnectionLoss_.load())
                             {
                                 this->connectionLost.invoke(false);
                             }
                         }
                     });

    this->reconnectTimer_.setSingleShot(true);
    QObject::connect(&this->reconnectTimer_, &QTimer::timeout, [this] {
        if (this->isConnected())
        {
            qCDebug(chatterinoIrc) << "Reconnect: already reconnected";
        }
        else
        {
            qCDebug(chatterinoIrc) << "Reconnecting";
            this->open();
        }
    });

    this->pingTimer_.setInterval(5000);
    this->pingTimer_.start();
    this->lastPing_ = std::chrono::system_clock::now();
    QObject::connect(&this->pingTimer_, &QTimer::timeout, [this] {
        if (this->isConnected())
        {
            if (this->recentlyReceivedMessage_.load())
            {
                this->recentlyReceivedMessage_ = false;
                this->waitingForPong_ = false;

                auto now = std::chrono::system_clock::now();
                auto elapsed = now - this->lastPing_;
                if (elapsed < 3 * 5000ms)
                {
                    this->heartbeat.invoke();
                }
                else
                {
                    qCDebug(chatterinoIrc).nospace()
                        << "Got late ping (skipping heartbeat): "
                        << std::chrono::duration_cast<
                               std::chrono::milliseconds>(elapsed)
                               .count()
                        << "ms";
                }
                this->lastPing_ = now;

                return;
            }

            if (this->waitingForPong_.load())
            {
                this->close();
                this->connectionLost.invoke(true);
            }
            else
            {
                this->sendRaw("PING " + payload);
                this->waitingForPong_ = true;
            }
        }
    });

    QObject::connect(this, &Communi::IrcConnection::connected, this, [this] {
        this->pingTimer_.start();
    });

    QObject::connect(this, &Communi::IrcConnection::pongMessageReceived,
                     [this](Communi::IrcPongMessage *message) {
                         if (message->argument() == payload)
                         {
                             this->waitingForPong_ = false;
                         }
                     });

    QObject::connect(this, &Communi::IrcConnection::messageReceived,
                     [this](Communi::IrcMessage *message) {
                         this->recentlyReceivedMessage_ = true;

                         if (message->command() == "372")
                         {
                             this->reconnectBackoff_.reset();
                         }
                         if (message->command() == "001")
                         {
                             this->logEvent(QStringLiteral("logged in"));
                         }
                         else if (message->command() == "NOTICE" &&
                                  message->parameter(0) == "*")
                         {
                             // server notices such as "Login authentication failed"
                             this->logEvent(QStringLiteral("server notice: ") +
                                            message->parameter(1).left(200));
                         }
                     });
}

IrcConnection::~IrcConnection()
{
    this->disconnect();
}

std::chrono::milliseconds IrcConnection::smartReconnect()
{
    if (this->reconnectTimer_.isActive())
    {
        return std::chrono::milliseconds{0};
    }

    auto delay = this->reconnectBackoff_.next();
    qCDebug(chatterinoIrc) << "Reconnecting in" << delay.count() << "ms";
    this->logEvent(QStringLiteral("reconnect in %1 ms").arg(delay.count()));
    this->reconnectTimer_.start(delay);
    return delay;
}

void IrcConnection::setDiagnosticsLabel(const QString &label)
{
    this->label_ = label;
}

QString IrcConnection::lastError() const
{
    return this->lastError_;
}

void IrcConnection::logEvent(const QString &text) const
{
    // Small rolling file inside the profile: what happened to the chat
    // connection and why (no tokens, no messages).
    static QMutex mutex;
    auto *app = tryGetApp();
    if (app == nullptr)
    {
        return;
    }
    QMutexLocker locker(&mutex);
    const auto path =
        QDir(app->getPaths().messageLogDirectory).filePath("connection.log");
    QFile file(path);
    if (file.size() > 1024 * 1024)
    {
        QFile::remove(path + ".old");
        QFile::rename(path, path + ".old");
    }
    if (file.open(QIODevice::Append | QIODevice::Text))
    {
        file.write((QDateTime::currentDateTime().toString(
                        "yyyy-MM-dd HH:mm:ss.zzz") +
                    QStringLiteral(" [%1] ").arg(this->label_) + text + '\n')
                       .toUtf8());
    }
}

void IrcConnection::open()
{
    this->expectConnectionLoss_ = false;
    this->waitingForPong_ = false;
    this->recentlyReceivedMessage_ = false;
    switch (refreshIrcSystemProxy(this->socket(), Env::get().proxyUrl.has_value()))
    {
        case IrcProxyRefresh::Direct:
            this->logEvent(QStringLiteral("proxy mode: current system DIRECT"));
            break;
        case IrcProxyRefresh::SystemDefault:
            this->logEvent(QStringLiteral("proxy mode: current system resolver"));
            break;
        case IrcProxyRefresh::Unchanged:
            break;
    }
    Communi::IrcConnection::open();
}

void IrcConnection::close()
{
    this->expectConnectionLoss_ = true;
    Communi::IrcConnection::close();
}

}  // namespace chatterino
