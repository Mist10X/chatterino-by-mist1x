// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

namespace chatterino {

/// How many installations of Chatterino by Mist1X exist: each copy adds one
/// to a public counter once, on its first start, and sends nothing else (no
/// name, no account, no ID). Described in the README and the guide; off with
/// the "Count this installation" setting.
void countInstallOnce();

/// The current number (-1 when it could not be read).
void fetchInstallCount(std::function<void(int)> callback);

}  // namespace chatterino
