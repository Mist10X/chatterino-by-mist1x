#include "providers/archive/ArchiveClient.hpp"

#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace chatterino {
namespace {
bool validEndpoint(const ArchiveEndpoint &endpoint)
{
    static const QRegularExpression socketPattern(
        QStringLiteral("\\A[A-Za-z0-9_.-]{1,128}\\z"));
    static const QRegularExpression datasetPattern(
        QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}\\z"));
    return socketPattern.match(endpoint.socketName).hasMatch() &&
           datasetPattern.match(endpoint.datasetId).hasMatch();
}

bool timestamp(const QJsonValue &value)
{
    const auto number = value.toDouble(-1);
    return value.isDouble() && std::isfinite(number) && number >= 0 &&
           number <= 253402300799999.0 && std::floor(number) == number;
}

bool validCard(const QJsonObject &data, const QString &uid)
{
    if (!QStringList{"found", "empty", "partial"}.contains(
            data.value("state").toString()) ||
        !timestamp(data.value("generated_at_ms")) ||
        !data.value("truncated").isBool())
        return false;
    const auto coverage = data.value("coverage");
    if (!coverage.isObject() ||
        !QStringList{"unknown", "observed", "not_covered"}.contains(
            coverage.toObject().value("status").toString()))
        return false;
    int contextCount = 0;
    auto contexts = [&](const QJsonValue &value) {
        if (!value.isArray())
            return false;
        for (const auto &raw : value.toArray())
        {
            if (++contextCount > 64 || !raw.isObject())
                return false;
            const auto message = raw.toObject();
            if (!message.value("text").isString() ||
                !timestamp(message.value("at_ms")) ||
                !message.value("automod").isBool())
                return false;
            if (!message.value("user_id").toString().isEmpty() &&
                message.value("user_id").toString() != uid)
                return false;
        }
        return true;
    };
    for (const auto &key : {"messages", "moderation", "dossier"})
    {
        if (!data.value(key).isObject())
            return false;
        const auto block = data.value(key).toObject();
        if (!QStringList{"found", "empty", "unavailable", "unsupported",
                         "unsupported_uid", "not_covered"}
                 .contains(block.value("status").toString()) ||
            !block.value("rows").isArray() || !block.value("more").isBool())
            return false;
        const auto total = block.value("total");
        if (!total.isNull() &&
            (!total.isDouble() || total.toDouble() < 0 ||
             total.toDouble() > 9007199254740991.0 ||
             std::floor(total.toDouble()) != total.toDouble()))
            return false;
        const auto rows = block.value("rows").toArray();
        if (rows.size() > (QString::fromLatin1(key) == u"dossier" ? 8 : 20))
            return false;
        for (const auto &raw : rows)
        {
            if (!raw.isObject())
                return false;
            const auto row = raw.toObject();
            if (!row.value("channel").isString() ||
                !timestamp(row.value("at_ms")))
                return false;
            if (QString::fromLatin1(key) != u"dossier" &&
                row.value("user_id").toString() != uid)
                return false;
            if (QString::fromLatin1(key) == u"messages")
            {
                if (!row.value("text").isString() ||
                    !row.value("id").isString())
                    return false;
            }
            else if (!contexts(row.value("context")))
                return false;
            if (QString::fromLatin1(key) == u"dossier" &&
                !timestamp(row.value("fetched_at_ms")))
                return false;
            if (QString::fromLatin1(key) == u"moderation")
            {
                if (!row.value("kind").isString())
                    return false;
                const auto receipt = row.value("receipt");
                if (receipt.isObject() &&
                    !timestamp(receipt.toObject().value("checked_at_ms")))
                    return false;
            }
        }
    }
    return true;
}

bool validCursor(const QJsonValue &value)
{
    if (!value.isObject())
        return false;
    const auto object = value.toObject();
    static const QRegularExpression id(QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}\\z"));
    const auto stamp = object.value("time_utc").toString();
    return object.size() == 2 && stamp.size() <= 64 &&
           QDateTime::fromString(stamp, Qt::ISODateWithMs).isValid() &&
           id.match(object.value("id").toString()).hasMatch();
}

bool validHistory(const QJsonObject &data, const QJsonObject &request)
{
    const auto uid = request.value("user_id").toString();
    const auto channel = request.value("channel").toString();
    const auto source = request.value("source") == "public" ? "public_logs" : "local_archive";
    const auto rowsValue = data.value("rows");
    if (data.value("channel").toString() != channel ||
        data.value("source").toString() != source ||
        data.value("scope").toString() != "channel" ||
        !QStringList{"found", "empty", "end", "loading", "error", "disabled", "refused",
                     "unavailable", "unsupported_uid"}.contains(data.value("status").toString()) ||
        !rowsValue.isArray() || rowsValue.toArray().size() > 50 ||
        !data.value("more").isBool() || !data.value("truncated").isBool())
        return false;
    const auto more = data.value("more").toBool();
    const auto cursor = data.value("next_cursor");
    if ((more && !validCursor(cursor)) || (!more && !cursor.isNull()))
        return false;
    QSet<QString> seen;
    QString previousTime, previousId;
    for (const auto &raw : rowsValue.toArray())
    {
        if (!raw.isObject())
            return false;
        const auto row = raw.toObject();
        const auto id = row.value("id").toString();
        const auto time = row.value("time_utc").toString();
        if (row.value("user_id").toString() != uid ||
            row.value("channel").toString() != channel || id.isEmpty() ||
            seen.contains(id) || !row.value("text").isString() ||
            !timestamp(row.value("at_ms")) ||
            !QDateTime::fromString(time, Qt::ISODateWithMs).isValid() ||
            (!previousTime.isEmpty() &&
             (time > previousTime || (time == previousTime && id >= previousId))))
            return false;
        if (request.value("before").isObject())
        {
            const auto before = request.value("before").toObject();
            const auto beforeTime = before.value("time_utc").toString();
            if (time > beforeTime ||
                (time == beforeTime && id >= before.value("id").toString()))
                return false;
        }
        seen.insert(id);
        previousTime = time;
        previousId = id;
    }
    if (more && request.value("before").isObject())
    {
        const auto old = request.value("before").toObject();
        const auto next = cursor.toObject();
        if (next.value("time_utc").toString() > old.value("time_utc").toString() ||
            (next.value("time_utc") == old.value("time_utc") &&
             next.value("id").toString() >= old.value("id").toString()))
            return false;
    }
    return true;
}
}  // namespace

