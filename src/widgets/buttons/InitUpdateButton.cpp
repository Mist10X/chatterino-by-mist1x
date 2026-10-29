// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/buttons/InitUpdateButton.hpp"

#include "Application.hpp"
#include "common/UiLanguage.hpp"
#include "singletons/Updates.hpp"
#include "widgets/buttons/PixmapButton.hpp"
#include "widgets/helper/UpdateBanner.hpp"

namespace chatterino {

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

        const auto *imageUrl = getApp()->getUpdates().isError()
                                   ? ":/buttons/updateError.png"
                                   : ":/buttons/update.png";
        button.setPixmap(QPixmap(imageUrl));

        relayout();
    };

    updateChange(getApp()->getUpdates().getStatus());

    signalHolder.managedConnect(getApp()->getUpdates().statusUpdated,
                                [updateChange](auto status) {
                                    updateChange(status);
                                });
}

}  // namespace chatterino
