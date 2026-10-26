// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

// Standalone regression harness linked against chatterino-lib. No Application,
// account, profile, external endpoint, Windows proxy write, or GUI is needed.
// Pass an absolute JSON output path below D:/devtools/ as the only argument.

#include "common/network/NetworkManager.hpp"
#include "common/network/NetworkPrivate.hpp"
#include "common/network/NetworkProxy.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/network/NetworkTask.hpp"
#include "providers/irc/IrcProxy.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSslCertificate>
#include <QSslError>
#include <QSslKey>
#include <QSslSocket>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using chatterino::HttpProxyFactory;
using chatterino::HttpProxySnapshot;
using chatterino::NetworkData;
using chatterino::NetworkManager;
using chatterino::NetworkResult;
using chatterino::detail::SystemProxyState;
using chatterino::network::detail::NetworkTask;

namespace {

QJsonArray results;
QJsonArray observations;
int failures = 0;

void check(const QString &name, bool passed, const QJsonObject &evidence = {})
{
    QJsonObject row{{"name", name}, {"passed", passed}};
    if (!evidence.isEmpty())
    {
        row.insert("evidence", evidence);
    }
    results.append(row);
    if (!passed)
    {
        ++failures;
    }
}

bool spinUntil(const std::function<bool()> &done, int timeoutMs = 2500)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!done() && elapsed.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        // This standalone harness pumps events without app.exec(); explicitly
        // deliver deleteLater() events while predicates observe QPointers.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return done();
}

void drainEvents(int durationMs = 120)
{
    QElapsedTimer elapsed;
    elapsed.start();
    spinUntil([&] { return elapsed.elapsed() >= durationMs; }, durationMs + 50);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QHostAddress ownedLocalAddress()
{
    // Qt deliberately bypasses some proxies for loopback destinations. This
    // address still belongs to this computer, and every listener is ours.
    for (const auto &iface : QNetworkInterface::allInterfaces())
    {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            (iface.flags() & QNetworkInterface::IsLoopBack))
        {
            continue;
        }
        for (const auto &entry : iface.addressEntries())
        {
            const auto address = entry.ip();
            if (address.protocol() == QAbstractSocket::IPv4Protocol &&
                !address.isLoopback() && !address.isNull())
            {
                return address;
            }
        }
    }
    return {};
}

QUrl localUrl(const char *scheme, const QHostAddress &address, quint16 port,
              const QString &path)
{
    QUrl url;
    url.setScheme(QString::fromLatin1(scheme));
    url.setHost(address.toString());
    url.setPort(port);
    url.setPath(path);
    return url;
}

void proxyPolicyTests()
{
    HttpProxySnapshot snapshot{true, false, SystemProxyState{1, 1}};
    int reads = 0;
    int fallbacks = 0;
    const QList<QNetworkProxy> fallbackList{
        QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 23456,
                      "fixture-user", "fixture-password"),
        QNetworkProxy(QNetworkProxy::NoProxy)};
    const QUrl target("https://192.0.2.1/never-requested-policy-query");
    const QNetworkProxyQuery query(target);
    QUrl fallbackQuery;
    HttpProxyFactory factory(
        [&] {
            ++reads;
            return snapshot;
        },
        [&](const QNetworkProxyQuery &received) {
            ++fallbacks;
            fallbackQuery = received.url();
            return fallbackList;
        });

    const auto direct = factory.queryProxy(query);
    check("HTTP exact DIRECT/DIRECT avoids stale Qt fallback",
          direct == QList<QNetworkProxy>{QNetworkProxy(QNetworkProxy::NoProxy)} &&
              reads == 1 && fallbacks == 0);

    const std::vector<std::pair<QString, HttpProxySnapshot>> preserved{
        {"custom application proxy factory",
         {false, false, SystemProxyState{1, 1}}},
        {"explicit environment proxy", {true, true, SystemProxyState{1, 1}}},
        {"unknown system API result", {true, false, std::nullopt}},
        {"manual proxy", {true, false, SystemProxyState{3, 3}}},
        {"PAC configured", {true, false, SystemProxyState{5, 5}}},
        {"WPAD configured", {true, false, SystemProxyState{9, 9}}},
        {"effective DIRECT with configured manual proxy",
         {true, false, SystemProxyState{1, 3}}},
        {"configured DIRECT with effective manual proxy",
         {true, false, SystemProxyState{3, 1}}},
        {"unknown flag bits", {true, false, SystemProxyState{0x101, 0x101}}},
        {"empty flags", {true, false, SystemProxyState{0, 0}}},
    };
    for (const auto &[name, state] : preserved)
    {
        snapshot = state;
        const auto before = fallbacks;
        check("HTTP policy preserves " + name,
              factory.queryProxy(query) == fallbackList &&
                  fallbacks == before + 1 && fallbackQuery == target);
    }
    for (quint32 flag : {0U, 2U, 3U, 4U, 5U, 8U, 9U, 15U, 0x100U})
    {
        for (bool configured : {false, true})
        {
            snapshot = {true, false,
                        configured ? SystemProxyState{1, flag}
                                   : SystemProxyState{flag, 1}};
            const auto before = fallbacks;
            check(QString("HTTP nonexact direct flag %1 in %2 uses fallback")
                      .arg(flag)
                      .arg(configured ? "configured" : "effective"),
                  factory.queryProxy(query) == fallbackList &&
                      fallbacks == before + 1);
        }
    }
    snapshot = {true, false, SystemProxyState{1, 1}};
    const auto before = fallbacks;
    check("HTTP query re-reads live snapshot after proxy returns to DIRECT",
          factory.queryProxy(query) ==
                  QList<QNetworkProxy>{QNetworkProxy(QNetworkProxy::NoProxy)} &&
              fallbacks == before);
}

