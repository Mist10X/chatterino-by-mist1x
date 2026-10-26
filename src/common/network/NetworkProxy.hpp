// SPDX-License-Identifier: MIT
#pragma once

#include "providers/irc/IrcProxy.hpp"

#include <QNetworkProxyFactory>

#include <cstdint>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QObject;

namespace chatterino {

struct HttpProxySnapshot {
    bool usesSystemConfiguration = false;
    bool hasExplicitProxy = false;
    std::optional<detail::SystemProxyState> system;
};

// Per-manager factory, never installed as the global application factory.
// Only a verified live DIRECT setting overrides Qt's cached system resolver.
class HttpProxyFactory final : public QNetworkProxyFactory
{
public:
    using SnapshotReader = std::function<HttpProxySnapshot()>;
    using Fallback = std::function<QList<QNetworkProxy>(const QNetworkProxyQuery &)>;

    explicit HttpProxyFactory(SnapshotReader reader = {}, Fallback fallback = {});
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override;

private:
    SnapshotReader reader_;
    Fallback fallback_;
};

QNetworkAccessManager *createNetworkAccessManager(QObject *parent = nullptr);
void configureNetworkAccessManager(QNetworkAccessManager &manager);

// Absolute per-reply deadline for independent, finite HTTP image/metadata GETs.
// Long-lived SSE streams deliberately do not use this helper.
void setNetworkReplyTimeout(QNetworkReply *reply, int timeoutMs);

// Observed proxy-state epoch for failed image recovery. Reads only DWORD flags;
// refresh is throttled to at most once per second outside normal proxy queries.
uint64_t httpNetworkGeneration();

}  // namespace chatterino
