// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/UiLanguage.hpp"
#include "widgets/dialogs/WelcomeDialog.hpp"

namespace chatterino {

WelcomeDialog::WelcomeDialog()
    : BaseWindow({BaseWindow::EnableCustomFrame, BaseWindow::DisableLayoutSave})
{
    this->setWindowTitle(uiText("Chatterino quick setup"));
}

}  // namespace chatterino
