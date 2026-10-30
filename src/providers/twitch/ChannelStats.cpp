// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/ChannelStats.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "controllers/emotes/EmoteController.hpp"
#include "messages/Emote.hpp"
#include "messages/Message.hpp"
#include "messages/MessageElement.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchEmotes.hpp"
#include "util/PostToThread.hpp"

#include <pajlada/signals/scoped-connection.hpp>
#include <QSet>
#include <QTimer>
#include <QTimeZone>

#include <optional>
#include <unordered_map>
#include <vector>

namespace chatterino {

namespace {

/// Keeps the per-minute counts and the new chatters list bounded; a stream
/// longer than this is counted for its last day.
constexpr qint64 KEEP_MINUTES = 24 * 60;
constexpr int KEEP_NEW_CHATTERS = 500;
/// A shorter stretch before the first message in the chat is not worth
/// asking the logs for.
constexpr qint64 MIN_GAP_MS = 60 * 1000;
/// The logs are read in pieces of this length (a busy chat writes ~10 MB/h).
constexpr qint64 WINDOW_MS = 30 * 60 * 1000;
constexpr int LOG_TIMEOUT_MS = 60000;
constexpr int EMOTES_GRACE_MS = 4000;

/// rustlog mirrors, fastest first (the same ones the usercard reads)
const QStringList LOG_MIRRORS{
    QStringLiteral("https://logs.twitchmetrics.xyz"),
    QStringLiteral("https://logs.zonian.dev"),
    QStringLiteral("https://logs.spanix.team"),
};

/// The USERNOTICE kinds the chat marks as subscriptions (as MessageBuilder).
const QSet<QByteArray> SUB_NOTICES{"sub", "resub", "subgift", "anonsubgift"};

/// One counted thing, from the chat or from the logs.
struct Item {
    enum class Kind {
        Chat,
        Subscription,
        Moderation,
    } kind = Kind::Chat;
    QString id;
    QString login;
    QString name;
    QColor color;
    qint64 ms = 0;
    bool first = false;
    std::vector<EmotePtr> emotes;
    /// from the logs: Twitch emotes (id, name), made into emotes in the GUI
    /// thread
    std::vector<std::pair<QString, QString>> twitchEmotes;
};

struct Tracked {
    std::weak_ptr<Channel> channel;
    std::unique_ptr<pajlada::Signals::ScopedConnection> appended;
    std::unique_ptr<pajlada::Signals::ScopedConnection> prepended;
    std::unique_ptr<pajlada::Signals::ScopedConnection> status;
    /// the stream the numbers are for (empty while offline)
    QString streamId;
    /// ids of the messages already counted: the logs repeat the chat
    QSet<QString> seen;
    /// bumped when the counting starts anew; older log answers are dropped
    int generation = 0;
    /// some mirror had this channel's messages
    bool logsAnswered = false;
    ChannelStatsData data;
};

std::unordered_map<QString, Tracked> &tracked()
{
    // Never destroyed: at exit the channels (and their signals) are gone
    // before static objects, so dropping the connections then would touch
    // freed memory.
    static auto *map = new std::unordered_map<QString, Tracked>;
    return *map;
}

Tracked *find(const QString &name, int generation)
{
    auto it = tracked().find(name);
    if (it == tracked().end() || it->second.generation != generation)
    {
        return nullptr;
    }
    return &it->second;
}

void add(Tracked &entry, const Item &item)
{
    auto &data = entry.data;
    if (!item.id.isEmpty())
    {
        if (entry.seen.contains(item.id))
        {
            return;
        }
        entry.seen.insert(item.id);
    }
    if (item.kind == Item::Kind::Subscription)
    {
        ++data.subscriptions;
        return;
    }
    if (item.kind == Item::Kind::Moderation)
    {
        ++data.moderation;
        return;
    }

    ++data.messages;
    auto &chatter = data.chatters[item.login];
    if (chatter.name.isEmpty() || !item.name.isEmpty())
    {
        chatter.name = item.name.isEmpty() ? item.login : item.name;
    }
    if (item.color.isValid())
    {
        chatter.color = item.color;
    }
    ++chatter.messages;
    if (item.first && !data.newChatters.contains(item.login))
    {
        ++data.firstMessages;
        if (data.newChatters.size() < KEEP_NEW_CHATTERS)
        {
            data.newChatters.append(item.login);
        }
    }

    const auto minute = item.ms / 60000;
    ++data.perMinute[minute];
    while (!data.perMinute.isEmpty() &&
           data.perMinute.lastKey() - data.perMinute.firstKey() > KEEP_MINUTES)
    {
        data.perMinute.erase(data.perMinute.begin());
    }

    for (const auto &emote : item.emotes)
    {
        auto &use = data.emotes[emote->name.string];
        if (!use.emote)
        {
            use.emote = emote;
        }
        ++use.uses;
    }
}

std::optional<Item> itemFrom(const Message &message)
{
    Item item;
    item.id = message.id;
    item.ms = message.serverReceivedTime.isValid()
                  ? message.serverReceivedTime.toMSecsSinceEpoch()
                  : QDateTime::currentMSecsSinceEpoch();
    const auto flags = message.flags;
    if (flags.has(MessageFlag::Subscription))
    {
        item.kind = Item::Kind::Subscription;
        return item;
    }
    if (flags.hasAny({MessageFlag::Timeout, MessageFlag::ModerationAction,
                      MessageFlag::ClearChat}))
    {
        item.kind = Item::Kind::Moderation;
        return item;
    }
    if (flags.hasAny({MessageFlag::System, MessageFlag::Whisper}) ||
        message.loginName.isEmpty())
    {
        return std::nullopt;
    }

    item.login = message.loginName.toLower();
    item.name =
        message.displayName.isEmpty() ? message.loginName : message.displayName;
    item.color = message.usernameColor;
    item.first = flags.has(MessageFlag::FirstMessage);
    for (const auto &element : message.elements)
    {
        if (const auto *emote =
                dynamic_cast<const EmoteElement *>(element.get()))
        {
            if (auto ptr = emote->getEmote())
            {
                item.emotes.push_back(std::move(ptr));
            }
        }
        else if (const auto *layered =
                     dynamic_cast<const LayeredEmoteElement *>(element.get()))
        {
            if (!layered->getEmotes().empty() &&
                layered->getEmotes().front().ptr)
            {
                item.emotes.push_back(layered->getEmotes().front().ptr);
            }
        }
    }
    return item;
}

// ---- the public logs ------------------------------------------------------

/// The emote maps a log line's words are looked up in (snapshots: the maps
/// themselves are never changed, only replaced).
using EmoteMaps = std::vector<std::shared_ptr<const EmoteMap>>;

EmoteMaps emoteMapsOf(const std::shared_ptr<Channel> &channel)
{
    EmoteMaps maps;
    if (auto *twitch = dynamic_cast<TwitchChannel *>(channel.get()))
    {
        maps.push_back(twitch->seventvEmotes());
        maps.push_back(twitch->bttvEmotes());
        maps.push_back(twitch->ffzEmotes());
    }
    auto *app = getApp();
    maps.push_back(app->getSeventvEmotes()->globalEmotes());
    maps.push_back(app->getBttvEmotes()->emotes());
    maps.push_back(app->getFfzEmotes()->emotes());
    std::erase(maps, nullptr);
    return maps;
}

std::vector<QByteArrayView> split(QByteArrayView text, char separator)
{
    std::vector<QByteArrayView> parts;
    qsizetype from = 0;
    while (from <= text.size())
    {
        auto end = text.indexOf(separator, from);
        if (end < 0)
        {
            end = text.size();
        }
        parts.push_back(text.sliced(from, end - from));
        from = end + 1;
    }
    return parts;
}

QString unescapeTag(const QByteArray &value)
{
    if (!value.contains('\\'))
    {
        return QString::fromUtf8(value);
    }
    QByteArray out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i)
    {
        if (value[i] != '\\' || i + 1 == value.size())
        {
            out.append(value[i]);
            continue;
        }
        switch (value[++i])
        {
            case 's':
                out.append(' ');
                break;
            case ':':
                out.append(';');
                break;
            case 'r':
                out.append('\r');
                break;
            case 'n':
                out.append('\n');
                break;
            default:
                out.append(value[i]);
        }
    }
    return QString::fromUtf8(out);
}

/// The chat messages, subscriptions and moderation in a mirror's raw answer
/// (IRC lines with tags), within [fromMs, toMs). Runs off the GUI thread.
std::vector<Item> parseLog(const QByteArray &body, qint64 fromMs, qint64 toMs,
                           const EmoteMaps &maps)
{
    std::vector<Item> items;
    for (const auto line : split(body, '\n'))
    {
        if (!line.startsWith('@'))
        {
            continue;
        }
        const auto tagsEnd = line.indexOf(' ');
        if (tagsEnd < 0)
        {
            continue;
        }
        QHash<QByteArray, QByteArray> tags;
        for (const auto tag : split(line.sliced(1, tagsEnd - 1), ';'))
        {
            const auto eq = tag.indexOf('=');
            if (eq > 0)
            {
                tags.insert(tag.first(eq).toByteArray(),
                            tag.sliced(eq + 1).toByteArray());
            }
        }
        auto rest = line.sliced(tagsEnd + 1);
        QByteArrayView prefix;
        if (rest.startsWith(':'))
        {
            const auto end = rest.indexOf(' ');
            if (end < 0)
            {
                continue;
            }
            prefix = rest.sliced(1, end - 1);
            rest = rest.sliced(end + 1);
        }
        const auto commandEnd = rest.indexOf(' ');
        const auto command = commandEnd < 0 ? rest : rest.first(commandEnd);
        const auto textStart = rest.indexOf(" :");
        const auto text = textStart < 0 ? QByteArrayView()
                                        : rest.sliced(textStart + 2);

        Item item;
        item.ms = tags.value("tmi-sent-ts").toLongLong();
        if (item.ms < fromMs || item.ms >= toMs)
        {
            continue;
        }
        item.id = QString::fromLatin1(tags.value("id"));

        if (command == "CLEARCHAT" || command == "CLEARMSG")
        {
            item.kind = Item::Kind::Moderation;
            item.id.clear();
            items.push_back(std::move(item));
            continue;
        }
        if (command == "USERNOTICE")
        {
            if (SUB_NOTICES.contains(tags.value("msg-id")))
            {
                item.kind = Item::Kind::Subscription;
                items.push_back(std::move(item));
            }
            continue;
        }
        if (command != "PRIVMSG")
        {
            continue;
        }

        const auto bang = prefix.indexOf('!');
        item.login =
            QString::fromUtf8(bang < 0 ? prefix : prefix.first(bang)).toLower();
        if (item.login.isEmpty())
        {
            continue;
        }
        item.name = unescapeTag(tags.value("display-name"));
        if (const auto color = tags.value("color"); !color.isEmpty())
        {
            item.color = QColor(QString::fromLatin1(color));
        }
        item.first = tags.value("first-msg") == "1";

        auto message = QString::fromUtf8(text);
        if (message.startsWith(u"\x01" "ACTION "))
        {
            message = message.mid(8);
            if (message.endsWith(u'\x01'))
            {
                message.chop(1);
            }
        }

        // Twitch emotes: "id:from-to,from-to/id:from-to" (code points)
        QSet<QString> twitchNames;
        if (const auto emotes = tags.value("emotes"); !emotes.isEmpty())
        {
            const auto codepoints = message.toUcs4();
            for (const auto emote : split(emotes, '/'))
            {
                const auto colon = emote.indexOf(':');
                if (colon <= 0)
                {
                    continue;
                }
                const auto id = QString::fromLatin1(emote.first(colon));
                for (const auto range : split(emote.sliced(colon + 1), ','))
                {
                    const auto dash = range.indexOf('-');
                    if (dash <= 0)
                    {
                        continue;
                    }
                    const auto from = range.first(dash).toInt();
                    const auto to = range.sliced(dash + 1).toInt();
                    if (from < 0 || to < from || to >= codepoints.size())
                    {
                        continue;
                    }
                    const auto name = QString::fromUcs4(
                        reinterpret_cast<const char32_t *>(
                            codepoints.constData()) +
                            from,
                        to - from + 1);
                    twitchNames.insert(name);
                    item.twitchEmotes.emplace_back(id, name);
                }
            }
        }
        // 7TV, BTTV and FFZ emotes: whole words found in the maps
        for (const auto word :
             QStringView(message).split(u' ', Qt::SkipEmptyParts))
        {
            const auto name = word.toString();
            if (twitchNames.contains(name))
            {
                continue;
            }
            for (const auto &map : maps)
            {
                auto it = map->find(EmoteName{name});
                if (it != map->end())
                {
                    item.emotes.push_back(it->second);
                    break;
                }
            }
        }
        items.push_back(std::move(item));
    }
    return items;
}

QString utcText(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC)
        .toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"));
}