void deadlinePolicyTests()
{
    NetworkData finiteGet;
    finiteGet.startDeadline();
    check("GET without explicit timeout receives finite default deadline",
          finiteGet.deadline && finiteGet.deadline->remainingTime() > 29000 &&
              finiteGet.deadline->remainingTime() <= 30000);
    if (finiteGet.deadline)
    {
        const auto initial = *finiteGet.deadline;
        finiteGet.startDeadline();
        check("starting an existing deadline does not extend it",
              finiteGet.deadline && *finiteGet.deadline == initial);
    }
    for (const auto type : {chatterino::NetworkRequestType::Post,
                           chatterino::NetworkRequestType::Put,
                           chatterino::NetworkRequestType::Patch,
                           chatterino::NetworkRequestType::Delete})
    {
        NetworkData unboundedWrite;
        unboundedWrite.requestType = type;
        unboundedWrite.startDeadline();
        check(QString("write method %1 keeps its unspecified timeout")
                  .arg(int(type)),
              !unboundedWrite.deadline);
        NetworkData boundedWrite;
        boundedWrite.requestType = type;
        boundedWrite.timeout = std::chrono::milliseconds(500);
        boundedWrite.startDeadline();
        check(QString("write method %1 honors an explicit timeout").arg(int(type)),
              boundedWrite.deadline &&
                  boundedWrite.deadline->remainingTime() > 0 &&
                  boundedWrite.deadline->remainingTime() <= 500);
    }
}

// Public, throwaway fixture identity. It authenticates no real service. Only
// ObservedManager below ignores this certificate's validation errors.
constexpr char fixtureKey[] = R"PEM(-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQCc2QMKfOlVo7pM
ctfsJ2RWCZcXUe6JfnY4AZTh+1F+pLPXH6ZB07lC5Wo9d5zA1eAIILlsGi6TSE3p
WCfLlUmRVlsbTD1PvBy65IBywdZpGZBp4Txhgci1geWJgQUk1g0GH7vKPXL4MJ6Z
cGFGbBpiQBQJSO9b0EzLbQOOR9bENmI38Mp6JcyCbHF2SSw24s2Qzm/BW+s55a0r
uu+DF2E0ur8AWUmhwtlirk4PweOn0SAp1v77b/xJbYXptCgIaWy3B7xLeCqYbxQP
8NraalDr+t5ejiUT0RthC6n5ew4i4TWSRgkxW6xk7adN/9JKGXgcE3u6owpSHx6r
xaROXHMzAgMBAAECggEADeYfIl1KUgf1uNOOdcNR3VvxMyRUBoQZ1sGxH0tlf9r5
gs0vrvKjnp10yiigz+fD+ARqWf2B9hQ4rz5cHGrIv6PO4/7gCgJJ63fXOBQcO2kE
BpXqg1t8XTU7EXHI8mgzdED4o6XJW9v3GfK+8/4dYmp6PA5mA5VUUMyCvad5DR7/
FdeJrQPXHquIOLTiDuB5mTCFi3LcLcPsPsbriUKnJKs4IyzZDoZ9pACCZhqpFaGO
dlElBAyfknb80pzRIPMtjq2eg3lipLd3fA2q9wewNOJMY5/IDO7CMC2Doe95QLjr
KUzCCF+QvjoRURwYnGILu1+KV044J0AY9RZuoaekOQKBgQDK9oNQ4ED6dPXO+iK1
Gdo5RWwFcSjckncMw6Z3c+50xfDbP/DOnWxCPcCBryrGQVSnSR2WY4WpOdzNA07m
3+RW/NEkpNV8vtFyRiEcCCmCpYkMpINO2lHCQSjwijE8uzMZ+/Mbb+mPbXIjisk/
oCiV0OVQDqm1sF4XCbgh581naQKBgQDF1Y5hcUXFRMc90M2WIorLoX3B1leHUe7E
9n+HGtrh7WUmtxXtw+PL3YiBNt11rnx301GmQQw3o3nrdNY+z2s1hB5AUo44afnX
JbxODbYpKres7C1HQr2mtbUydgzS34Gt53sBdDvShfA3j5OCAI2WK2LQ2Y0ML6lZ
ZR2iIpLuOwKBgC08mnIZOYJZNTxHlhrFQ1VGetgPq6rV3kDzDpRLO5e9UnybmMGE
Zs+w0cpVz3PWtdZ/Pl/5mJ3qLqNnHFvrBQq6WKcRMiAWq7oLlW+ZBh9/Y7d0p2uY
z2Y7fbN3KZg+pRoihMHVxLnaC/mOAmLzAHi70ZBxZDQBtkbuPKMZ9jcZAoGBAISV
9VBU69+jkqxZDg96ou6KEpAgKJUmUhFq6lkPpHYZ9dqWhXnN+RMeK2f97zn5cK2V
0HjNy3RWeVVV/mgl9sHtA6E0MuCYFbsdLTBafCTm3iZkOowxrIzX5tIkz79bz7tP
Ed8NmTIkVbPG46MS39EuzpEN3Du4jZS98bhB15sVAoGBAL8h5bx3SxzL77vk5ft0
Y9Zm8a/YmGjPluPgresOc2zJYenSPReiBLYBPhCNTodyJD1rJ5xUyzFYxvfnx0zC
PKwhzHQNsqF/dH0x/zHRi++EJPDfGsPAuJPxQOGm/+ihJGKpLE0lflLv7sN9k8Zc
HSgYznnfZJQOydpfnUp5b79P
-----END PRIVATE KEY-----
)PEM";

