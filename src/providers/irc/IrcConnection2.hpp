// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "util/ExponentialBackoff.hpp"

#include <IrcConnection>
#include <pajlada/signals/signal.hpp>
#include <QTimer>

#include <chrono>

namespace chatterino {

class IrcConnection : public Communi::IrcConnection
{
public:
    IrcConnection(QObject *parent = nullptr);
    ~IrcConnection() override;

    pajlada::Signals::Signal<bool> connectionLost;

    pajlada::Signals::NoArgSignal heartbeat;

    /// Schedules a reconnect; returns its delay, or 0 if one is already queued.
    std::chrono::milliseconds smartReconnect();

    /// Name used in Logs/connection.log ("read", "write", ...).
    void setDiagnosticsLabel(const QString &label);
    /// Readable reason of the last failure ("" while none).
    QString lastError() const;

    virtual void open();
    virtual void close();

private:
    void logEvent(const QString &text) const;

    QString label_ = QStringLiteral("irc");
    QString lastError_;
    QTimer pingTimer_;
    QTimer reconnectTimer_;
    std::atomic<bool> recentlyReceivedMessage_{true};
    std::chrono::time_point<std::chrono::system_clock> lastPing_;

    ExponentialBackoff<5> reconnectBackoff_{std::chrono::milliseconds{1000}};

    std::atomic<bool> expectConnectionLoss_{false};

    std::atomic<bool> waitingForPong_{false};
};

}  // namespace chatterino
