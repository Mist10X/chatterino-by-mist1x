// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/PublicChatLogs.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"

#include <IrcMessage>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <deque>
#include <memory>
#include <vector>

namespace chatterino::publiclogs {

namespace {

// The same mirrors the Archive asks (its Personal sources). They keep different
// stretches of time, so every page asks all of them.
QStringList &mirrors()
{
    static QStringList list{
        QStringLiteral("https://logs.zonian.dev"),
        QStringLiteral("https://logs.twitchmetrics.xyz"),
        QStringLiteral("https://logs.spanix.team"),
    };
    return list;
}
qint64 SPACING_MS = 1500;
constexpr qint64 COOLDOWN_MS = 30000;
// A mirror that failed or kept a page waiting is left out for a while (as the
// Archive does): one dead service must not slow every page.
constexpr qint64 COLD_MS = 10 * 60 * 1000;
// Once one mirror has answered, a page waits this long for the others.
constexpr int GRACE_MS = 3000;
constexpr int REQUEST_TIMEOUT_MS = 8000;
// A page that waited this long for its turn gives up on that mirror.
constexpr qint64 PAGE_DEADLINE_MS = 25000;

qint64 nowMs()
{
    static const QElapsedTimer clock = [] {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return clock.elapsed();
}

/// One service: one request at a time, spaced, held back after a 429/503.
struct Host {
    bool busy = false;
    bool waiting = false;
    qint64 nextAt = 0;
    qint64 coldUntil = 0;
    std::deque<std::function<void()>> queue;
};

std::deque<Host> &hosts()
{
    static std::deque<Host> list(size_t(mirrors().size()));
    return list;
}

Host &host(size_t index)
{
    return hosts()[index];
}

void pump(size_t index)
{
    auto &h = host(index);
    if (h.busy || h.waiting || h.queue.empty())
    {
        return;
    }
    const auto wait = h.nextAt - nowMs();
    if (wait > 0)
    {
        h.waiting = true;
        // Precise: a coarse timer may fire a little early.
        QTimer::singleShot(int(wait), Qt::PreciseTimer, [index] {
            host(index).waiting = false;
            pump(index);
        });
        return;
    }
    auto job = std::move(h.queue.front());
    h.queue.pop_front();
    h.busy = true;
    h.nextAt = nowMs() + SPACING_MS;
    job();
}

void release(size_t index, bool throttled)
{
    auto &h = host(index);
    h.busy = false;
    if (throttled)
    {
        h.nextAt = std::max(h.nextAt, nowMs() + COOLDOWN_MS);
    }
    pump(index);
}

// Opt-outs seen this session: not asked again, through any mirror.
QSet<QString> &refusedUsers()
{
    static QSet<QString> users;
    return users;
}

QSet<QString> &refusedChannels()
{
    static QSet<QString> channels;
    return channels;
}

bool isRefused(const QString &userId, const QString &channel)
{
    return refusedUsers().contains(userId) ||
           refusedChannels().contains(channel);
}

QString utcText(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC)
        .toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"));
}

struct Page {
    QString userId;
    QString login;
    QString channel;
    qint64 untilMs = 0;
    bool hasCursor = false;
    qint64 beforeMs = 0;
    QString beforeId;
    QPointer<QObject> caller;
    bool hasCaller = false;
    PageCallback callback;
    qint64 deadline = 0;