constexpr char fixtureCertificate[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIDPzCCAiegAwIBAgIURkIlArua4VTP1VoDM8CNNhmg+RgwDQYJKoZIhvcNAQEL
BQAwLjEsMCoGA1UEAwwjbmV0d29yay1yZWNvdmVyeS1vd25lZC10ZXN0LmludmFs
aWQwIBcNMjYxMDIxMTkyNzM3WhgPMjEyNjA5MjcxOTI3MzdaMC4xLDAqBgNVBAMM
I25ldHdvcmstcmVjb3Zlcnktb3duZWQtdGVzdC5pbnZhbGlkMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAnNkDCnzpVaO6THLX7CdkVgmXF1HuiX52OAGU
4ftRfqSz1x+mQdO5QuVqPXecwNXgCCC5bBouk0hN6Vgny5VJkVZbG0w9T7wcuuSA
csHWaRmQaeE8YYHItYHliYEFJNYNBh+7yj1y+DCemXBhRmwaYkAUCUjvW9BMy20D
jkfWxDZiN/DKeiXMgmxxdkksNuLNkM5vwVvrOeWtK7rvgxdhNLq/AFlJocLZYq5O
D8Hjp9EgKdb++2/8SW2F6bQoCGlstwe8S3gqmG8UD/Da2mpQ6/reXo4lE9EbYQup
+XsOIuE1kkYJMVusZO2nTf/SShl4HBN7uqMKUh8eq8WkTlxzMwIDAQABo1MwUTAd
BgNVHQ4EFgQUpPiHvHavleOz3yxFoHIO0YlZJakwHwYDVR0jBBgwFoAUpPiHvHav
leOz3yxFoHIO0YlZJakwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOC
AQEATDIzPetYb9YbgNturdMvDQQDaRKMCUdMo6Rb8CbBCGkJR8/y4vsAKgPbysv+
r4ecG33ovz+6bcv79HZ4XKnB1GX9QuWdpVGsPYNIOMT9fADAaIsIxeAKNhq0D7f2
uARI7TfP22PTG2vTFS/o9cFF3tudGBOMSBei+0mZIxsaNRgyx1ZO6DIm4J0+Gy1S
RYFOqucWTsk96gjYFfVdAyP4Wnq1qMDz3R4+Sf+j5U2uOiFttAgj9P368v7eihj7
n7mV4Ny6GotNbZ+pVnFtKxuR7Q5JgpNVR0MRVxExMxy//MoNOhiy7ZmieIkn0QEy
6hvBcOkIi1YUKRyhZYzbq7HZ9A==
-----END CERTIFICATE-----
)PEM";

class OwnedTlsHttpServer final : public QTcpServer
{
public:
    OwnedTlsHttpServer()
        : certificate_(QByteArray(fixtureCertificate))
        , key_(QByteArray(fixtureKey), QSsl::Rsa)
    {
        this->setProxy(QNetworkProxy::NoProxy);
    }

    bool identityValid() const
    {
        return !this->certificate_.isNull() && !this->key_.isNull();
    }

