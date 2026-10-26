// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QSet>
#include <QString>
#include <optional>

namespace chatterino {
class Channel;

// Only the nonsecret retention fields of history-tools' existing HT2 sidecar.
struct ArchiveChatPolicy {
    bool enabled = false;
    size_t keep = 1000;
    QSet<QString> preserved;
    static std::optional<ArchiveChatPolicy> parse(QByteArray data);
    size_t limitFor(const QString &channel, size_t fallback) const;
};

// One local reader for all real Twitch channels. Never touches archive data.
void followArchiveChatPolicy(Channel &channel);
}  // namespace chatterino
