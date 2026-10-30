// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

class QMenu;

namespace chatterino {

/// The look of the split menu: rounded, roomy items with the accent color
/// under the pointer, in the current theme's colors. Submenus made with
/// addMenu() get it too.
void styleMenu(QMenu *menu);

}  // namespace chatterino