void readWindow(const QString &name, int generation, qint64 fromMs,
                qint64 untilMs, int mirror, int tried, bool failed);

/// The next piece of the logs, or the end of reading.
void nextWindow(const QString &name, int generation, qint64 fromMs,
                qint64 untilMs, int mirror)
{
    auto *entry = find(name, generation);
    if (entry == nullptr)
    {
        return;
    }
    entry->data.historyRead = QDateTime::fromMSecsSinceEpoch(fromMs);
    if (fromMs >= untilMs)
    {
        // no mirror had any of it: the channel is not logged
        if (entry->data.history == ChannelStatsData::History::Loading)
        {
            entry->data.history = entry->logsAnswered
                                      ? ChannelStatsData::History::Done
                                      : ChannelStatsData::History::Unavailable;
        }
        return;
    }
    readWindow(name, generation, fromMs, untilMs, mirror, 0, false);
}

void readWindow(const QString &name, int generation, qint64 fromMs,
                qint64 untilMs, int mirror, int tried, bool failed)
{
    const auto toMs = std::min(fromMs + WINDOW_MS, untilMs);
    const auto url = QStringLiteral("%1/channel/%2?raw&from=%3&to=%4")
                         .arg(LOG_MIRRORS[mirror], name, utcText(fromMs),
                              utcText(toMs));
    auto *entry = find(name, generation);
    if (entry == nullptr)
    {
        return;
    }
    auto maps = emoteMapsOf(entry->channel.lock());

    // a mirror that does not have this piece: the next one is asked; when
    // none has it, the piece is skipped (404 means "no messages" too)
    const auto tryNext = [name, generation, fromMs, toMs, untilMs, mirror,
                          tried](bool failure) {
        const auto count = int(LOG_MIRRORS.size());
        if (tried + 1 < count)
        {
            readWindow(name, generation, fromMs, untilMs,
                       (mirror + 1) % count, tried + 1, failure);
            return;
        }
        if (failure)
        {
            if (auto *entry = find(name, generation))
            {
                entry->data.history = ChannelStatsData::History::Partial;
            }
        }
        nextWindow(name, generation, toMs, untilMs, mirror);
    };

    NetworkRequest(QUrl(url))
        .timeout(LOG_TIMEOUT_MS)
        .concurrent()
        .onSuccess([name, generation, fromMs, toMs, untilMs, mirror,
                    maps = std::move(maps)](const NetworkResult &result) {
            auto items = parseLog(result.getData(), fromMs, toMs, maps);
            postToThread([name, generation, toMs, untilMs, mirror,
                          items = std::move(items)]() mutable {
                auto *entry = find(name, generation);
                if (entry == nullptr)
                {
                    return;
                }
                entry->logsAnswered = true;
                auto *twitchEmotes = getApp()->getEmotes()->getTwitchEmotes();
                for (auto &item : items)
                {
                    for (const auto &[id, emoteName] : item.twitchEmotes)
                    {
                        item.emotes.push_back(twitchEmotes->getOrCreateEmote(
                            EmoteId{id}, EmoteName{emoteName}));
                    }
                    add(*entry, item);
                }
                nextWindow(name, generation, toMs, untilMs, mirror);
            });
        })
        .onError([name, generation, failed, tryNext](
                     const NetworkResult &result) {
            const auto status = result.status();
            postToThread([name, generation, failed, tryNext, status] {
                if (find(name, generation) == nullptr)
                {
                    return;
                }
                if (status == 403)
                {
                    // the channel opted out of being logged
                    find(name, generation)->data.history =
                        ChannelStatsData::History::Unavailable;
                    return;
                }
                tryNext(failed || status != 404);
            });
        })
        .execute();
}

