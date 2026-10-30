// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

class QWidget;

namespace chatterino {

/// "Chat statistics" of a channel: messages, activity, the most active
/// chatters and emotes, new chatters (see ChannelStats). Updates itself while
/// open.
void showStreamStatsDialog(QWidget *parent, const QString &channelName);

}  // namespace chatterino