ArchiveEndpoint readArchiveEndpointFile(const QString &path)
{
    constexpr qint64 maxBytes = 4096;
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() <= 0 ||
        info.size() > maxBytes)
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto bytes = file.read(maxBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.isEmpty() ||
        bytes.size() > maxBytes || !file.atEnd())
        return {};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    const auto object = document.object();
    if (object.value("protocol").toString() != QStringLiteral("archive-card") ||
        !object.value("version").isDouble() ||
        object.value("version").toDouble() != ArchiveClient::ProtocolVersion ||
        !object.value("socket_name").isString() ||
        !object.value("dataset_id").isString())
        return {};
    ArchiveEndpoint endpoint{object.value("socket_name").toString(),
                             object.value("dataset_id").toString()};
    return validEndpoint(endpoint) ? endpoint : ArchiveEndpoint{};
}

int ArchiveClient::activeRequests_ = 0;

ArchiveClient::ArchiveClient(ArchiveEndpoint endpoint, QObject *parent)
    : QObject(parent)
    , endpoint_(std::move(endpoint))
{
    this->deadline_.setSingleShot(true);
    this->historyPoll_.setSingleShot(true);
    QObject::connect(&this->historyPoll_, &QTimer::timeout, this, [this] {
        if (this->phase_ == Phase::Operation && this->operation_.value("op") == "history")
            this->send(this->operation_);
    });
    QObject::connect(&this->deadline_, &QTimer::timeout, this, [this] {
        this->fail(QStringLiteral("timeout"));
    });
}

ArchiveClient::~ArchiveClient()
{
    this->cancel();
}

bool isValidArchiveEndpoint(const ArchiveEndpoint &endpoint)
{
    return validEndpoint(endpoint);
}

bool ArchiveClient::configured() const
{
    return validEndpoint(this->endpoint_);
}

bool ArchiveClient::validTarget(const QJsonObject &request) const
{
    static const QRegularExpression uid(QStringLiteral("^[0-9]{1,25}$"));
    static const QRegularExpression login(QStringLiteral("^[a-z0-9_]{1,25}$"));
    static const QRegularExpression message(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}$"));
    const auto name = request.value("user").toString();
    const auto channel = request.value("channel").toString();
    const auto messageId = request.value("message_id").toString();
    return uid.match(request.value("user_id").toString()).hasMatch() &&
           login.match(name).hasMatch() &&
           (channel.isEmpty() || login.match(channel).hasMatch()) &&
           (messageId.isEmpty() || message.match(messageId).hasMatch());
}

void ArchiveClient::requestCard(const QString &userId, const QString &login,
                                const QString &channel)
{
    this->begin({{"op", "card"},
                 {"user_id", userId},
                 {"user", login.toLower()},
                 {"channel", channel.toLower()}});
}

