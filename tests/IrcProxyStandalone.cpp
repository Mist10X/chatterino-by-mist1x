// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/irc/IrcProxy.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QSslSocket>
#include <QTcpServer>
#include <QThread>
#include <QVariant>

#include <functional>
#include <memory>
#include <vector>

using chatterino::IrcProxyRefresh;
using chatterino::detail::applyIrcProxyPolicy;
using chatterino::detail::systemProxyIsDirect;
using chatterino::detail::SystemProxyState;

namespace {
QJsonArray results;
int failures = 0;

void check(const QString &name, bool ok, const QString &detail = {})
{
    QJsonObject row{{"name", name}, {"passed", ok}};
    if (!detail.isEmpty())
        row.insert("detail", detail);
    results.append(row);
    if (!ok)
        ++failures;
}

bool spinUntil(const std::function<bool()> &done, int timeoutMs = 2500)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return done();
}

bool isOurDirect(QAbstractSocket &socket)
{
    return socket.proxy().type() == QNetworkProxy::NoProxy &&
           socket.property("leafyrinoLiveSystemProxy").toBool();
}

bool isDefault(QAbstractSocket &socket)
{
    return socket.proxy().type() == QNetworkProxy::DefaultProxy &&
           !socket.property("leafyrinoLiveSystemProxy").toBool();
}

void policyTests()
{
    check("direct exact DIRECT/DIRECT",
          systemProxyIsDirect(SystemProxyState{1, 1}));
    check("unknown API state is not direct",
          !systemProxyIsDirect(std::nullopt));
    for (quint32 flags : {0U, 2U, 3U, 4U, 5U, 8U, 9U, 15U, 0x100U})
    {
        check(QString("effective flags %1 cannot imply direct").arg(flags),
              !systemProxyIsDirect(SystemProxyState{flags, 1}));
        check(QString("configured flags %1 cannot imply direct").arg(flags),
              !systemProxyIsDirect(SystemProxyState{1, flags}));
    }

    check("null socket is harmless",
          applyIrcProxyPolicy(nullptr, true, false, SystemProxyState{1, 1}) ==
              IrcProxyRefresh::Unchanged);

    QSslSocket socket;
    check("fresh socket defaults", isDefault(socket));
    check("direct transition selects NoProxy",
          applyIrcProxyPolicy(&socket, true, false, SystemProxyState{1, 1}) ==
                  IrcProxyRefresh::Direct &&
              isOurDirect(socket));
    check("repeated direct transition remains NoProxy",
          applyIrcProxyPolicy(&socket, true, false, SystemProxyState{1, 1}) ==
                  IrcProxyRefresh::Direct &&
              isOurDirect(socket));
    check("manual proxy reappearance restores default",
          applyIrcProxyPolicy(&socket, true, false, SystemProxyState{3, 3}) ==
                  IrcProxyRefresh::SystemDefault &&
              isDefault(socket));

    for (const auto &state : std::vector<std::optional<SystemProxyState>>{
             std::nullopt, SystemProxyState{5, 5}, SystemProxyState{9, 9},
             SystemProxyState{1, 3}, SystemProxyState{3, 1},
             SystemProxyState{0, 0}, SystemProxyState{0x101, 0x101}})
    {
        applyIrcProxyPolicy(&socket, true, false, SystemProxyState{1, 1});
        const QString label = state ? QString("%1/%2")
                                          .arg(state->effectiveFlags)
                                          .arg(state->configuredFlags)
                                    : QStringLiteral("API unknown");
        check("return from own direct respects " + label,
              applyIrcProxyPolicy(&socket, true, false, state) ==
                      IrcProxyRefresh::SystemDefault &&
                  isDefault(socket));
    }

    for (bool explicitOverride : {false, true})
    {
        QSslSocket configured;
        const bool systemManaged = !explicitOverride;
        // Either a global application proxy or an environment override must win.
        const bool managedArgument = explicitOverride ? true : false;
        Q_UNUSED(systemManaged);
        check(explicitOverride ? "environment override preserves default"
                               : "non-system global policy preserves default",
              applyIrcProxyPolicy(&configured, managedArgument,
                                  explicitOverride, SystemProxyState{1, 1}) ==
                      IrcProxyRefresh::Unchanged &&
                  isDefault(configured));
        applyIrcProxyPolicy(&configured, true, false, SystemProxyState{1, 1});
        check(explicitOverride
                  ? "environment override releases own NoProxy"
                  : "non-system global policy releases own NoProxy",
              applyIrcProxyPolicy(&configured, managedArgument,
                                  explicitOverride, SystemProxyState{1, 1}) ==
                      IrcProxyRefresh::SystemDefault &&
                  isDefault(configured));
    }

    for (const auto type : {QNetworkProxy::Socks5Proxy,
                            QNetworkProxy::HttpProxy, QNetworkProxy::NoProxy})
    {
        QSslSocket configured;
        configured.setProxy(QNetworkProxy(
            type, "127.0.0.1", 12345, "synthetic-user", "synthetic-password"));
        const auto before = configured.proxy();
        check(QString("explicit socket proxy type %1 is preserved")
                  .arg(int(type)),
              applyIrcProxyPolicy(&configured, true, false,
                                  SystemProxyState{1, 1}) ==
                      IrcProxyRefresh::Unchanged &&
                  configured.proxy() == before);
        configured.setProperty("leafyrinoLiveSystemProxy", true);
        if (type != QNetworkProxy::NoProxy)
            check(QString("foreign explicit proxy %1 wins stale marker")
                      .arg(int(type)),
                  applyIrcProxyPolicy(&configured, true, false,
                                      SystemProxyState{1, 1}) ==
                          IrcProxyRefresh::Unchanged &&
                      configured.proxy() == before);
    }
}

