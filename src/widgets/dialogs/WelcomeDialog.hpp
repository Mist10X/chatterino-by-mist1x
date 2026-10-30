// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

class QWidget;

namespace chatterino {

/// "Getting started": log in to Twitch, open a channel, the second login for
/// channel points and moving settings over from another Chatterino. Shown
/// once on the first start of a fresh copy.
void showWelcomeDialog(QWidget *parent);

/// Shows the dialog if this copy has never shown it and nobody is logged in
/// yet (people updating from an older version are not asked).
void showWelcomeDialogIfNew(QWidget *parent);

/// Stand only (--ui-snapshot-settings import:<Settings folder>): the same
/// import without the question; it restarts the program.
void importSettingsForTesting(const QString &from);

}  // namespace chatterino