void ArchiveClient::requestHistory(const QString &userId, const QString &login,
                                   const QString &channel,
                                   const QJsonObject &before, bool publicLogs)
{
    if (channel.isEmpty() || (!before.isEmpty() && !validCursor(before)))
    {
        this->cancel();
        this->fail(QStringLiteral("invalid_request"));
        return;
    }
    QJsonObject request{{"op", "history"}, {"user_id", userId},
                        {"user", login.toLower()}, {"channel", channel.toLower()},
                        {"source", publicLogs ? "public" : "local"}};
    if (!before.isEmpty())
    {
        auto cursor = before;
        if (publicLogs)
            cursor.insert("time_utc", QDateTime::fromString(
                before.value("time_utc").toString(), Qt::ISODateWithMs)
                .toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'")));
        request.insert("before", cursor);
    }
    this->begin(std::move(request));
}

void ArchiveClient::openView(const QString &view, const QString &userId,
                             const QString &login, const QString &channel,
                             const QString &messageId)
{
    static const QStringList allowed{"user", "moderation", "message", "context",
                                     "archive"};
    if (!allowed.contains(view) ||
        ((view == "message" || view == "context") && messageId.isEmpty()))
    {
        this->cancel();
        this->fail(QStringLiteral("invalid_request"));
        return;
    }
    this->begin({{"op", "open"},
                 {"view", view},
                 {"user_id", userId},
                 {"user", login.toLower()},
                 {"channel", channel.toLower()},
                 {"message_id", messageId}});
}

void ArchiveClient::cancel()
{
    ++this->generation_;
    this->deadline_.stop();
    this->historyPoll_.stop();
    this->phase_ = Phase::Idle;
    this->buffer_.clear();
    if (this->socket_)
    {
        auto *socket = this->socket_.data();
        this->socket_ = nullptr;
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    if (this->active_)
    {
        this->active_ = false;
        --activeRequests_;
    }
}

void ArchiveClient::begin(QJsonObject request)
{
    this->cancel();
    if (!this->configured())
    {
        this->fail(QStringLiteral("not_configured"));
        return;
    }
    if (!this->validTarget(request))
    {
        this->fail(QStringLiteral("identity_required"));
        return;
    }
    if (activeRequests_ >= 4)
    {
        this->fail(QStringLiteral("busy"));
        return;
    }
    this->active_ = true;
    ++activeRequests_;
    this->operation_ = std::move(request);
    this->phase_ = Phase::Hello;
    const auto generation = this->generation_;
    auto *socket = new QLocalSocket(this);
    this->socket_ = socket;
    socket->setReadBufferSize(MaxResponseBytes + 1);
    QObject::connect(socket, &QLocalSocket::connected, this,
                     [this, generation] {
                         if (generation == this->generation_)
                         {
                             this->send({{"op", "hello"}});
                         }
                     });
    QObject::connect(socket, &QLocalSocket::readyRead, this,
                     [this, generation] {
                         this->read(generation);
                     });
    QObject::connect(socket, &QLocalSocket::errorOccurred, this,
                     [this, generation](QLocalSocket::LocalSocketError error) {
                         if (generation == this->generation_)
                         {
                             if (error == QLocalSocket::PeerClosedError)
                             {
                                 this->read(generation);
                                 if (generation != this->generation_)
                                     return;
                             }
                             this->fail(QStringLiteral("unavailable"));
                         }
                     });
    QObject::connect(socket, &QLocalSocket::disconnected, this,
                     [this, generation] {
                         if (generation == this->generation_)
                         {
                             // Consume a final complete frame before classifying EOF.
                             this->read(generation);
                             if (generation == this->generation_)
                             {
                                 this->fail(QStringLiteral("unavailable"));
                             }
                         }
                     });
    this->deadline_.start(this->operation_.value("op") == "history" &&
                                  this->operation_.value("source") == "public"
                              ? 25000
                              : std::clamp(this->endpoint_.timeoutMs, 20, 5000));
    socket->connectToServer(this->endpoint_.socketName, QIODevice::ReadWrite);
}

void ArchiveClient::send(QJsonObject request)
{
    request.insert("id", ++this->sequence_);
    request.insert("protocol", "archive-card");
    request.insert("version", ProtocolVersion);
    request.insert("expected_dataset_id", this->endpoint_.datasetId);
    const auto bytes =
        QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > MaxRequestBytes || !this->socket_)
    {
        this->fail(QStringLiteral("invalid_request"));
        return;
    }
    this->expectedId_ = this->sequence_;
    if (this->socket_->write(bytes) != bytes.size())
    {
        this->fail(QStringLiteral("unavailable"));
    }
}

void ArchiveClient::read(quint64 generation)
{
    if (generation != this->generation_ || !this->socket_)
    {
        return;
    }
    this->buffer_.append(this->socket_->readAll());
    if (this->buffer_.size() > MaxResponseBytes)
    {
        this->fail(QStringLiteral("response_too_large"));
        return;
    }
    const auto end = this->buffer_.indexOf('\n');
    if (end < 0)
    {
        return;
    }
    const auto line = this->buffer_.left(end);
    this->buffer_.remove(0, end + 1);
    // Exactly one reply for the pending operation; unsolicited frames cannot
    // be mistaken for the next handshake stage.
    if (!this->buffer_.trimmed().isEmpty())
    {
        this->fail(QStringLiteral("malformed"));
        return;
    }
    this->frame(line);
}

void ArchiveClient::frame(const QByteArray &line)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        this->fail(QStringLiteral("malformed"));
        return;
    }
    const auto reply = document.object();
    if (!reply.value("id").isDouble() ||
        reply.value("id").toDouble() != this->expectedId_ ||
        !reply.value("ok").isBool())
    {
        this->fail(QStringLiteral("malformed"));
        return;
    }
    if (!reply.value("ok").toBool())
    {
        // Never display raw server exceptions/paths or arbitrary HTML.
        const auto code = reply.value("error").toString("error");
        this->fail(this->phase_ == Phase::Hello &&
                           (code == "unknown op" || code == "no user")
                       ? QStringLiteral("protocol_mismatch")
                       : code);
        return;
    }
    if (!reply.value("data").isObject())
    {
        this->fail(QStringLiteral("malformed"));
        return;
    }
    const auto data = reply.value("data").toObject();
    if (data.value("protocol").toString() != "archive-card" ||
        data.value("version").toInt(-1) != ProtocolVersion)
    {
        this->fail(QStringLiteral("protocol_mismatch"));
        return;
    }
    if (data.value("dataset_id").toString() != this->endpoint_.datasetId)
    {
        this->fail(QStringLiteral("dataset_mismatch"));
        return;
    }
    if (this->phase_ == Phase::Hello)
    {
        this->phase_ = Phase::Operation;
        this->send(this->operation_);
        return;
    }
    if (this->phase_ != Phase::Operation ||
        data.value("identity").toObject().value("user_id").toString() !=
            this->operation_.value("user_id").toString())
    {
        this->fail(QStringLiteral("identity_mismatch"));
        return;
    }
    const auto op = this->operation_.value("op").toString();
    const auto view = this->operation_.value("view").toString();
    if (op == "card" &&
        !validCard(data, this->operation_.value("user_id").toString()))
    {
        this->fail(QStringLiteral("malformed"));
        return;
    }
    if (op == "history")
    {
        if (!validHistory(data, this->operation_))
        {
            this->fail(QStringLiteral("malformed"));
            return;
        }
        if (data.value("status") == "loading")
        {
            if (this->operation_.value("source") != "public")
            {
                this->fail(QStringLiteral("malformed"));
                return;
            }
            this->historyPoll_.start(250);
            Q_EMIT this->historyLoading();
            return;
        }
    }
    if (op == "open" && (!data.value("opened").toBool() ||
                         data.value("view").toString() != view))
    {
        this->fail(QStringLiteral("not_opened"));
        return;
    }
    this->cancel();
    if (op == "card")
    {
        Q_EMIT this->cardReady(data);
    }
    else if (op == "history")
    {
        Q_EMIT this->historyReady(data);
    }
    else
    {
        Q_EMIT this->viewOpened(view);
    }
}

