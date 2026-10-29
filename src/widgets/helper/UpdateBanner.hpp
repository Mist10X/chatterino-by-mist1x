// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <pajlada/signals/signalholder.hpp>

class QWidget;

namespace chatterino {

/// When a newer Chatterino by Mist1X is out, a card at the top of @a window
/// says so (once per version: closing it hides it until the next version).
/// Clicking it opens showUpdatePrompt().
void initUpdateBanner(QWidget *window, pajlada::Signals::SignalHolder &holder);

/// What is new in the available version, with "Update now" / "Later".
void showUpdatePrompt(QWidget *parent);

}  // namespace chatterino