// ---- counting ---------------------------------------------------------------

/// Counts anew: the current stream (the chat's messages since it went live,
/// the rest from the logs) or, while offline, what the chat shows.
void restart(const QString &name, Tracked &entry,
             const std::shared_ptr<Channel> &channel)
{
    ++entry.generation;
    entry.seen.clear();
    entry.logsAnswered = false;
    entry.data = {};
    entry.streamId.clear();

    const auto now = QDateTime::currentDateTime();
    QDateTime start;
    auto *twitch = dynamic_cast<TwitchChannel *>(channel.get());
    if (twitch != nullptr && twitch->isLive())
    {
        const auto status = twitch->accessStreamStatus();
        if (status->startedAt.isValid())
        {
            start = std::max(status->startedAt.toLocalTime(),
                             now.addSecs(-KEEP_MINUTES * 60));
            entry.streamId = status->streamId;
        }
    }
    // stand only: "<from>/<to>" (ISO) — a past stream of this channel, read
    // from the logs as if it were the current one
    std::optional<qint64> testUntil;
    if (const auto test = qEnvironmentVariable("MIST1X_STATS_TEST_STREAM");
        !test.isEmpty())
    {
        const auto parts = test.split(u'/');
        start = QDateTime::fromString(parts.value(0), Qt::ISODate);
        testUntil = QDateTime::fromString(parts.value(1), Qt::ISODate)
                        .toMSecsSinceEpoch();
        entry.streamId = QStringLiteral("test");
    }
    entry.data.live = start.isValid();
    entry.data.sinceStreamStart = start.isValid();
    const auto startMs = start.isValid() ? start.toMSecsSinceEpoch() : 0;

    std::optional<qint64> earliest = testUntil;
    for (const auto &message : channel->getMessageSnapshot())
    {
        if (testUntil)
        {
            break;
        }
        auto item = itemFrom(*message);
        if (!item || item->ms < startMs)
        {
            continue;
        }
        if (!earliest || item->ms < *earliest)
        {
            earliest = item->ms;
        }
        add(entry, *item);
    }

    if (!start.isValid())
    {
        entry.data.since = earliest
                               ? QDateTime::fromMSecsSinceEpoch(std::min(
                                     *earliest, now.toMSecsSinceEpoch()))
                               : now;
        return;
    }
    entry.data.since = start;
    const auto untilMs = earliest.value_or(now.toMSecsSinceEpoch());
    if (untilMs - startMs > MIN_GAP_MS)
    {
        entry.data.history = ChannelStatsData::History::Loading;
        entry.data.historyUntil = QDateTime::fromMSecsSinceEpoch(untilMs);
        entry.data.historyRead = start;
        // a moment for the channel's emotes to load: the log lines' words
        // are looked up in them
        QTimer::singleShot(EMOTES_GRACE_MS, [name, generation = entry.generation,
                                             startMs, untilMs] {
            if (find(name, generation) != nullptr)
            {
                readWindow(name, generation, startMs, untilMs, 0, 0, false);
            }
        });
    }
}