    int connections = 0;
    int requests = 0;
    QStringList paths;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(descriptor))
        {
            socket->deleteLater();
            return;
        }
        ++this->connections;
        socket->setLocalCertificate(this->certificate_);
        socket->setPrivateKey(this->key_);
        const auto request = std::make_shared<QByteArray>();
        const auto answered = std::make_shared<bool>(false);
        QObject::connect(socket, &QSslSocket::disconnected, socket,
                         &QObject::deleteLater);
        QObject::connect(socket, &QSslSocket::readyRead, socket,
                         [this, socket, request, answered] {
                             *request += socket->readAll();
                             if (*answered || !request->contains("\r\n\r\n"))
                             {
                                 return;
                             }
                             *answered = true;
                             ++this->requests;
                             const auto line = request->left(request->indexOf("\r\n"));
                             const auto parts = line.split(' ');
                             const auto path = parts.size() >= 2 ? parts[1] : QByteArray();
                             this->paths.append(QString::fromLatin1(path));
                             const QByteArray body = "OWNED-HTTP:" + path;
                             socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\n"
                                           "Content-Length: " +
                                           QByteArray::number(body.size()) + "\r\n\r\n" + body);
                             socket->disconnectFromHost();
                         });
        socket->startServerEncryption();
    }

private:
    QSslCertificate certificate_;
    QSslKey key_;
};

class OwnedConnectProxy final : public QTcpServer
{
public:
    OwnedConnectProxy(QHostAddress address, quint16 port)
        : address_(std::move(address))
        , port_(port)
    {
        this->setProxy(QNetworkProxy::NoProxy);
        QObject::connect(this, &QTcpServer::newConnection, this, [this] {
            while (this->hasPendingConnections())
            {
                this->acceptClient(this->nextPendingConnection());
            }
        });
    }

    int connections = 0;
    int tunnels = 0;
    int refused = 0;
    QStringList connectLines;

private:
    void acceptClient(QTcpSocket *client)
    {
        ++this->connections;
        auto *upstream = new QTcpSocket(client);
        upstream->setProxy(QNetworkProxy::NoProxy);
        const auto header = std::make_shared<QByteArray>();
        const auto tunnel = std::make_shared<bool>(false);
        QObject::connect(client, &QTcpSocket::disconnected, client,
                         [client, upstream] {
                             upstream->abort();
                             client->deleteLater();
                         });
        QObject::connect(upstream, &QTcpSocket::readyRead, client,
                         [client, upstream] { client->write(upstream->readAll()); });
        QObject::connect(upstream, &QTcpSocket::connected, client,
                         [client, upstream, header] {
                             client->write("HTTP/1.1 200 Connection established\r\n\r\n");
                             if (!header->isEmpty())
                             {
                                 upstream->write(*header);
                                 header->clear();
                             }
                         });
        QObject::connect(upstream, &QTcpSocket::disconnected, client,
                         &QTcpSocket::disconnectFromHost);
        QObject::connect(client, &QTcpSocket::readyRead, client,
                         [this, client, upstream, header, tunnel] {
                             if (*tunnel)
                             {
                                 upstream->write(client->readAll());
                                 return;
                             }
                             *header += client->readAll();
                             const auto end = header->indexOf("\r\n\r\n");
                             if (end < 0)
                             {
                                 return;
                             }
                             const auto firstLine = header->left(header->indexOf("\r\n"));
                             this->connectLines.append(QString::fromLatin1(firstLine));
                             const auto expected = "CONNECT " + this->address_.toString().toLatin1() +
                                                   ':' + QByteArray::number(this->port_) + ' ';
                             if (!firstLine.startsWith(expected))
                             {
                                 ++this->refused;
                                 client->abort();
                                 return;
                             }
                             ++this->tunnels;
                             *tunnel = true;
                             header->remove(0, end + 4);
                             // Never resolve or connect to the requested host. The
                             // sole upstream is our already-listening local server.
                             upstream->connectToHost(this->address_, this->port_);
                         });
    }

    QHostAddress address_;
    quint16 port_;
};

class ObservedManager final : public QNetworkAccessManager
{
public:
    ObservedManager()
    {
        QObject::connect(this, &QNetworkAccessManager::sslErrors, this,
                         [](QNetworkReply *reply, const QList<QSslError> &) {
                             reply->ignoreSslErrors();
                         });
    }

    QPointer<QNetworkReply> last;
    int created = 0;
    int sent = 0;
    int encrypted = 0;

protected:
    QNetworkReply *createRequest(Operation operation,
                                 const QNetworkRequest &request,
                                 QIODevice *body = nullptr) override
    {
        auto *reply = QNetworkAccessManager::createRequest(operation, request, body);
        ++this->created;
        this->last = reply;
        QObject::connect(reply, &QNetworkReply::requestSent, this,
                         [this] { ++this->sent; });
        QObject::connect(reply, &QNetworkReply::encrypted, this,
                         [this] { ++this->encrypted; });
        return reply;
    }
};

