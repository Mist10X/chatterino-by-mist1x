// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <pajlada/signals/signal.hpp>
#include <QHash>
#include <QMimeData>
#include <QMutex>
#include <QString>
#include <QStringView>

#include <functional>
#include <memory>
#include <queue>

namespace chatterino {

class ResizingTextEdit;
class Channel;
class NetworkResult;
using ChannelPtr = std::shared_ptr<Channel>;

}  // namespace chatterino

namespace chatterino::imageuploader::detail {

QString getJSONValue(QJsonValue responseJson, QStringView jsonPattern);

QString getLinkFromResponse(const NetworkResult &response, QString pattern);

}  // namespace chatterino::imageuploader::detail

namespace chatterino {

struct RawImageData {
    QByteArray data;
    QString format;
    QString filePath;
    QString mimeType;
};

/// What the preview above the input shows while a picture is uploaded.
struct ImageUploadUpdate {
    enum class Kind { Started, Finished, Failed };

    Kind kind = Kind::Started;
    /// The input the link goes into.
    ResizingTextEdit *textEdit = nullptr;
    /// Started: the picture itself.
    QByteArray imageData;
    /// Finished.
    QString link;
    QString deletionLink;
    /// Started: pictures still waiting after this one.
    size_t queued = 0;
};

class ImageUploader final
{
public:
    enum class DeleteResult { Deleted, OpenedInBrowser, Failed };

    std::pair<std::queue<RawImageData>, QString> getImages(
        const QMimeData *source) const;

    void upload(std::queue<RawImageData> images, ChannelPtr channel,
                QPointer<ResizingTextEdit> outputTextEdit);

    /// The deletion link saved for a picture uploaded from here (also in an
    /// earlier session), or empty.
    QString deletionLinkFor(const QString &imageLink);

    /// Deletes a picture uploaded from here. kappa.lol and its sister sites
    /// are asked directly; for other sites the deletion page is opened in
    /// the browser. `done` gets an error text on failure (GUI thread).
    void deleteImage(const QString &imageLink,
                     std::function<void(DeleteResult, QString)> done);

    /// Upload progress for the preview above the input (GUI thread).
    pajlada::Signals::Signal<const ImageUploadUpdate &> updated;

private:
    QString logFilePath() const;
    void loadDeletionLinks();
    void markDeleted(const QString &imageLink);

    void sendImageUploadRequest(RawImageData imageData, ChannelPtr channel,
                                QPointer<ResizingTextEdit> textEdit);

    void handleSuccessfulUpload(const NetworkResult &result,
                                QString originalFilePath, ChannelPtr channel,
                                QPointer<ResizingTextEdit> textEdit);
    void handleFailedUpload(const NetworkResult &result, ChannelPtr channel,
                            QPointer<ResizingTextEdit> textEdit);

    void logToFile(const QString &originalFilePath, const QString &imageLink,
                   const QString &deletionLink, ChannelPtr channel);

    QMutex uploadMutex_;
    std::queue<RawImageData> uploadQueue_;
    /// imagepreview::key of the picture -> its deletion link
    QHash<QString, QString> deletionLinks_;
    bool deletionLinksLoaded_ = false;
};
}  // namespace chatterino
