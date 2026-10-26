// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/irc/IrcProxy.hpp"

#include <QAbstractSocket>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QVariant>

#ifdef Q_OS_WIN
#    include <qt_windows.h>
#    include <wininet.h>
#endif

namespace chatterino {

namespace {

constexpr auto LIVE_SYSTEM_PROXY = "leafyrinoLiveSystemProxy";

}  // namespace

namespace detail {

bool systemProxyIsDirect(const std::optional<SystemProxyState> &state)
{
    // PROXY_TYPE_DIRECT is 1. Requiring exactly DIRECT also rejects PAC, WPAD,
    // manual proxies, conflicting effective/UI settings, and unknown flags.
    return state && state->effectiveFlags == 1 && state->configuredFlags == 1;
}

IrcProxyRefresh applyIrcProxyPolicy(
    QAbstractSocket *socket, bool usesSystemConfiguration,
    bool hasExplicitProxy, const std::optional<SystemProxyState> &state)
{
    if (socket == nullptr ||
        socket->state() != QAbstractSocket::UnconnectedState)
    {
        return IrcProxyRefresh::Unchanged;
    }

    const auto proxy = socket->proxy();
    const bool ourDirectProxy = socket->property(LIVE_SYSTEM_PROXY).toBool() &&
                                proxy.type() == QNetworkProxy::NoProxy;

    // Do not replace a proxy deliberately set on this socket by another caller.
    if (proxy.type() != QNetworkProxy::DefaultProxy && !ourDirectProxy)
    {
        return IrcProxyRefresh::Unchanged;
    }

    if (hasExplicitProxy || !usesSystemConfiguration)
    {
        if (!ourDirectProxy)
        {
            return IrcProxyRefresh::Unchanged;
        }
        // An application override appeared after our previous direct attempt.
        socket->setProxy(QNetworkProxy::DefaultProxy);
        socket->setProperty(LIVE_SYSTEM_PROXY, false);
        return IrcProxyRefresh::SystemDefault;
    }

    const bool direct = systemProxyIsDirect(state);
    socket->setProxy(direct ? QNetworkProxy::NoProxy
                            : QNetworkProxy::DefaultProxy);
    socket->setProperty(LIVE_SYSTEM_PROXY, direct);
    return direct ? IrcProxyRefresh::Direct : IrcProxyRefresh::SystemDefault;
}

}  // namespace detail

std::optional<detail::SystemProxyState> currentSystemProxyState()
{
#ifdef Q_OS_WIN
    // Query both configured (UI) and effective modes. These DWORD-only options
    // allocate no strings, read no credentials, and perform no PAC/network work.
    INTERNET_PER_CONN_OPTIONW options[2]{};
    options[0].dwOption = INTERNET_PER_CONN_FLAGS_UI;
    options[1].dwOption = INTERNET_PER_CONN_FLAGS;
    INTERNET_PER_CONN_OPTION_LISTW list{};
    list.dwSize = sizeof(list);
    list.dwOptionCount = 2;
    list.pOptions = options;
    DWORD size = sizeof(list);
    if (InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION,
                             &list, &size))
    {
        return detail::SystemProxyState{options[1].Value.dwValue,
                                        options[0].Value.dwValue};
    }

    // FLAGS_UI is unavailable on older Windows. Fall back only for that API
    // compatibility error; an unrelated query failure must not imply DIRECT.
    if (GetLastError() == ERROR_INVALID_PARAMETER && list.dwOptionError == 0)
    {
        options[0] = {};
        options[0].dwOption = INTERNET_PER_CONN_FLAGS;
        list.dwOptionCount = 1;
        list.dwOptionError = 0;
        size = sizeof(list);
        if (InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION,
                                 &list, &size))
        {
            return detail::SystemProxyState{options[0].Value.dwValue,
                                            options[0].Value.dwValue};
        }
    }
#endif
    return std::nullopt;
}

IrcProxyRefresh refreshIrcSystemProxy(QAbstractSocket *socket,
                                      bool hasExplicitProxy)
{
#ifdef Q_OS_WIN
    return detail::applyIrcProxyPolicy(
        socket, QNetworkProxyFactory::usesSystemConfiguration(),
        hasExplicitProxy, currentSystemProxyState());
#else
    Q_UNUSED(socket);
    Q_UNUSED(hasExplicitProxy);
    return IrcProxyRefresh::Unchanged;
#endif
}

}  // namespace chatterino
