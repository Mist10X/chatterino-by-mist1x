// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/UploadPreview.hpp"

#include "Application.hpp"
#include "common/UiLanguage.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/ImageUploader.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "widgets/helper/ResizingTextEdit.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace chatterino {

namespace {

constexpr int THUMB_HEIGHT = 56;
constexpr int MAX_WIDTH = 420;

QString siteOf(const QString &link)
{
    auto host = QUrl(link).host();
    if (host.startsWith(QStringLiteral("www.")))
    {
        host = host.mid(4);
    }
    return host.isEmpty() ? link : host;
}

}  // namespace

UploadPreview::UploadPreview(QWidget *view, ResizingTextEdit *textEdit,
                             std::function<void(const QString &)> notify)
    : QFrame(view)
    , view_(view)
    , textEdit_(textEdit)
    , notify_(std::move(notify))
    , thumbnail_(new QLabel)
    , title_(new QLabel)
    , detail_(new QLabel)
    , delete_(new QPushButton(uiText("Delete")))
    , close_(new QPushButton(QStringLiteral("×")))
{
    this->setObjectName("uploadPreview");
    // clicks on the card must not start a text selection in the chat under it
    this->setAttribute(Qt::WA_NoMousePropagation);
    this->hide();

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(8, 6, 6, 6);
    row->setSpacing(10);
    this->thumbnail_->setObjectName("uploadPreviewThumb");
    this->thumbnail_->setAlignment(Qt::AlignCenter);
    row->addWidget(this->thumbnail_);

    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->addStretch(1);
    this->title_->setObjectName("uploadPreviewTitle");
    this->detail_->setObjectName("uploadPreviewDetail");
    this->title_->setTextFormat(Qt::PlainText);
    this->detail_->setTextFormat(Qt::PlainText);
    this->detail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // place() cuts the texts to the room there is
    this->title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    this->detail_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    texts->addWidget(this->title_);
    texts->addWidget(this->detail_);
    texts->addStretch(1);
    row->addLayout(texts, 1);

    this->delete_->setObjectName("uploadPreviewDelete");
    this->delete_->setCursor(Qt::PointingHandCursor);
    this->delete_->setToolTip(
        uiText("Delete the picture from the site and its link from the input"));
    QObject::connect(this->delete_, &QPushButton::clicked, this, [this] {
        this->deleteImage();
    });
    row->addWidget(this->delete_, 0, Qt::AlignVCenter);

    this->close_->setObjectName("uploadPreviewClose");
    this->close_->setCursor(Qt::PointingHandCursor);
    this->close_->setToolTip(uiText("Close"));
    QObject::connect(this->close_, &QPushButton::clicked, this, [this] {
        this->close();
    });
    row->addWidget(this->close_, 0, Qt::AlignTop);

    if (textEdit != nullptr)
    {
        QObject::connect(textEdit, &QTextEdit::textChanged, this, [this] {
            this->onTextChanged();
        });
    }
    view->installEventFilter(this);
}

