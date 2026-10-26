// SPDX-License-Identifier: MIT
#include "common/network/NetworkProxy.hpp"

#include "common/Env.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <utility>

namespace chatterino {
namespace {

struct ObservedNetwork {
    std::mutex mutex;
    std::optional<detail::SystemProxyState> previous;
    std::atomic<uint64_t> generation{0};
    std::atomic<int64_t> checkedAt{0};
};

ObservedNetwork &observedNetwork()
{
    static ObservedNetwork state;
    return state;
}

int64_t steadyMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void observe(const std::optional<detail::SystemProxyState> &current)
{
    if (!current)
    {
        return;
    }
    auto &state = observedNetwork();
    std::lock_guard lock(state.mutex);
    if (state.previous &&
        (state.previous->effectiveFlags != current->effectiveFlags ||
         state.previous->configuredFlags != current->configuredFlags))
    {
        state.generation.fetch_add(1, std::memory_order_relaxed);
    }
    state.previous = current;
}

HttpProxySnapshot currentSnapshot()
{
    return {QNetworkProxyFactory::usesSystemConfiguration(),
            Env::get().proxyUrl.has_value(), currentSystemProxyState()};
}

}  // namespace

HttpProxyFactory::HttpProxyFactory(SnapshotReader reader, Fallback fallback)
    : reader_(reader ? std::move(reader) : currentSnapshot)
    , fallback_(fallback ? std::move(fallback)
                         : QNetworkProxyFactory::proxyForQuery)
{
}

QList<QNetworkProxy> HttpProxyFactory::queryProxy(const QNetworkProxyQuery &query)
{
    const auto state = this->reader_();
    observe(state.system);
    if (state.usesSystemConfiguration && !state.hasExplicitProxy &&
        detail::systemProxyIsDirect(state.system))
    {
        return {QNetworkProxy(QNetworkProxy::NoProxy)};
    }
    // Retain explicit application proxies, custom global factories, PAC/WPAD
    // and unknown/error cases. No direct fallback when an enabled proxy fails.
    return this->fallback_(query);
}

void configureNetworkAccessManager(QNetworkAccessManager &manager)
{
    if (manager.proxyFactory() == nullptr &&
        manager.proxy().type() == QNetworkProxy::DefaultProxy)
    {
        manager.setProxyFactory(new HttpProxyFactory);
    }
}

QNetworkAccessManager *createNetworkAccessManager(QObject *parent)
{
    auto *manager = new QNetworkAccessManager(parent);
    configureNetworkAccessManager(*manager);
    return manager;
}

void setNetworkReplyTimeout(QNetworkReply *reply, int timeoutMs)
{
    if (reply == nullptr || reply->isFinished())
    {
        return;
    }
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->setTimerType(Qt::PreciseTimer);
    QObject::connect(reply, &QNetworkReply::finished, timer, &QTimer::stop);
    QObject::connect(timer, &QTimer::timeout, reply, [reply] {
        if (reply->isRunning())
        {
            reply->abort();
        }
    });
    timer->start(std::max(1, timeoutMs));
}

uint64_t httpNetworkGeneration()
{
    auto &state = observedNetwork();
#ifdef Q_OS_WIN
    const auto now = steadyMilliseconds();
    auto checkedAt = state.checkedAt.load(std::memory_order_relaxed);
    if (now - checkedAt >= 1000 &&
        state.checkedAt.compare_exchange_strong(checkedAt, now,
                                               std::memory_order_relaxed))
    {
        observe(currentSystemProxyState());
    }
#endif
    return state.generation.load(std::memory_order_relaxed);
}

}  // namespace chatterino
