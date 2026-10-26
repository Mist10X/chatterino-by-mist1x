// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/network/NetworkTask.hpp"

#include "Application.hpp"
#include "common/network/NetworkManager.hpp"
#include "common/network/NetworkPrivate.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "singletons/Paths.hpp"
#include "util/AbandonObject.hpp"
#include "util/DebugCount.hpp"

#include <QFile>
#include <QNetworkReply>
#include <QtConcurrent>

#ifndef signals
#    define signals public
#endif
#include <private/qnetworkreplyhttpimpl_p.h>
#undef signals

namespace {

void forceCustomOperation(QNetworkReply *reply)
{
    auto *d = static_cast<QNetworkReplyPrivate *>(QObjectPrivate::get(reply));
    if (!d)
    {
        return;
    }
    d->operation = QNetworkAccessManager::CustomOperation;
}

}  // namespace

namespace chatterino::network::detail {

NetworkTask::NetworkTask(std::shared_ptr<NetworkData> &&data)
    : data_(std::move(data))
{
}

NetworkTask::~NetworkTask()
{
    if (this->reply_)
    {
        this->reply_->deleteLater();
    }
}

void NetworkTask::run()
{
    if (this->completed_)
    {
        return;
    }
    this->data_->startDeadline();
    if (this->data_->hasCaller && this->data_->caller.isNull())
    {
        this->completed_ = true;
        this->deleteLater();
        return;
    }
    if (this->data_->deadline)
    {
        if (this->data_->deadline->hasExpired())
        {
            this->timeout();
            return;
        }
        this->timer_ = new QTimer(this);
        this->timer_->setSingleShot(true);
        this->timer_->setTimerType(Qt::PreciseTimer);
        QObject::connect(this->timer_, &QTimer::timeout, this, &NetworkTask::timeout);
        this->timer_->start(std::chrono::milliseconds(this->data_->deadline->remainingTime()));
    }
    // The deadline is already running before the first proxy/TCP/TLS attempt.
    this->reply_ = this->createReply();
    if (!this->reply_)
    {
        this->completed_ = true;
        if (this->timer_) this->timer_->stop();
        this->data_->emitError({NetworkResult::NetworkError::UnknownNetworkError, {}, {}});
        this->data_->emitFinally();
        this->deleteLater();
        return;
    }
    QObject::connect(this->reply_, &QNetworkReply::finished, this, &NetworkTask::finished);
    if (this->data_->hasCaller)
    {
        if (this->data_->caller.isNull())
        {
            this->reply_->abort();
        }
        else
        {
            QObject::connect(this->data_->caller, &QObject::destroyed, this, [this] {
                if (!this->completed_ && this->reply_)
                {
                    this->reply_->abort();
                }
            });
        }
    }
#ifndef NDEBUG
    if (this->data_->ignoreSslErrors)
    {
        QObject::connect(this->reply_, &QNetworkReply::sslErrors, this,
                         [this](const auto &) { this->reply_->ignoreSslErrors(); });
    }
#endif
    if (this->data_->deadline && this->data_->deadline->hasExpired())
    {
        this->timeout();
    }
    else if (this->reply_->isFinished())
    {
        // Preserve single completion even for a reply completed during creation.
        QMetaObject::invokeMethod(this, &NetworkTask::finished, Qt::QueuedConnection);
    }
}

QNetworkReply *NetworkTask::createReply()
{
    const auto &data = this->data_;
    const auto &request = this->data_->request;
    auto *accessManager = NetworkManager::accessManager;
    switch (this->data_->requestType)
    {
        case NetworkRequestType::Get:
            return accessManager->get(request);

        case NetworkRequestType::Delete: {
            if (data->payload.isEmpty())
            {
                return accessManager->deleteResource(request);
            }

            auto *reply = accessManager->sendCustomRequest(request, "DELETE",
                                                           data->payload);
            if (!reply)
            {
                return reply;
            }
            forceCustomOperation(reply);
            return reply;
        }

        case NetworkRequestType::Put:
            if (data->multiPartPayload)
            {
                assert(data->payload.isNull());

                return accessManager->put(request,
                                          data->multiPartPayload.get());
            }
            else
            {
                assert(data->multiPartPayload == nullptr);

                return accessManager->put(request, data->payload);
            }

        case NetworkRequestType::Post:
            if (data->multiPartPayload)
            {
                assert(data->payload.isNull());

                return accessManager->post(request,
                                           data->multiPartPayload.get());
            }
            else
            {
                assert(data->multiPartPayload == nullptr);

                return accessManager->post(request, data->payload);
            }

        case NetworkRequestType::Patch:
            if (data->multiPartPayload)
            {
                assert(data->payload.isNull());

                return accessManager->sendCustomRequest(
                    request, "PATCH", data->multiPartPayload.get());
            }
            else
            {
                assert(data->multiPartPayload == nullptr);

                return NetworkManager::accessManager->sendCustomRequest(
                    request, "PATCH", data->payload);
            }
    }
    return nullptr;
}

void NetworkTask::logReply()
{
    auto status =
        this->reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute)
            .toInt();
    if (this->data_->requestType == NetworkRequestType::Get)
    {
        qCDebug(chatterinoHTTP).noquote()
            << this->data_->typeString() << status
            << this->data_->request.url().toString();
    }
    else
    {
        QUtf8StringView payload = this->data_->payload;
#ifdef NDEBUG
        if (this->data_->hideRequestBody)
#else
        static bool alwaysShowRequestBodies =
            qEnvironmentVariableIntValue(
                "CHATTERINO_HTTP_ALWAYS_SHOW_REQUEST_BODY") != 0;
        if (this->data_->hideRequestBody && !alwaysShowRequestBodies)
#endif
        {
            payload = "(redacted)";
        }
        qCDebug(chatterinoHTTP).noquote()
            << this->data_->typeString()
            << this->data_->request.url().toString() << status << payload;
    }
}

