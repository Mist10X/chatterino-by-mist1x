// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QLabel>
#include <QPixmap>
#include <QString>

namespace chatterino {

/// The channel points balance under the input, with the channel's own points
/// icon to the left of the number (Twitch's coin when the channel has none).
class ChannelPointsLabel : public QLabel
{
public:
    ChannelPointsLabel();

    /// The channel's points icon; empty for Twitch's default coin.
    void setIconUrl(const QString &url);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int iconSpace() const;
    void paintDefaultIcon(QPainter &painter, QRectF rect) const;

    QString iconUrl_;
};

}  // namespace chatterino