/// A stream that went live (or a new one) starts the numbers anew; one that
/// ended keeps them.
void onStatus(const QString &name)
{
    auto it = tracked().find(name);
    if (it == tracked().end())
    {
        return;
    }
    auto &entry = it->second;
    auto channel = entry.channel.lock();
    auto *twitch = dynamic_cast<TwitchChannel *>(channel.get());
    if (twitch == nullptr || entry.streamId == u"test")
    {
        return;
    }
    if (twitch->isLive())
    {
        const auto status = twitch->accessStreamStatus();
        const bool newStream =
            status->startedAt.isValid() && status->streamId != entry.streamId;
        if (newStream)
        {
            restart(name, entry, channel);
        }
    }
    else
    {
        entry.data.live = false;
    }
}

}  // namespace

void ChannelStats::attach(const std::shared_ptr<Channel> &channel)
{
    if (!channel || channel->getName().isEmpty())
    {
        return;
    }
    const auto name = channel->getName().toLower();
    auto &entry = tracked()[name];
    if (entry.channel.lock() == channel)
    {
        return;  // already counted
    }
    entry.channel = channel;
    entry.status.reset();
    restart(name, entry, channel);

    std::weak_ptr<Channel> weak = channel;
    entry.appended = std::make_unique<pajlada::Signals::ScopedConnection>(
        channel->messageAppended.connect(
            [name, weak](MessagePtr &message, auto) {
                auto it = tracked().find(name);
                if (it == tracked().end() || weak.lock() == nullptr ||
                    !message)
                {
                    return;
                }
                if (auto item = itemFrom(*message))
                {
                    add(it->second, *item);
                }
            }));
    // the history loaded on joining comes in front of the chat
    entry.prepended = std::make_unique<pajlada::Signals::ScopedConnection>(
        channel->messagesAddedAtStart.connect(
            [name, weak](std::vector<MessagePtr> &messages) {
                auto it = tracked().find(name);
                if (it == tracked().end() || weak.lock() == nullptr)
                {
                    return;
                }
                auto &data = it->second.data;
                const auto startMs =
                    data.sinceStreamStart ? data.since.toMSecsSinceEpoch() : 0;
                const auto logsUntil =
                    data.history == ChannelStatsData::History::NotNeeded
                        ? 0
                        : data.historyUntil.toMSecsSinceEpoch();
                for (const auto &message : messages)
                {
                    auto item = message ? itemFrom(*message) : std::nullopt;
                    // the logs count this stretch's subscriptions and bans
                    if (!item || item->ms < startMs ||
                        (item->kind != Item::Kind::Chat &&
                         item->ms < logsUntil))
                    {
                        continue;
                    }
                    add(it->second, *item);
                    if (!data.sinceStreamStart &&
                        item->ms < data.since.toMSecsSinceEpoch())
                    {
                        data.since = QDateTime::fromMSecsSinceEpoch(item->ms);
                    }
                }
            }));
    if (auto *twitch = dynamic_cast<TwitchChannel *>(channel.get()))
    {
        entry.status = std::make_unique<pajlada::Signals::ScopedConnection>(
            twitch->streamStatusChanged.connect([name] {
                onStatus(name);
            }));
    }
}

ChannelStatsData ChannelStats::get(const QString &channelName)
{
    auto it = tracked().find(channelName.toLower());
    if (it == tracked().end())
    {
        return {};
    }
    return it->second.data;
}

}  // namespace chatterino
