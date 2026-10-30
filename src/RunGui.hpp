// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

class QApplication;
class QString;

namespace chatterino {

class Args;
class Paths;
class Settings;
class Updates;
class Modes;

bool activateExistingGuiInstance(const Paths &paths);

/// Local socket that a second start of this same profile uses to bring the
/// running window forward; unique per profile folder.
QString guiActivationServerName(const Paths &paths);

void runGui(QApplication &a, const Modes &modes, const Paths &paths,
            Settings &settings, const Args &args, Updates &updates);

/// Quits and starts the program again once this copy has fully closed (a
/// second start while it still runs would only bring this window forward).
void restartApp();
bool restartRequested();

}  // namespace chatterino