void persistentManagerTests(const QHostAddress &address)
{
    OwnedTlsHttpServer target;
    check("owned TLS test identity parses", target.identityValid());
    check("owned TLS HTTP server listens", target.listen(address, 0));
    if (!target.isListening() || !target.identityValid())
    {
        return;
    }
    OwnedConnectProxy proxy(address, target.serverPort());
    check("owned HTTP CONNECT proxy listens",
          proxy.listen(QHostAddress::LocalHost, 0));
    if (!proxy.isListening())
    {
        return;
    }

    HttpProxySnapshot snapshot{true, false, SystemProxyState{3, 3}};
    int reads = 0;
    int fallbacks = 0;
    ObservedManager manager;
    manager.setProxyFactory(new HttpProxyFactory(
        [&] {
            ++reads;
            return snapshot;
        },
        [&](const QNetworkProxyQuery &) {
            ++fallbacks;
            return QList<QNetworkProxy>{QNetworkProxy(
                QNetworkProxy::HttpProxy, "127.0.0.1", proxy.serverPort())};
        }));
    auto *const originalFactory = manager.proxyFactory();

    for (int phase = 0; phase != 3; ++phase)
    {
        const bool direct = phase == 1;
        snapshot.system = direct ? SystemProxyState{1, 1}
                                 : SystemProxyState{3, 3};
        const auto proxyBefore = proxy.tunnels;
        const auto targetBefore = target.requests;
        const auto readsBefore = reads;
        const auto fallbackBefore = fallbacks;
        const auto path = QString("/same-manager-phase-%1").arg(phase);
        QPointer<QNetworkReply> reply = manager.get(QNetworkRequest(
            localUrl("https", address, target.serverPort(), path)));
        const bool finished = spinUntil([&] { return reply && reply->isFinished(); });
        const auto body = reply ? reply->readAll() : QByteArray();
        const QJsonObject evidence{
            {"phase", phase}, {"direct", direct},
            {"finished", finished}, {"body", QString::fromLatin1(body)},
            {"proxy_tunnels", proxy.tunnels - proxyBefore},
            {"target_requests", target.requests - targetBefore},
            {"snapshot_reads", reads - readsBefore},
            {"fallback_queries", fallbacks - fallbackBefore},
            {"error", reply ? int(reply->error()) : -1}};
        observations.append(evidence);
        check(QString("same QNAM phase %1 returns owned sentinel").arg(phase),
              finished && reply->error() == QNetworkReply::NoError &&
                  body == "OWNED-HTTP:" + path.toLatin1(), evidence);
        check(QString("same QNAM phase %1 uses current proxy mode").arg(phase),
              proxy.tunnels == proxyBefore + (direct ? 0 : 1) &&
                  target.requests == targetBefore + 1 && reads > readsBefore &&
                  (direct ? fallbacks == fallbackBefore
                          : fallbacks > fallbackBefore), evidence);
        check(QString("same QNAM phase %1 keeps factory identity").arg(phase),
              manager.proxyFactory() == originalFactory);
        if (reply)
        {
            if (!reply->isFinished())
            {
                reply->abort();
            }
            reply->deleteLater();
        }
        drainEvents(30);
    }
    check("ON-DIRECT-ON performs exactly two owned CONNECT tunnels",
          proxy.tunnels == 2 && proxy.refused == 0 &&
              proxy.connectLines.size() == 2 && target.requests == 3);
}

enum class ResponseMode {
    Blackhole,
    Stall,
    Trickle,
    Empty,
    HttpError,
};

class OwnedHttpServer final : public QTcpServer
{
public:
    explicit OwnedHttpServer(ResponseMode mode)
        : mode_(mode)
    {
        this->setProxy(QNetworkProxy::NoProxy);
        QObject::connect(this, &QTcpServer::newConnection, this, [this] {
            while (this->hasPendingConnections())
            {
                auto *client = this->nextPendingConnection();
                ++this->connections;
                auto request = std::make_shared<QByteArray>();
                auto answered = std::make_shared<bool>(false);
                QObject::connect(client, &QTcpSocket::disconnected, client,
                                 &QObject::deleteLater);
                QObject::connect(client, &QTcpSocket::readyRead, client,
                                 [this, client, request, answered] {
                                     const auto bytes = client->readAll();
                                     this->received += bytes.size();
                                     *request += bytes;
                                     if (*answered || this->mode_ == ResponseMode::Blackhole ||
                                         !request->contains("\r\n\r\n"))
                                     {
                                         return;
                                     }
                                     *answered = true;
                                     ++this->requests;
                                     if (this->mode_ == ResponseMode::Empty)
                                     {
                                         client->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n"
                                                       "Connection: close\r\n\r\n");
                                         client->disconnectFromHost();
                                     }
                                     else if (this->mode_ == ResponseMode::HttpError)
                                     {
                                         client->write("HTTP/1.1 503 Service Unavailable\r\n"
                                                       "Content-Length: 0\r\nConnection: close\r\n\r\n");
                                         client->disconnectFromHost();
                                     }
                                     else if (this->mode_ == ResponseMode::Trickle)
                                     {
                                         client->write("HTTP/1.1 200 OK\r\nContent-Length: 100000\r\n"
                                                       "Connection: close\r\n\r\nx");
                                         auto *timer = new QTimer(client);
                                         timer->setInterval(50);
                                         QObject::connect(timer, &QTimer::timeout, client,
                                                          [this, client] {
                                                              if (client->state() == QAbstractSocket::ConnectedState)
                                                              {
                                                                  client->write("x");
                                                                  ++this->trickles;
                                                              }
                                                          });
                                         timer->start();
                                     }
                                 });
            }
        });
    }

    int connections = 0;
    int requests = 0;
    int trickles = 0;
    qint64 received = 0;