void UploadPreview::apply(const ImageUploadUpdate &update)
{
    using Kind = ImageUploadUpdate::Kind;
    switch (update.kind)
    {
        case Kind::Started: {
            QPixmap pixmap;
            pixmap.loadFromData(update.imageData);
            const auto ratio = this->devicePixelRatioF();
            const int height = int(THUMB_HEIGHT * ratio);
            if (!pixmap.isNull())
            {
                pixmap = pixmap.scaled(int(height * 16 / 9), height,
                                       Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
                pixmap.setDevicePixelRatio(ratio);
            }
            this->thumbnail_->setPixmap(pixmap);
            this->thumbnail_->setVisible(!pixmap.isNull());
            this->link_.clear();
            this->deleting_ = false;
            this->delete_->hide();
            this->setStatus(
                uiText("Uploading the picture..."),
                update.queued > 0
                    ? uiText("%1 more after it").arg(update.queued)
                    : QString());
            this->updateStyle();
            this->show();
            this->place();
            this->raise();
            break;
        }
        case Kind::Finished:
            this->link_ = update.link;
            this->delete_->setEnabled(true);
            this->delete_->show();
            this->setStatus(
                uiText("Uploaded to %1").arg(siteOf(update.link)),
                update.link);
            this->show();
            this->place();
            this->raise();
            break;
        case Kind::Failed:
            this->close();
            break;
    }
}

void UploadPreview::setStatus(const QString &title, const QString &detail)
{
    const auto scale = getSettings()->getClampedUiScale();
    auto *fonts = getApp()->getFonts();
    this->title_->setFont(fonts->getFont(FontStyle::ChatMediumBold, scale));
    this->detail_->setFont(fonts->getFont(FontStyle::ChatMediumSmall, scale));
    this->titleText_ = title;
    this->title_->setText(title);
    this->detailText_ = detail;
    this->detail_->setText(detail);
    this->detail_->setToolTip(detail);
    this->detail_->setVisible(!detail.isEmpty());
    this->place();
}

void UploadPreview::deleteImage()
{
    if (this->link_.isEmpty() || this->deleting_)
    {
        return;
    }
    this->deleting_ = true;
    this->delete_->setEnabled(false);
    this->setStatus(uiText("Deleting the picture..."), this->link_);
    QPointer<UploadPreview> self(this);
    const auto link = this->link_;
    getApp()->getImageUploader()->deleteImage(
        link, [self, link](ImageUploader::DeleteResult result,
                           const QString &error) {
            if (!self)
            {
                return;
            }
            self->deleting_ = false;
            using Result = ImageUploader::DeleteResult;
            if (result == Result::Failed)
            {
                self->delete_->setEnabled(true);
                self->setStatus(uiText("Could not delete: %1").arg(error),
                                link);
                return;
            }
            self->removeLinkFromInput();
            self->close();
            self->notify_(
                result == Result::Deleted
                    ? uiText("The picture %1 was deleted from %2")
                          .arg(link, siteOf(link))
                    : uiText("The deletion page of %1 was opened in the "
                             "browser")
                          .arg(link));
        });
}

void UploadPreview::removeLinkFromInput()
{
    if (!this->textEdit_ || this->link_.isEmpty())
    {
        return;
    }
    auto text = this->textEdit_->toPlainText();
    const auto at = text.indexOf(this->link_);
    if (at < 0)
    {
        return;
    }
    auto length = this->link_.size();
    if (at + length < text.size() && text[at + length] == u' ')
    {
        ++length;
    }
    text.remove(at, length);
    const auto link = std::exchange(this->link_, QString());
    this->textEdit_->setPlainText(text);
    this->textEdit_->moveCursor(QTextCursor::End);
    this->link_ = link;
}

void UploadPreview::onTextChanged()
{
    // The link left the input: sent, erased or replaced. The picture can
    // still be deleted from the chat's menu on the link.
    if (!this->link_.isEmpty() && !this->deleting_ && this->textEdit_ &&
        !this->textEdit_->toPlainText().contains(this->link_))
    {
        this->close();
    }
}

void UploadPreview::close()
{
    this->link_.clear();
    this->deleting_ = false;
    this->hide();
}

void UploadPreview::updateStyle()
{
    const auto accent = getTheme()->accent.name();
    if (getTheme()->isLightTheme())
    {
        this->setStyleSheet(
            QStringLiteral(
                "QFrame#uploadPreview { background: #f6f3fb; border: 1px "
                "solid rgba(0, 0, 0, 0.14); border-left: 4px solid %1; "
                "border-radius: 8px; }"
                "QLabel { color: #2b2340; background: transparent; border: "
                "none; }"
                "QLabel#uploadPreviewDetail { color: #6b6478; }"
                "QPushButton#uploadPreviewDelete { color: #b4232f; "
                "background: rgba(220, 40, 60, 0.10); border: 1px solid "
                "rgba(220, 40, 60, 0.35); border-radius: 6px; padding: 4px "
                "10px; }"
                "QPushButton#uploadPreviewDelete:hover { background: "
                "rgba(220, 40, 60, 0.20); }"
                "QPushButton#uploadPreviewDelete:disabled { color: #9a96a6; "
                "border-color: rgba(0, 0, 0, 0.12); background: transparent; }"
                "QPushButton#uploadPreviewClose { color: #6b6478; background: "
                "transparent; border: none; font-size: 15px; padding: 0 4px; }"
                "QPushButton#uploadPreviewClose:hover { color: #000000; }")
                .arg(accent));
    }
    else
    {
        this->setStyleSheet(
            QStringLiteral(
                "QFrame#uploadPreview { background: #221f2b; border: 1px "
                "solid rgba(255, 255, 255, 0.10); border-left: 4px solid %1; "
                "border-radius: 8px; }"
                "QLabel { color: #e6e3ee; background: transparent; border: "
                "none; }"
                "QLabel#uploadPreviewDetail { color: #9a96a6; }"
                "QPushButton#uploadPreviewDelete { color: #ff8a95; "
                "background: rgba(239, 68, 84, 0.12); border: 1px solid "
                "rgba(239, 68, 84, 0.45); border-radius: 6px; padding: 4px "
                "10px; }"
                "QPushButton#uploadPreviewDelete:hover { background: "
                "rgba(239, 68, 84, 0.24); color: #ffffff; }"
                "QPushButton#uploadPreviewDelete:disabled { color: #6f6a7c; "
                "border-color: rgba(255, 255, 255, 0.10); background: "
                "transparent; }"
                "QPushButton#uploadPreviewClose { color: #9a96a6; background: "
                "transparent; border: none; font-size: 15px; padding: 0 4px; }"
                "QPushButton#uploadPreviewClose:hover { color: #ffffff; }")
                .arg(accent));
    }
}

void UploadPreview::place()
{
    if (!this->isVisible())
    {
        return;
    }
    const int width = std::min(MAX_WIDTH, this->view_->width() - 16);
    if (width < 120)
    {
        return;
    }
    const int height = std::max(this->sizeHint().height(), THUMB_HEIGHT + 12);
    this->setFixedSize(width, height);
    this->move(8, this->view_->height() - height - 8);
    // both lines may be long: cut them to the room next to the picture
    const int room =
        width -
        (this->thumbnail_->isVisible() ? this->thumbnail_->sizeHint().width()
                                       : 0) -
        (this->delete_->isVisible() ? this->delete_->sizeHint().width() : 0) -
        this->close_->sizeHint().width() - 50;
    this->title_->setText(this->title_->fontMetrics().elidedText(
        this->titleText_, Qt::ElideRight, std::max(40, room)));
    this->detail_->setText(this->detail_->fontMetrics().elidedText(
        this->detailText_, Qt::ElideMiddle, std::max(40, room)));
}

bool UploadPreview::eventFilter(QObject *object, QEvent *event)
{
    if (object == this->view_ && event->type() == QEvent::Resize)
    {
        this->place();
    }
    return QFrame::eventFilter(object, event);
}

}  // namespace chatterino
