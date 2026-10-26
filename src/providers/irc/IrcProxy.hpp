// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QtGlobal>

#include <optional>

class QAbstractSocket;

namespace chatterino {

enum class IrcProxyRefresh {
    Unchanged,
    SystemDefault,
    Direct,
};

namespace detail {

struct SystemProxyState {
    quint32 effectiveFlags;
    quint32 configuredFlags;
};

bool systemProxyIsDirect(const std::optional<SystemProxyState> &state);

IrcProxyRefresh applyIrcProxyPolicy(
    QAbstractSocket *socket, bool usesSystemConfiguration,
    bool hasExplicitProxy, const std::optional<SystemProxyState> &state);

}  // namespace detail

/// Reads proxy mode only; never retrieves credentials or changes Windows settings.
std::optional<detail::SystemProxyState> currentSystemProxyState();

/// Refreshes an idle IRC socket before connecting. Explicit proxy settings win.
IrcProxyRefresh refreshIrcSystemProxy(QAbstractSocket *socket,
                                      bool hasExplicitProxy);

}  // namespace chatterino
