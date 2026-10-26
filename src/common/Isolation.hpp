// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

/// Rules that keep this Leafyrino build apart from other Chatterino-family
/// clients (Chatterino2, Chatterino7, Chatterino+) installed for the same
/// Windows user. Kept free of app singletons so it can be tested on its own.
namespace chatterino::isolation {

/// The profile always lives next to the executable (or in --portable-dir),
/// whatever the `modes` file says. There is no AppData fallback.
inline constexpr bool FORCE_PORTABLE = true;

/// Browser-extension message queue. Chatterino2/7 use "chatterino_gui".
inline constexpr const char *NM_QUEUE_NAME = "leafyrino_gui";

/// Value name under HKCU\...\CurrentVersion\Run. Chatterino2/7 use
/// "Chatterino", so sharing it would overwrite or delete their autostart.
inline constexpr const char *AUTOSTART_VALUE_NAME = "Leafyrino";

inline constexpr const char *APPLICATION_NAME = "Leafyrino";
inline constexpr const char *ORGANIZATION_NAME = "Leafyrino";
inline constexpr const char *ORGANIZATION_DOMAIN = "leafyrino.invalid";

/// Profile folders of other clients (and the shared bases holding them),
/// built from APPDATA, LOCALAPPDATA, USERPROFILE, ProgramFiles and HOME.
QStringList foreignProfileRoots();

/// Empty when `root` may hold Leafyrino's profile, otherwise why it may not:
/// it is, holds or lies inside another client's profile or install folder.
QString rejectProfileRoot(const QString &root, const QStringList &foreignRoots);

/// Empty when a file can be created, written and removed inside `dir`.
QString probeWritable(const QString &dir);

/// True when `path` is `root` or lies inside it (junctions resolved).
bool isInside(const QString &path, const QString &root);

/// `path` when it lies inside `root`, otherwise `fallback`.
QString confine(const QString &path, const QString &root,
                const QString &fallback);

}  // namespace chatterino::isolation
