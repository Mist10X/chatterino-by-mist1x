// SPDX-License-Identifier: MIT

#include "common/ArchiveChatPolicy.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"

#include <pajlada/signals/signal.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>

namespace chatterino {
std::optional<ArchiveChatPolicy> ArchiveChatPolicy::parse(QByteArray data)
{
    if (data.size() > 150000) return std::nullopt;
    data.replace("\r\n", "\n");
    if (!data.startsWith("HT2\n") || !data.endsWith("\nEND\n")) return std::nullopt;
    ArchiveChatPolicy result;
    bool enabledSeen = false, keepSeen = false;
    int count = -1;
    static const QRegularExpression name("^[a-z0-9_]{1,64}$");
    for (const auto &line : data.split('\n'))
    {
        const auto fields = line.split('\t');
        if (fields[0] == "auto_trim")
        {
            if (enabledSeen || fields.size() != 2 || (fields[1] != "on" && fields[1] != "off")) return std::nullopt;
            result.enabled = fields[1] == "on"; enabledSeen = true;
        }
        else if (fields[0] == "auto_trim_keep")
        {
            if (keepSeen || fields.size() != 2) return std::nullopt;
            bool ok; const auto value = fields[1].toUInt(&ok);
            if (!ok || value < 1 || value > 1000000) return std::nullopt;
            result.keep = value; keepSeen = true;
        }
        else if (fields[0] == "trim_preserve_count")
        {
            if (count != -1 || fields.size() != 2) return std::nullopt;
            bool ok; count = fields[1].toInt(&ok);
            if (!ok || count < 0 || count > 1000) return std::nullopt;
        }
        else if (fields[0] == "trim_preserve")
        {
            if (fields.size() != 2) return std::nullopt;
            const auto channel = QString::fromLatin1(fields[1]);
            if (!name.match(channel).hasMatch() || result.preserved.contains(channel)) return std::nullopt;
            result.preserved.insert(channel);
        }
    }
    if (!enabledSeen || !keepSeen || count != result.preserved.size()) return std::nullopt;
    return result;
}

size_t ArchiveChatPolicy::limitFor(const QString &channel, size_t fallback) const
{
    if (!this->enabled) return fallback;
    // The plugin owns the hourly schedule and exceptions. Between cleanups
    // every channel must grow; keep is a trim target, never a rolling cap.
    (void)channel;
    return 0;
}

namespace {
class Reader final : public QObject {
public:
    explicit Reader(QString path) : QObject(QCoreApplication::instance()), path_(std::move(path))
    {
        this->reload();
        QObject::connect(&this->timer_, &QTimer::timeout, this, [this] { this->reload(); });
        this->timer_.start(2000);
    }
    ArchiveChatPolicy policy;
    pajlada::Signals::NoArgSignal changed;
private:
    void reload()
    {
        QFile file(this->path_);
        // Keep the last valid policy during an atomic replacement or partial write.
        if (!file.open(QIODevice::ReadOnly) || file.size() > 150000) return;
        const auto data = file.read(150001);
        if (data == this->previous_) return;
        if (auto next = ArchiveChatPolicy::parse(data))
        {
            this->previous_ = data;
            if (policy.enabled == next->enabled && policy.keep == next->keep && policy.preserved == next->preserved) return;
            policy = std::move(*next);
            changed.invoke();
        }
    }
    QString path_;
    QByteArray previous_;
    QTimer timer_;
};
}

void followArchiveChatPolicy(Channel &channel)
{
    static QPointer<Reader> reader;
    if (!reader)
    {
        reader = new Reader(QDir(getApp()->getPaths().pluginsDirectory).filePath("history-tools/data/panel-control.txt"));
    }
    auto *source = reader.data();
    channel.followMessageLimit(reader->changed, [source, &channel] {
        return source->policy.limitFor(channel.getName(), size_t(std::clamp(getSettings()->scrollbackSplitLimit.getValue(), 100, 100000)));
    });
}
}  // namespace chatterino
