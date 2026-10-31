// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/Mist1XMark.hpp"

#include "Application.hpp"
#include "common/Mist1XVersion.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/Emote.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "messages/Image.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QMutex>
#include <QPainter>
#include <QPixmap>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QThread>
#include <QTimer>

namespace chatterino::mist1xmark {

namespace {

constexpr qint64 KEEP_SECS = 60LL * 24 * 60 * 60;

QString signature(QStringView head)
{
    // Not a secret: a fixed word keeps random web nonces from matching.
    const auto digest = QCryptographicHash::hash(
        QStringLiteral("chatterino-by-mist1x:%1").arg(head).toUtf8(),
        QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex().left(8));
}

constexpr int IDS_EVERY_MS = 30 * 60 * 1000;
constexpr int HELLO_EVERY_MS = 24 * 60 * 60 * 1000;

struct Known {
    QMutex mutex;
    QHash<QString, qint64> lastSeen;  // user ID -> seconds since epoch
    QSet<QString> online;             // from the service's list
    bool loaded = false;
    bool saveQueued = false;
    bool disk = true;
};

Known &known()
{
    static Known value;
    return value;
}

QString filePath()
{
    auto *app = tryGetApp();
    if (app == nullptr)
    {
        return {};
    }
    return QDir(app->getPaths().miscDirectory)
        .filePath(QStringLiteral("mist1x-users.json"));
}

// with the mutex held
void loadLocked(Known &k)
{
    if (k.loaded)
    {
        return;
    }
    k.loaded = true;
    const auto path = k.disk ? filePath() : QString();
    if (path.isEmpty())
    {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }
    const auto now = QDateTime::currentSecsSinceEpoch();
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        const auto seen = qint64(it.value().toDouble());
        if (now - seen < KEEP_SECS)
        {
            k.lastSeen.insert(it.key(), seen);
        }
    }
}

void save()
{
    auto &k = known();
    QJsonObject root;
    {
        QMutexLocker lock(&k.mutex);
        k.saveQueued = false;
        for (auto it = k.lastSeen.begin(); it != k.lastSeen.end(); ++it)
        {
            root.insert(it.key(), double(it.value()));
        }
    }
    const auto path = filePath();
    if (path.isEmpty())
    {
        return;
    }
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

// QPixmap is made on the GUI thread only (messages are also built elsewhere).
EmotePtr makeBadge()
{
    QPixmap icon(QStringLiteral(":/icon.png"));
    auto sized = [&](int side) {
        return icon.isNull() ? QPixmap()
                             : icon.scaled(side, side, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
    };
    Emote emote;
    emote.name = EmoteName{QStringLiteral("Chatterino by Mist1X")};
    emote.images = ImageSet(Image::fromResourcePixmap(sized(18), 1),
                            Image::fromResourcePixmap(sized(36), 0.5),
                            Image::fromResourcePixmap(sized(72), 0.25));
    emote.tooltip = Tooltip{QStringLiteral("Chatterino by Mist1X")};
    emote.homePage =
        Url{QStringLiteral("https://github.com/Mist10X/chatterino-by-mist1x")};
    return std::make_shared<const Emote>(std::move(emote));
}

QMutex &badgeMutex()
{
    static QMutex value;
    return value;
}

EmotePtr &badgeSlot()
{
    static EmotePtr value;
    return value;
}

/// The badge, or nullptr until it is made on the GUI thread.
EmotePtr badge()
{
    {
        QMutexLocker lock(&badgeMutex());
        if (badgeSlot())
        {
            return badgeSlot();
        }
    }
    auto *app = QCoreApplication::instance();
    if (app == nullptr)
    {
        return nullptr;
    }
    auto make = [] {
        auto made = makeBadge();
        QMutexLocker lock(&badgeMutex());
        if (!badgeSlot())
        {
            badgeSlot() = std::move(made);
        }
        return badgeSlot();
    };
    if (QThread::currentThread() == app->thread())
    {
        return make();
    }
    QMetaObject::invokeMethod(app, make, Qt::QueuedConnection);
    return nullptr;
}

}  // namespace

QString makeNonce()
{
    QString head;
    head.reserve(24);
    auto *random = QRandomGenerator::global();
    for (int i = 0; i < 3; ++i)
    {
        head += QStringLiteral("%1").arg(random->generate(), 8, 16,
                                         QLatin1Char('0'));
    }
    head = head.toLower();
    return head + signature(head);
}

bool isMarked(QStringView nonce)
{
    if (nonce.size() != 32)
    {
        return false;
    }
    for (auto c : nonce)
    {
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')))
        {
            return false;
        }
    }
    return nonce.sliced(24) == signature(nonce.first(24));
}

void remember(const QString &userID)
{
    if (userID.isEmpty())
    {
        return;
    }
    auto &k = known();
    bool queue = false;
    {
        QMutexLocker lock(&k.mutex);
        loadLocked(k);
        const auto now = QDateTime::currentSecsSinceEpoch();
        const auto previous = k.lastSeen.value(userID, 0);
        k.lastSeen.insert(userID, now);
        // a new person, or a day since the stored time: write it down
        if (k.disk && (previous == 0 || now - previous > 24 * 60 * 60) &&
            !k.saveQueued)
        {
            k.saveQueued = true;
            queue = true;
        }
    }
    if (queue && QCoreApplication::instance() != nullptr)
    {
        QTimer::singleShot(5000, QCoreApplication::instance(), [] {
            save();
        });
    }
}

bool isKnown(const QString &userID)
{
    if (userID.isEmpty())
    {
        return false;
    }
    auto &k = known();
    QMutexLocker lock(&k.mutex);
    loadLocked(k);
    return k.lastSeen.contains(userID) || k.online.contains(userID);
}

QString serviceUrl()
{
    return QStringLiteral("https://mist1x-users.mist1x-users-worker.workers.dev");
}

namespace {

void fetchIds()
{
    NetworkRequest(serviceUrl() + QStringLiteral("/v1/ids"))
        .timeout(15000)
        .onSuccess([](const NetworkResult &result) {
            QSet<QString> ids;
            for (const auto &value : result.parseJson().value("ids").toArray())
            {
                const auto id = value.toString();
                if (!id.isEmpty())
                {
                    ids.insert(id);
                }
            }
            auto &k = known();
            QMutexLocker lock(&k.mutex);
            k.online = std::move(ids);
        })
        .execute();
}

void sayHello()
{
    auto *app = tryGetApp();
    if (app == nullptr || !getSettings()->mist1xShareId)
    {
        return;
    }
    auto account = app->getAccounts()->twitch.getCurrent();
    if (!account || account->isAnon() || account->getUserId().isEmpty())
    {
        return;
    }
    // only the Twitch ID, login and version: no token
    NetworkRequest(serviceUrl() + QStringLiteral("/v1/hello"),
                   NetworkRequestType::Post)
        .json(QJsonObject{
            {"id", account->getUserId()},
            {"login", account->getUserName().toLower()},
            {"version", QStringLiteral(MIST1X_VERSION)},
        })
        .timeout(15000)
        .execute();
    // you have the badge yourself right away
    remember(account->getUserId());
}

}  // namespace

void startOnline()
{
    static bool started = false;
    auto *app = tryGetApp();
    if (started || app == nullptr || QCoreApplication::instance() == nullptr)
    {
        return;
    }
    started = true;
    fetchIds();
    sayHello();
    auto *ids = new QTimer(QCoreApplication::instance());
    QObject::connect(ids, &QTimer::timeout, [] {
        fetchIds();
    });
    ids->start(IDS_EVERY_MS);
    auto *hello = new QTimer(QCoreApplication::instance());
    QObject::connect(hello, &QTimer::timeout, [] {
        sayHello();
    });
    hello->start(HELLO_EVERY_MS);
    std::ignore = app->getAccounts()->twitch.currentUserChanged.connect([] {
        QTimer::singleShot(2000, QCoreApplication::instance(), [] {
            sayHello();
        });
    });
}

std::optional<EmotePtr> badgeFor(const QString &userID)
{
    if (!getSettings()->mist1xShowBadge || !isKnown(userID))
    {
        return std::nullopt;
    }
    if (auto value = badge())
    {
        return value;
    }
    return std::nullopt;
}

void resetForTesting()
{
    auto &k = known();
    QMutexLocker lock(&k.mutex);
    k.lastSeen.clear();
    k.loaded = true;
    k.disk = false;
}

}  // namespace chatterino::mist1xmark