class OwnedProxyFactory final : public QNetworkProxyFactory
{
public:
    explicit OwnedProxyFactory(quint16 port)
        : port_(port)
    {
    }
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &) override
    {
        ++calls;
        return {QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", port_)};
    }
    int calls = 0;

private:
    quint16 port_;
};

void networkTests()
{
    QHostAddress localAddress;
    for (const auto &iface : QNetworkInterface::allInterfaces())
    {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const auto &entry : iface.addressEntries())
        {
            const auto ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol &&
                !ip.isLoopback() && !ip.isNull())
            {
                localAddress = ip;
                break;
            }
        }
        if (!localAddress.isNull())
            break;
    }
    check("owned non-loopback local address exists", !localAddress.isNull());
    if (localAddress.isNull())
        return;

    QTcpServer target;
    QTcpServer proxy;
    check("owned target listens", target.listen(localAddress, 0));
    check("owned CONNECT proxy listens",
          proxy.listen(QHostAddress::LocalHost, 0));
    if (!target.isListening() || !proxy.isListening())
        return;

    int targetConnections = 0;
    int proxyConnections = 0;
    QStringList connectRequests;
    QObject::connect(&target, &QTcpServer::newConnection, &target, [&] {
        while (target.hasPendingConnections())
        {
            QTcpSocket *accepted = target.nextPendingConnection();
            ++targetConnections;
            QObject::connect(
                accepted, &QTcpSocket::readyRead, accepted, [accepted] {
                    accepted->write("OWNED-TARGET:" + accepted->readAll());
                });
        }
    });
    QObject::connect(&proxy, &QTcpServer::newConnection, &proxy, [&] {
        while (proxy.hasPendingConnections())
        {
            QTcpSocket *accepted = proxy.nextPendingConnection();
            ++proxyConnections;
            auto request = std::make_shared<QByteArray>();
            auto forwarded = std::make_shared<bool>(false);
            auto *upstream = new QTcpSocket(accepted);
            upstream->setProxy(QNetworkProxy::NoProxy);
            QObject::connect(upstream, &QTcpSocket::readyRead, accepted,
                             [accepted, upstream] {
                                 accepted->write(upstream->readAll());
                             });
            QObject::connect(
                upstream, &QTcpSocket::connected, accepted, [accepted] {
                    accepted->write(
                        "HTTP/1.1 200 Connection established\r\n\r\n");
                });
            QObject::connect(accepted, &QTcpSocket::disconnected, upstream,
                             [upstream] {
                                 upstream->abort();
                             });
            QObject::connect(accepted, &QTcpSocket::readyRead, accepted,
                             [&, accepted, upstream, request, forwarded] {
                                 if (*forwarded)
                                 {
                                     upstream->write(accepted->readAll());
                                     return;
                                 }
                                 *request += accepted->readAll();
                                 const auto end = request->indexOf("\r\n\r\n");
                                 if (end < 0)
                                     return;
                                 connectRequests.append(QString::fromLatin1(
                                     request->left(request->indexOf("\r\n"))));
                                 *forwarded = true;
                                 // Destination is fixed to our own listening socket, never parsed from user input.
                                 upstream->connectToHost(localAddress,
                                                         target.serverPort());
                             });
        }
    });

    auto *factory = new OwnedProxyFactory(proxy.serverPort());
    QNetworkProxyFactory::setApplicationProxyFactory(factory);
    check("test factory disables system managed flag in this process",
          !QNetworkProxyFactory::usesSystemConfiguration());
    {
        QSslSocket processPolicy;
        check("public refresh respects process-owned custom factory",
              chatterino::refreshIrcSystemProxy(&processPolicy, false) ==
                      IrcProxyRefresh::Unchanged &&
                  isDefault(processPolicy));
    }

    QSslSocket client;
    for (int phase = 0; phase < 3; ++phase)
    {
        const bool direct = phase == 1;
        const auto result = applyIrcProxyPolicy(
            &client, true, false,
            direct ? SystemProxyState{1, 1} : SystemProxyState{3, 3});
        check(QString("same QSslSocket phase %1 policy").arg(phase),
              result == (direct ? IrcProxyRefresh::Direct
                                : IrcProxyRefresh::SystemDefault));
        const int proxyBefore = proxyConnections;
        const int targetBefore = targetConnections;
        client.connectToHost(localAddress, target.serverPort());
        const bool connected = spinUntil([&] {
            return client.state() == QAbstractSocket::ConnectedState;
        });
        check(QString("same QSslSocket phase %1 connects").arg(phase),
              connected, connected ? QString() : client.errorString());
        if (connected)
        {
            const auto proxyType = client.proxy().type();
            check(QString("busy QSslSocket phase %1 policy is untouched")
                      .arg(phase),
                  applyIrcProxyPolicy(&client, true, false,
                                      direct ? SystemProxyState{3, 3}
                                             : SystemProxyState{1, 1}) ==
                          IrcProxyRefresh::Unchanged &&
                      client.proxy().type() == proxyType);
            const QByteArray marker = "phase-" + QByteArray::number(phase);
            client.write(marker);
            QByteArray reply;
            const bool echoed = spinUntil([&] {
                reply += client.readAll();
                return reply == "OWNED-TARGET:" + marker;
            });
            check(
                QString("same QSslSocket phase %1 actual owned data round trip")
                    .arg(phase),
                echoed);
            check(QString("same QSslSocket phase %1 reaches owned target")
                      .arg(phase),
                  targetConnections == targetBefore + 1);
            check(QString("same QSslSocket phase %1 expected proxy use")
                      .arg(phase),
                  proxyConnections == proxyBefore + (direct ? 0 : 1));
        }
        client.abort();
        spinUntil(
            [&] {
                return client.state() == QAbstractSocket::UnconnectedState;
            },
            200);
    }
    check("CONNECT factory observed both proxied connections",
          factory->calls >= 2);
    check("exactly two CONNECT requests across ON-OFF-ON",
          connectRequests.size() == 2);
    for (int n = 0; n < connectRequests.size(); ++n)
        check(QString("CONNECT request %1 points only to owned local server")
                  .arg(n),
              connectRequests[n].startsWith(QString("CONNECT %1:%2 ")
                                                .arg(localAddress.toString())
                                                .arg(target.serverPort())));

    // IrcConnection keeps independent read and write sockets. Exercise both
    // concurrently so refreshing one cannot leave the other on a stale proxy.
    QSslSocket readSocket;
    QSslSocket writeSocket;
    for (int phase = 0; phase < 3; ++phase)
    {
        const bool direct = phase == 1;
        const int proxyBefore = proxyConnections;
        for (auto *channelSocket : {&readSocket, &writeSocket})
        {
            applyIrcProxyPolicy(
                channelSocket, true, false,
                direct ? SystemProxyState{1, 1} : SystemProxyState{3, 3});
            channelSocket->connectToHost(localAddress, target.serverPort());
            const auto proxyType = channelSocket->proxy().type();
            check(QString("read/write phase %1 connecting socket unchanged %2")
                      .arg(phase)
                      .arg(channelSocket == &readSocket ? "read" : "write"),
                  applyIrcProxyPolicy(channelSocket, true, false,
                                      direct ? SystemProxyState{3, 3}
                                             : SystemProxyState{1, 1}) ==
                          IrcProxyRefresh::Unchanged &&
                      channelSocket->proxy().type() == proxyType);
        }
        const bool bothConnected = spinUntil([&] {
            return readSocket.state() == QAbstractSocket::ConnectedState &&
                   writeSocket.state() == QAbstractSocket::ConnectedState;
        });
        check(QString("read/write phase %1 both connect").arg(phase),
              bothConnected);
        if (bothConnected)
        {
            readSocket.write("read");
            writeSocket.write("write");
            QByteArray readReply;
            QByteArray writeReply;
            check(QString("read/write phase %1 both exchange owned data")
                      .arg(phase),
                  spinUntil([&] {
                      readReply += readSocket.readAll();
                      writeReply += writeSocket.readAll();
                      return readReply == "OWNED-TARGET:read" &&
                             writeReply == "OWNED-TARGET:write";
                  }));
            check(QString("read/write phase %1 expected proxy use").arg(phase),
                  proxyConnections == proxyBefore + (direct ? 0 : 2));
        }
        readSocket.abort();
        writeSocket.abort();
        spinUntil(
            [&] {
                return readSocket.state() ==
                           QAbstractSocket::UnconnectedState &&
                       writeSocket.state() == QAbstractSocket::UnconnectedState;
            },
            200);
    }
    QNetworkProxyFactory::setApplicationProxyFactory(nullptr);
}
}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QElapsedTimer elapsed;
    elapsed.start();
    policyTests();
    networkTests();
    const auto native = chatterino::currentSystemProxyState();
    QJsonObject nativeState{{"available", bool(native)}};
    if (native)
    {
        nativeState.insert("effective_flags", int(native->effectiveFlags));
        nativeState.insert("configured_flags", int(native->configuredFlags));
    }
    QJsonObject report{
        {"test_count", results.size()},
        {"passed", results.size() - failures},
        {"failed", failures},
        {"elapsed_ms", elapsed.elapsed()},
        {"scope", "Actual IrcProxy.cpp; controlled process-only proxy factory; "
                  "owned local HTTP CONNECT and TCP servers; no production "
                  "endpoints, profile or proxy writes"},
        {"native_proxy_flags_read_only", nativeState},
        {"tests", results}};
    const auto outputPath = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("irc-proxy-results.json");
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly))
        return 2;
    file.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    return failures == 0 ? 0 : 1;
}
