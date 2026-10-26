// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/helper/ChannelPointsLabel.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"

#include <QHash>
#include <QList>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QUrl>

#include <cmath>

namespace chatterino {

namespace {

/// Downloaded points icons by URL, shared by all inputs. A failed download
/// stays a null pixmap, so the default coin is drawn and nothing is retried.
QHash<QString, QPixmap> &icons()
{
    static QHash<QString, QPixmap> icons;
    return icons;
}

/// Labels waiting for an icon that is still downloading.
QHash<QString, QList<QPointer<ChannelPointsLabel>>> &waiting()
{
    static QHash<QString, QList<QPointer<ChannelPointsLabel>>> waiting;
    return waiting;
}

void download(const QString &url)
{
    NetworkRequest(QUrl(url))
        .onSuccess([url](const NetworkResult &result) {
            QPixmap pixmap;
            pixmap.loadFromData(result.getData());
            icons().insert(url, pixmap);
            for (const auto &label : waiting().take(url))
            {
                if (label)
                {
                    label->update();
                }
            }
        })
        .onError([url](const NetworkResult &result) {
            qCWarning(chatterinoWidget)
                << "Channel points icon failed:" << result.formatError();
            icons().insert(url, {});
            waiting().remove(url);
        })
        .execute();
}

}  // namespace

ChannelPointsLabel::ChannelPointsLabel() = default;

void ChannelPointsLabel::setIconUrl(const QString &url)
{
    if (this->iconUrl_ == url)
    {
        return;
    }
    this->iconUrl_ = url;

    if (!url.isEmpty() && !icons().contains(url))
    {
        auto &labels = waiting()[url];
        if (labels.isEmpty())
        {
            download(url);
        }
        labels.append(this);
    }
    this->update();
}

int ChannelPointsLabel::iconSpace() const
{
    if (this->text().isEmpty())
    {
        return 0;
    }
    auto line = this->fontMetrics().height();
    return static_cast<int>(std::lround(line * 0.85 + line * 0.2));
}

QSize ChannelPointsLabel::sizeHint() const
{
    auto hint = QLabel::sizeHint();
    hint.rwidth() += this->iconSpace();
    return hint;
}

QSize ChannelPointsLabel::minimumSizeHint() const
{
    auto hint = QLabel::minimumSizeHint();
    hint.rwidth() += this->iconSpace();
    return hint;
}

void ChannelPointsLabel::paintEvent(QPaintEvent *event)
{
    QLabel::paintEvent(event);

    if (this->text().isEmpty())
    {
        return;
    }

    // The text is aligned right and bottom: put the icon just left of it.
    const auto metrics = this->fontMetrics();
    const auto line = metrics.height();
    const auto size = line * 0.85;
    const auto area = this->contentsRect();
    const auto textLeft =
        area.right() + 1 - metrics.horizontalAdvance(this->text());
    const auto lineTop = area.bottom() + 1 - line;
    const QRectF rect(textLeft - line * 0.2 - size,
                      lineTop + (line - size) / 2.0, size, size);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto icon = icons().value(this->iconUrl_);
    if (!icon.isNull())
    {
        painter.drawPixmap(rect, icon, QRectF(icon.rect()));
    }
    else
    {
        this->paintDefaultIcon(painter, rect);
    }
}

void ChannelPointsLabel::paintDefaultIcon(QPainter &painter, QRectF rect) const
{
    // Twitch's coin: a ring with a curved stroke inside.
    const auto color = this->palette().color(QPalette::WindowText);
    const auto width = std::max(1.0, rect.width() / 9.0);
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(Qt::NoBrush);

    const auto ring = rect.adjusted(width / 2, width / 2, -width / 2,
                                    -width / 2);
    painter.drawEllipse(ring);

    const auto inner = ring.adjusted(ring.width() * 0.27, ring.height() * 0.27,
                                     -ring.width() * 0.27,
                                     -ring.height() * 0.27);
    // from the top, round the left side, to the bottom-left
    painter.drawArc(inner, 90 * 16, 150 * 16);
}

}  // namespace chatterino
