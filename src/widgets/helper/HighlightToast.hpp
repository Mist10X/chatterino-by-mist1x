// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

class QWidget;

#include <memory>

namespace chatterino {

struct Message;
using MessagePtr = std::shared_ptr<const Message>;

/// A highlight that flashes the taskbar also shows a small card at the top of
/// the main window (like Chatterino+), unless its channel is already on the
/// open tab. Click: switch to that channel's tab and scroll to the message.
void showHighlightToast(const QString &channelName, const MessagePtr &message);

/// Offline preview only: the same card on any widget.
void showHighlightToastOn(QWidget *window, const QString &channelName,
                          const MessagePtr &message);

}  // namespace chatterino