void ArchiveClient::fail(const QString &code)
{
    this->cancel();
    Q_EMIT this->failed(code, userFacingError(code));
}

QString ArchiveClient::userFacingError(const QString &code)
{
    if (code == "not_configured")
        return QStringLiteral("Локальное подключение к Архиву не настроено.");
    if (code == "timeout")
        return QStringLiteral("Архив не ответил вовремя.");
    if (code == "dataset_mismatch")
        return QStringLiteral(
            "Подключён другой набор данных Архива. Запрос отменён.");
    if (code == "protocol_mismatch" || code == "unsupported_version")
        return QStringLiteral("Версия локального API Архива несовместима.");
    if (code == "identity_required" || code == "identity_mismatch")
        return QStringLiteral("Не удалось подтвердить Twitch ID пользователя.");
    if (code == "busy")
        return QStringLiteral("Архив занят. Повторите действие позже.");
    if (code == "unavailable" || code == "source_unavailable")
        return QStringLiteral(
            "Архив недоступен или нужные данные не подготовлены.");
    if (code == "not_opened" || code == "unsupported_view" ||
        code == "navigation_failed")
        return QStringLiteral(
            "Архив не подтвердил открытие сохранённых данных.");
    if (code == "message_not_found")
        return QStringLiteral("Это сообщение не найдено среди сохранённых "
                              "записей этого Twitch ID.");
    if (code == "message_ambiguous")
        return QStringLiteral(
            "Не удалось однозначно определить сохранённое сообщение.");
    return QStringLiteral("Получен некорректный ответ Архива.");
}
}  // namespace chatterino
