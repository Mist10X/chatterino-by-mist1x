// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QString>

namespace chatterino {

/// A file the installer ships with the program (release notes, guide, themes).
/// Next to the executable on Windows, in Contents/Resources of the .app on
/// macOS.
inline QString bundledFilePath(const QString &name)
{
#ifdef Q_OS_MACOS
    return QDir(QCoreApplication::applicationDirPath() +
                QStringLiteral("/../Resources"))
        .filePath(name);
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
#endif
}

}  // namespace chatterino