    size_t pending = 0;
    // Mirrors this page still waits for (by index).
    std::vector<bool> waitingOn;
    bool finished = false;
    bool graceArmed = false;
    size_t answered = 0;
    size_t failures = 0;
    bool refused = false;
    QHash<QString, QJsonObject> rows;
    // The newest "oldest row" among the mirrors that returned a full page:
    // below it a full mirror may hold rows this page has not seen.
    std::optional<qint64> horizon;
};

using PagePtr = std::shared_ptr<Page>;

bool beforeCursor(const Page &page, qint64 at, const QString &id)
{
    return !page.hasCursor || at < page.beforeMs ||
           (at == page.beforeMs && id < page.beforeId);
}

QJsonObject emptyResult(const Page &page)
{
    return {
        {"identity",
         QJsonObject{{"user_id", page.userId}, {"user", page.login}}},
        {"channel", page.channel},
        {"scope", "channel"},
        {"source", "public_logs"},
        {"status", "empty"},
        {"rows", QJsonArray{}},
        {"more", false},
        {"next_cursor", QJsonValue::Null},
    };
}

void deliver(const PagePtr &page, QJsonObject result)
{
    if (page->hasCaller && page->caller.isNull())
    {
        return;
    }
    if (page->callback)
    {
        auto callback = std::move(page->callback);
        page->callback = nullptr;
        callback(result);
    }
}

void finish(const PagePtr &page)
{
    auto result = emptyResult(*page);
    if (page->refused)
    {
        result.insert("status", "refused");
        result.insert("error", "refused");
        deliver(page, result);
        return;
    }
    if (page->answered == 0)
    {
        result.insert("status", "error");
        result.insert("error", "source_unavailable");
        deliver(page, result);
        return;
    }

    std::vector<QJsonObject> rows;
    rows.reserve(size_t(page->rows.size()));
    for (const auto &row : page->rows)
    {
        if (!page->horizon ||
            qint64(row.value("at_ms").toDouble()) >= *page->horizon)
        {
            rows.push_back(row);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
        const auto at = qint64(a.value("at_ms").toDouble());
        const auto bt = qint64(b.value("at_ms").toDouble());
        return at == bt ? a.value("id").toString() > b.value("id").toString()
                        : at > bt;
    });

    QJsonArray pageRows;
    for (size_t i = 0; i < rows.size() && i < size_t(PAGE_SIZE); ++i)
    {
        pageRows.append(rows[i]);
    }
    bool more = page->horizon.has_value() || rows.size() > size_t(PAGE_SIZE);
    result.insert("rows", pageRows);
    result.insert("status", pageRows.isEmpty() ? "empty" : "found");
    if (more && !pageRows.isEmpty())
    {
        const auto last = pageRows.last().toObject();
        result.insert("next_cursor",
                      QJsonObject{{"time_utc", last.value("time_utc")},
                                  {"id", last.value("id")}});
    }
    else if (more)
    {
        // A full page without a single usable row: never claim the end.
        more = false;
        result.insert("status", "error");
        result.insert("error", "cursor_stalled");
    }
    result.insert("more", more);
    if (page->failures > 0)
    {
        // Another mirror may hold rows in this range: not the end of history.
        result.insert("error", "partial_source");
    }
    deliver(page, result);
}

void read(Page &page, const QByteArray &body)
{
    int lines = 0;
    std::optional<qint64> oldest;
    for (const auto &line : body.split('\n'))
    {
        const auto trimmed = line.trimmed();
        if (trimmed.isEmpty())
        {
            continue;
        }
        ++lines;
        auto row = parseLine(trimmed, page.userId, page.channel);
        if (!row)
        {
            continue;
        }
        const auto at = qint64(row->value("at_ms").toDouble());
        oldest = oldest ? std::min(*oldest, at) : at;
        const auto id = row->value("id").toString();
        if (at > page.untilMs || !beforeCursor(page, at, id) ||
            page.rows.contains(id))
        {
            continue;
        }
        page.rows.insert(id, *row);
    }
    if (lines >= PAGE_SIZE && oldest)
    {
        page.horizon = page.horizon ? std::max(*page.horizon, *oldest) : *oldest;
    }
}

QString requestUrl(size_t index, const Page &page)
{
    return mirrors()[qsizetype(index)] + u"/channel/" +
           QString::fromUtf8(QUrl::toPercentEncoding(page.channel)) +
           u"/userid/" + page.userId + u"?raw&reverse=true&from=" +
           QString::fromUtf8(QUrl::toPercentEncoding(utcText(0))) + u"&to=" +
           QString::fromUtf8(QUrl::toPercentEncoding(utcText(page.untilMs))) +
           u"&limit=" + QString::number(PAGE_SIZE);
}

void answered(const PagePtr &page, size_t index)
{
    if (page->finished)
    {
        return;  // a late mirror: the page was already delivered
    }
    page->waitingOn[index] = false;
    if (--page->pending == 0)
    {
        page->finished = true;
        finish(page);
        return;
    }
    if (page->answered > 0 && !page->graceArmed)
    {
        page->graceArmed = true;
        QTimer::singleShot(GRACE_MS, [page] {
            if (page->finished)
            {
                return;
            }
            for (size_t i = 0; i < page->waitingOn.size(); ++i)
            {
                if (page->waitingOn[i])
                {
                    host(i).coldUntil = nowMs() + COLD_MS;
                    ++page->failures;
                }
            }
            page->pending = 0;
            page->finished = true;
            finish(page);
        });
    }
}

void ask(const PagePtr &page, size_t index)
{
    host(index).queue.push_back([page, index] {
        if (page->finished)
        {
            host(index).nextAt = nowMs();
            release(index, false);
            return;
        }
        if ((page->hasCaller && page->caller.isNull()) || page->refused ||
            isRefused(page->userId, page->channel) ||
            nowMs() > page->deadline)
        {
            if (!page->refused && isRefused(page->userId, page->channel))
            {
                page->refused = true;
            }
            else if (!page->refused)
            {
                ++page->failures;
            }
            // Nothing was sent: the next request need not wait for spacing.
            host(index).nextAt = nowMs();
            release(index, false);
            answered(page, index);
            return;
        }
        auto throttled = std::make_shared<bool>(false);
        auto failed = std::make_shared<bool>(false);
        NetworkRequest(QUrl(requestUrl(index, *page)))
            .timeout(REQUEST_TIMEOUT_MS)
            .followRedirects(false)
            .header("Accept", "text/plain")
            .onSuccess([page, failed](const NetworkResult &result) {
                const auto &data = result.getData();
                const auto first = data.trimmed().left(1);
                if (!data.trimmed().isEmpty() && first != "@" && first != ":")
                {
                    *failed = true;  // not rustlog's raw format
                    return;
                }
                if (!page->finished)
                {
                    ++page->answered;
                    read(*page, data);
                }
            })
            .onError([page, throttled, failed](const NetworkResult &result) {
                const auto status = result.status().value_or(0);
                const auto &body = result.getData();
                if (status == 404)
                {
                    // «No user logs found»: this mirror logs the channel and
                    // the person wrote nothing there. Other 404s: nothing here.
                    if (!page->finished)
                    {
                        ++page->answered;
                    }
                    return;
                }
                if (status == 403)
                {
                    const auto kind = refusalKind(body);
                    if (kind == u"user")
                    {
                        refusedUsers().insert(page->userId);
                        page->refused = true;
                        return;
                    }
                    if (kind == u"channel")
                    {
                        refusedChannels().insert(page->channel);
                        page->refused = true;
                        return;
                    }
                }
                *throttled = status == 429 || status == 503;
                *failed = true;
            })
            .finally([page, index, throttled, failed] {
                if (*failed)
                {
                    if (!*throttled)
                    {
                        host(index).coldUntil = nowMs() + COLD_MS;
                    }
                    if (!page->finished)
                    {
                        ++page->failures;
                    }
                }
                else
                {
                    host(index).coldUntil = 0;
                }
                release(index, *throttled);
                answered(page, index);
            })
            .execute();
    });
    pump(index);
}

}  // namespace

QString refusalKind(const QByteArray &body)
{
    const auto text = body.left(2048);
    if (!text.contains("has opted out of being logged"))
    {
        return {};
    }
    return text.contains("requested user") ? QStringLiteral("user")
                                           : QStringLiteral("channel");
}

std::optional<QJsonObject> parseLine(const QByteArray &line,
                                     const QString &userId,
                                     const QString &channel)
{
    std::unique_ptr<Communi::IrcMessage> message(
        Communi::IrcMessage::fromData(line.trimmed(), nullptr));
    if (!message || message->command() != u"PRIVMSG")
    {
        return std::nullopt;
    }
    auto *privmsg = dynamic_cast<Communi::IrcPrivateMessage *>(message.get());
    if (privmsg == nullptr ||
        privmsg->target().compare(u'#' + channel, Qt::CaseInsensitive) != 0)
    {
        return std::nullopt;
    }
    const auto tags = message->tags();
    const auto id = tags.getOrEmpty("id");
    if (id.isEmpty() || tags.getOrEmpty("user-id") != userId)
    {
        return std::nullopt;
    }
    bool ok = false;
    const auto at = tags.getOrEmpty("tmi-sent-ts").toLongLong(&ok);
    if (!ok || at <= 0)
    {
        return std::nullopt;
    }
    QJsonObject row{
        {"id", id},
        {"user", message->nick().toLower()},
        {"user_id", userId},
        {"channel", channel.toLower()},
        {"time_utc", utcText(at)},
        {"at_ms", double(at)},
        {"text", privmsg->content()},
        {"display_name", tags.getOrEmpty("display-name")},
        {"color", tags.getOrEmpty("color")},
        {"badges", tags.getOrEmpty("badges")},
    };
    if (tags.getOrEmpty("first-msg") == u"1")
    {
        row.insert("first_msg", true);
    }
    return row;
}

void requestPage(const QString &userId, const QString &login,
                 const QString &channel, const QJsonObject &before,
                 const QObject *caller, PageCallback callback)
{
    auto page = std::make_shared<Page>();
    page->userId = userId;
    page->login = login.toLower();
    page->channel = channel.toLower();
    page->caller = const_cast<QObject *>(caller);
    page->hasCaller = caller != nullptr;
    page->callback = std::move(callback);
    page->deadline = nowMs() + PAGE_DEADLINE_MS;
    page->untilMs = QDateTime::currentMSecsSinceEpoch();

    const auto digits = !userId.isEmpty() && userId.size() <= 20 &&
                        std::all_of(userId.begin(), userId.end(),
                                    [](QChar c) { return c.isDigit(); });
    if (!digits || page->channel.isEmpty())
    {
        auto result = emptyResult(*page);
        result.insert("status", "error");
        result.insert("error", "invalid_request");
        deliver(page, result);
        return;
    }
    if (!before.isEmpty())
    {
        const auto stamp = QDateTime::fromString(
            before.value("time_utc").toString(), Qt::ISODateWithMs);
        if (!stamp.isValid() || before.value("id").toString().isEmpty())
        {
            auto result = emptyResult(*page);
            result.insert("status", "error");
            result.insert("error", "invalid_cursor");
            deliver(page, result);
            return;
        }
        page->hasCursor = true;
        page->beforeMs = stamp.toMSecsSinceEpoch();
        page->beforeId = before.value("id").toString();
        page->untilMs = page->beforeMs;
    }
    if (isRefused(page->userId, page->channel))
    {
        page->refused = true;
        finish(page);
        return;
    }

    // Mirrors that recently failed are skipped, unless all of them did.
    const auto count = size_t(mirrors().size());
    std::vector<size_t> asked;
    for (size_t i = 0; i < count; ++i)
    {
        if (host(i).coldUntil <= nowMs())
        {
            asked.push_back(i);
        }
    }
    if (asked.empty())
    {
        for (size_t i = 0; i < count; ++i)
        {
            asked.push_back(i);
        }
    }
    page->waitingOn.assign(count, false);
    page->pending = asked.size();
    for (auto i : asked)
    {
        page->waitingOn[i] = true;
    }
    for (auto i : asked)
    {
        ask(page, i);
    }
}

void setMirrorsForTesting(const QStringList &list, int spacingMs)
{
    mirrors() = list;
    hosts().clear();
    hosts().resize(size_t(list.size()));
    refusedUsers().clear();
    refusedChannels().clear();
    SPACING_MS = spacingMs;
}

}  // namespace chatterino::publiclogs