void NetworkTask::writeToCache(const QByteArray &bytes) const
{
    std::ignore = QtConcurrent::run([data = this->data_, bytes] {
        if (isAppAboutToQuit())
        {
            qCDebug(chatterinoHTTP)
                << "Skipping cache write for" << data->request.url()
                << "because app is about to quit";
            return;
        }

        auto *app = tryGetApp();
        if (!app)
        {
            qCDebug(chatterinoHTTP)
                << "Skipping cache write for" << data->request.url()
                << "because app is null";
            return;
        }

        QFile cachedFile(app->getPaths().cacheDirectory() + "/" +
                         data->getHash());

        if (cachedFile.open(QIODevice::WriteOnly))
        {
            cachedFile.write(bytes);
        }
    });
}

void NetworkTask::timeout()
{
    if (this->completed_) return;
    this->completed_ = true;
    AbandonObject guard(this);
    if (this->timer_) this->timer_->stop();
    if (this->reply_)
    {
        QObject::disconnect(this->reply_, &QNetworkReply::finished, this, &NetworkTask::finished);
        this->reply_->abort();
    }
    // Do not log URLs, query strings, headers or payloads for transport failures.
    qCDebug(chatterinoHTTP) << this->data_->typeString() << "[timed out]";
    this->data_->emitError({NetworkResult::NetworkError::TimeoutError, {}, {}});
    this->data_->emitFinally();
}

void NetworkTask::finished()
{
    if (this->completed_) return;
    this->completed_ = true;
    AbandonObject guard(this);

    if (this->timer_)
    {
        this->timer_->stop();
    }

    auto *reply = this->reply_;
    auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    auto etag = reply->rawHeader("ETag");

    if (reply->error() != QNetworkReply::NoError)
    {
        if (reply->error() == QNetworkReply::OperationCanceledError)
        {
            qCDebug(chatterinoHTTP) << this->data_->typeString() << "[cancelled]";
        }
        else
        {
            this->logReply();
        }
        this->data_->emitError(
            {reply->error(), status, reply->readAll(), std::move(etag)});
        this->data_->emitFinally();

        return;
    }

    QByteArray bytes = reply->readAll();

    if (this->data_->cache)
    {
        this->writeToCache(bytes);
    }

    DebugCount::increase(DebugObject::HTTPRequestSuccess);
    this->logReply();
    this->data_->emitSuccess({reply->error(), status, bytes, std::move(etag)});
    this->data_->emitFinally();
}

}  // namespace chatterino::network::detail
