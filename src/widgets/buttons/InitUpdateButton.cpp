// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/buttons/InitUpdateButton.hpp"

#include "Application.hpp"
#include "common/UiLanguage.hpp"
#include "singletons/Updates.hpp"
#include "widgets/buttons/PixmapButton.hpp"
#include "widgets/helper/UpdateBanner.hpp"

#include <QPainter>
#include <QPainterPath>

namespace chatterino {

namespace {

/// A muted red square with a white down arrow: seen among the title bar
/// buttons without shouting.
QPixmap updatePixmap()
{
    constexpr int size = 48;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(190, 74, 86));
    painter.drawRoundedRect(QRectF(2, 2, size - 4, size - 4), 10, 10);

    QPainterPath arrow;
    arrow.moveTo(20, 11);
    arrow.lineTo(28, 11);
    arrow.lineTo(28, 25);
    arrow.lineTo(35, 25);
    arrow.lineTo(24, 37);
    arrow.lineTo(13, 25);
    arrow.lineTo(20, 25);
    arrow.closeSubpath();
    painter.setBrush(QColor(255, 244, 245));
    painter.drawPath(arrow);
    return pixmap;
}

}  // namespace

void initUpdateButton(PixmapButton &button,
                      const std::function<void()> &relayout,
                      pajlada::Signals::SignalHolder &signalHolder)
{
    button.hide();

    // show update prompt when clicking the button
    QObject::connect(&button, &Button::leftClicked, [&button] {
        showUpdatePrompt(button.window());
    });

    // update image when state changes
    auto updateChange = [&button, relayout](auto) {
        button.setVisible(getApp()->getUpdates().shouldShowUpdateButton());
        button.setToolTip(uiText("Update %1 is available")
                              .arg(getApp()->getUpdates().getOnlineVersion()));

        button.setPixmap(getApp()->getUpdates().isError()
                             ? QPixmap(":/buttons/updateError.png")
                             : updatePixmap());

        relayout();
    };

    updateChange(getApp()->getUpdates().getStatus());

    signalHolder.managedConnect(getApp()->getUpdates().statusUpdated,
                                [updateChange](auto status) {
                                    updateChange(status);
                                });
}

}  // namespace chatterino