private:
    ResponseMode mode_;
};

class FixedProxyFactory final : public QNetworkProxyFactory
{
public:
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &) override
    {
        ++this->calls;
        return {QNetworkProxy(QNetworkProxy::NoProxy)};
    }

    int calls = 0;
};

void managerConfigurationTests(const QHostAddress &address)
{
    QNetworkAccessManager fresh;
    chatterino::configureNetworkAccessManager(fresh);
    auto *const installed = fresh.proxyFactory();
    chatterino::configureNetworkAccessManager(fresh);
    check("fresh HTTP manager receives one live factory",
          installed != nullptr && fresh.proxyFactory() == installed);

    QObject parent;
    auto *created = chatterino::createNetworkAccessManager(&parent);
    check("HTTP manager creation configures factory and preserves parent",
          created && created->parent() == &parent &&
              created->proxyFactory() != nullptr);

    for (const auto type : {QNetworkProxy::NoProxy, QNetworkProxy::HttpProxy,
                           QNetworkProxy::Socks5Proxy})
    {
        QNetworkAccessManager explicitManager;
        const QNetworkProxy explicitProxy(type, "127.0.0.1", 23456,
                                          "fixture-user", "fixture-password");
        explicitManager.setProxy(explicitProxy);
        chatterino::configureNetworkAccessManager(explicitManager);
        check(QString("explicit HTTP manager proxy type %1 is preserved")
                  .arg(int(type)),
              explicitManager.proxy() == explicitProxy &&
                  explicitManager.proxyFactory() == nullptr);
    }

    OwnedHttpServer target(ResponseMode::Stall);
    check("busy manager owned listener starts", target.listen(address, 0));
    if (!target.isListening())
    {
        return;
    }
    // Exercise an actual active request both with an explicit manager proxy
    // and a foreign factory. Configuration must not replace either or abort it.
    for (const bool useFactory : {false, true})
    {
        ObservedManager manager;
        FixedProxyFactory *factory = nullptr;
        if (useFactory)
        {
            factory = new FixedProxyFactory;
            manager.setProxyFactory(factory);
        }
        else
        {
            manager.setProxy(QNetworkProxy::NoProxy);
        }
        const auto before = target.requests;
        QPointer<QNetworkReply> reply = manager.get(QNetworkRequest(localUrl(
            "http", address, target.serverPort(), "/busy-manager")));
        const bool active = spinUntil([&] {
            return target.requests > before && reply && reply->isRunning();
        });
        chatterino::configureNetworkAccessManager(manager);
        drainEvents(30);
        check(useFactory ? "busy custom HTTP factory and request are preserved"
                         : "busy explicit HTTP proxy and request are preserved",
              active && reply && reply->isRunning() &&
                  reply->error() == QNetworkReply::NoError &&
                  (useFactory
                       ? manager.proxyFactory() == factory && factory->calls > 0
                       : manager.proxyFactory() == nullptr &&
                             manager.proxy().type() == QNetworkProxy::NoProxy));
        if (reply)
        {
            reply->abort();
            reply->deleteLater();
        }
        drainEvents(10);
    }
}

void rawReplyDeadlineTests(const QHostAddress &address)
{
    chatterino::setNetworkReplyTimeout(nullptr, 1);
    for (const auto mode : {ResponseMode::Blackhole, ResponseMode::Trickle,
                            ResponseMode::Empty})
    {
        const bool success = mode == ResponseMode::Empty;
        const QString name = mode == ResponseMode::Blackhole
                                 ? "finite raw GET pre-send deadline"
                             : mode == ResponseMode::Trickle
                                 ? "finite raw GET continuous-response deadline"
                                 : "finite raw GET early success";
        OwnedHttpServer target(mode);
        check(name + " owned listener starts", target.listen(address, 0));
        if (!target.isListening())
        {
            continue;
        }
        ObservedManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        QElapsedTimer elapsed;
        elapsed.start();
        QPointer<QNetworkReply> reply = manager.get(QNetworkRequest(localUrl(
            mode == ResponseMode::Blackhole ? "https" : "http", address,
            target.serverPort(), "/finite-raw-get")));
        int finishedCount = 0;
        qint64 finishedAt = -1;
        QObject::connect(reply, &QNetworkReply::finished, &manager, [&] {
            ++finishedCount;
            finishedAt = elapsed.elapsed();
        });
        chatterino::setNetworkReplyTimeout(reply, 200);
        const bool finished = spinUntil([&] { return finishedCount > 0; }, 1500);
        // Keep a successful reply alive across its former deadline, then call
        // the helper again on the finished reply to cover the harmless no-op.
        if (success && reply)
        {
            chatterino::setNetworkReplyTimeout(reply, 1);
        }
        drainEvents(250);
        const QJsonObject evidence{
            {"case", name}, {"finished_count", finishedCount},
            {"finished_at_ms", finishedAt}, {"request_sent", manager.sent},
            {"server_connections", target.connections},
            {"response_trickles", target.trickles},
            {"error", reply ? int(reply->error()) : -1}};
        observations.append(evidence);
        check(name + " settles once", finished && finishedCount == 1 && reply &&
                  !reply->isRunning() &&
                  reply->error() == (success ? QNetworkReply::NoError
                                            : QNetworkReply::OperationCanceledError),
              evidence);
        if (!success)
        {
            check(name + " stays within absolute bound",
                  finishedAt >= 120 && finishedAt < 1400, evidence);
            check(name + " reaches owned socket in expected phase",
                  target.connections >= 1 &&
                      (mode == ResponseMode::Blackhole
                           ? manager.sent == 0 && target.received > 0
                           : manager.sent >= 1 && target.trickles >= 2),
                  evidence);
        }
        if (reply)
        {
            if (reply->isRunning())
            {
                reply->abort();
            }
            reply->deleteLater();
        }
        drainEvents(10);
    }
}

struct Callbacks {
    int successes = 0;
    int errors = 0;
    int finals = 0;
    int errorCode = -1;
    int status = -1;
    qint64 firstAtMs = -1;
    QByteArray body;
};

struct TaskCase {
    QString name;
    ResponseMode mode;
    int timeoutMs;
    int abortMs = 0;
    bool queuedExpired = false;
    bool callerDestroyed = false;
};

void taskCase(const QHostAddress &address, const TaskCase &test)
{
    OwnedHttpServer target(test.mode);
    check(test.name + " owned listener starts", target.listen(address, 0));
    if (!target.isListening())
    {
        return;
    }
    ObservedManager manager;
    manager.setProxy(QNetworkProxy::NoProxy);
    auto *const previousManager = NetworkManager::accessManager;
    NetworkManager::accessManager = &manager;
    const auto callbacks = std::make_shared<Callbacks>();
    const auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    auto data = std::make_shared<NetworkData>();
    data->request.setUrl(localUrl(test.mode == ResponseMode::Blackhole ? "https" : "http",
                                  address, target.serverPort(), "/owned-task"));
    data->timeout = std::chrono::milliseconds(test.timeoutMs);
    data->onSuccess = [callbacks, elapsed](NetworkResult result) {
        ++callbacks->successes;
        callbacks->body = result.getData();
        callbacks->status = result.status().value_or(-1);
        if (callbacks->firstAtMs < 0)
        {
            callbacks->firstAtMs = elapsed->elapsed();
        }
    };
    data->onError = [callbacks, elapsed](NetworkResult result) {
        ++callbacks->errors;
        callbacks->errorCode = int(result.error());
        callbacks->body = result.getData();
        callbacks->status = result.status().value_or(-1);
        if (callbacks->firstAtMs < 0)
        {
            callbacks->firstAtMs = elapsed->elapsed();
        }
    };
    data->finally = [callbacks] { ++callbacks->finals; };
    std::unique_ptr<QObject> caller;
    if (test.callerDestroyed)
    {
        caller = std::make_unique<QObject>();
        data->hasCaller = true;
        data->caller = caller.get();
    }
    data->startDeadline();
    if (test.queuedExpired)
    {
        // Simulate time spent in the worker queue using the same absolute
        // deadline as production, before QNAM has created any reply.
        QThread::msleep(static_cast<unsigned long>(test.timeoutMs + 80));
    }
    QPointer<NetworkTask> task = new NetworkTask(std::move(data));
    task->run();
    if (test.callerDestroyed)
    {
        caller.reset();
    }
    QTimer abortTimer;
    abortTimer.setSingleShot(true);
    if (test.abortMs > 0)
    {
        QObject::connect(&abortTimer, &QTimer::timeout, &manager, [&manager] {
            if (manager.last)
            {
                manager.last->abort();
            }
        });
        abortTimer.start(test.abortMs);
    }
    const bool settled = spinUntil(
        [&] { return test.callerDestroyed ? task.isNull() : callbacks->finals != 0; },
        test.timeoutMs + 1800);
    drainEvents();
    const QJsonObject evidence{
        {"case", test.name}, {"timeout_ms", test.timeoutMs},
        {"elapsed_ms", elapsed->elapsed()},
        {"first_callback_ms", callbacks->firstAtMs},
        {"settled_before_watchdog", settled},
        {"success_count", callbacks->successes},
        {"error_count", callbacks->errors}, {"finally_count", callbacks->finals},
        {"error_code", callbacks->errorCode}, {"http_status", callbacks->status},
        {"body_size", callbacks->body.size()}, {"request_created", manager.created},
        {"request_sent", manager.sent}, {"tls_encrypted", manager.encrypted},
        {"server_connections", target.connections}, {"server_requests", target.requests},
        {"received_local_bytes", target.received}, {"response_trickles", target.trickles},
        {"task_destroyed", task.isNull()},
        {"reply_running", manager.last && manager.last->isRunning()}};
    observations.append(evidence);

    if (test.callerDestroyed)
    {
        check(test.name + " suppresses callbacks after caller destruction",
              settled && callbacks->successes == 0 && callbacks->errors == 0 &&
                  callbacks->finals == 0 && task.isNull(), evidence);
    }
    else if (test.mode == ResponseMode::Empty)
    {
        check(test.name + " emits success and finally exactly once",
              settled && callbacks->successes == 1 && callbacks->errors == 0 &&
                  callbacks->finals == 1 && callbacks->body.isEmpty() &&
                  callbacks->status == 200, evidence);
    }
    else
    {
        const int expectedError = test.abortMs > 0
                                      ? int(QNetworkReply::OperationCanceledError)
                                  : test.mode == ResponseMode::HttpError
                                      ? int(QNetworkReply::ServiceUnavailableError)
                                      : int(QNetworkReply::TimeoutError);
        check(test.name + " emits error and finally exactly once",
              settled && callbacks->successes == 0 && callbacks->errors == 1 &&
                  callbacks->finals == 1 && callbacks->errorCode == expectedError,
              evidence);
        if (test.abortMs == 0 && test.mode != ResponseMode::HttpError)
        {
            check(test.name + " obeys absolute deadline",
                  callbacks->firstAtMs >= test.timeoutMs - 80 &&
                      callbacks->firstAtMs < test.timeoutMs + 1500,
                  evidence);
        }
    }
    if (test.queuedExpired)
    {
        check(test.name + " starts no late request",
              manager.created == 0 && target.connections == 0 && manager.sent == 0,
              evidence);
    }
    else if (test.mode == ResponseMode::Blackhole)
    {
        check(test.name + " expires during TLS before requestSent",
              target.connections >= 1 && target.received > 0 && manager.sent == 0 &&
                  manager.encrypted == 0, evidence);
    }
    else if (!test.callerDestroyed)
    {
        check(test.name + " reaches owned HTTP server after requestSent",
              manager.sent >= 1 && target.requests >= 1, evidence);
    }
    if (test.mode == ResponseMode::Trickle)
    {
        check(test.name + " times out despite continuous response bytes",
              target.trickles >= 2, evidence);
    }
    check(test.name + " releases task and active reply",
          task.isNull() && (!manager.last || !manager.last->isRunning()), evidence);
    // Cleanup is after observations and assertions; it cannot manufacture a pass.
    if (manager.last && manager.last->isRunning())
    {
        manager.last->abort();
    }
    if (task)
    {
        delete task;
    }
    drainEvents(10);
    NetworkManager::accessManager = previousManager;
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QLoggingCategory::setFilterRules("*.debug=false\n*.info=false\n");
    if (argc != 2)
    {
        return 2;
    }
    const auto outputPath = QDir::cleanPath(
        QDir::fromNativeSeparators(QString::fromLocal8Bit(argv[1])));
    if (!QFileInfo(outputPath).isAbsolute() ||
        !outputPath.startsWith("D:/devtools/", Qt::CaseInsensitive))
    {
        return 3;
    }
    QElapsedTimer elapsed;
    elapsed.start();
    proxyPolicyTests();
    deadlinePolicyTests();
    const auto address = ownedLocalAddress();
    check("non-loopback address belongs to this computer", !address.isNull());
    check("Qt TLS backend is available", QSslSocket::supportsSsl());
    if (!address.isNull())
    {
        managerConfigurationTests(address);
        persistentManagerTests(address);
        rawReplyDeadlineTests(address);
        for (const TaskCase &test : std::vector<TaskCase>{
                 {"500ms pre-send TLS blackhole", ResponseMode::Blackhole, 500},
                 {"5s pre-send TLS blackhole", ResponseMode::Blackhole, 5000},
                 {"post-send response stall", ResponseMode::Stall, 500},
                 {"post-send continuous response", ResponseMode::Trickle, 500},
                 {"external abort", ResponseMode::Stall, 5000, 100},
                 {"empty successful response", ResponseMode::Empty, 500},
                 {"HTTP error response", ResponseMode::HttpError, 500},
                 {"expired worker queue deadline", ResponseMode::Stall, 100, 0, true},
                 {"destroyed caller", ResponseMode::Stall, 200, 0, false, true},
             })
        {
            taskCase(address, test);
        }
    }
    const QJsonObject report{
        {"qt", qVersion()}, {"ssl_backend", QSslSocket::activeBackend()},
        {"test_count", results.size()}, {"passed", results.size() - failures},
        {"failed", failures}, {"elapsed_ms", elapsed.elapsed()},
        {"production_objects", true}, {"real_profile_loaded", false},
        {"system_proxy_changed", false}, {"external_requests", false},
        {"scope", "Injected proxy snapshots and fallback; persistent QNAM; owned local "
                  "CONNECT/TLS/HTTP servers; actual NetworkTask callbacks and deadlines"},
        {"tests", results}, {"observations", observations}};
    QFile output(outputPath);
    const auto json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(json) != json.size())
    {
        return 4;
    }
    return failures == 0 ? 0 : 1;
}
