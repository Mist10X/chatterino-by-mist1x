// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/Isolation.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <iterator>

#ifdef Q_OS_WIN
#    include <Windows.h>
#endif

#ifndef CHATTERINO_DISABLE_UPDATER
#    error "This Leafyrino build must not ship the legacy self-updater"
#endif

namespace chatterino::isolation {

namespace {

constexpr Qt::CaseSensitivity PATH_CASE =
#ifdef Q_OS_WIN
    Qt::CaseInsensitive;
#else
    Qt::CaseSensitive;
#endif

/// Folder names other clients use for their profiles and installs.
const QStringList &foreignFolderNames()
{
    static const QStringList names{
        QStringLiteral("Chatterino by Rish"), QStringLiteral("Chatterino+"),
        QStringLiteral("ChatterinoPlus"),     QStringLiteral("Chatterino2"),
        QStringLiteral("Chatterino7"),        QStringLiteral("Chatterino"),
    };
    return names;
}

QString clean(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

/// Real location of an existing file or folder. QFileInfo::canonicalFilePath
/// does not follow NTFS junctions, so on Windows ask the file system itself.
QString finalPath(const QString &existing)
{
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(existing).toStdWString();
    HANDLE handle = CreateFileW(
        native.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE)
    {
        wchar_t buffer[32768];
        const auto length = GetFinalPathNameByHandleW(
            handle, buffer, DWORD(std::size(buffer)), VOLUME_NAME_DOS);
        CloseHandle(handle);
        if (length > 0 && length < std::size(buffer))
        {
            auto path = QString::fromWCharArray(buffer, int(length));
            if (path.startsWith(QStringLiteral("\\\\?\\UNC\\")))
            {
                path = QStringLiteral("\\\\") + path.mid(8);
            }
            else if (path.startsWith(QStringLiteral("\\\\?\\")))
            {
                path = path.mid(4);
            }
            return QDir::cleanPath(QDir::fromNativeSeparators(path));
        }
    }
#endif
    return QFileInfo(existing).canonicalFilePath();
}

/// `path` with symlinks and junctions resolved. A path that does not exist
/// yet is resolved through its deepest existing parent.
QString resolved(const QString &path)
{
    auto rest = QStringList();
    auto current = clean(path);
    while (!current.isEmpty())
    {
        const QFileInfo info(current);
        if (info.exists())
        {
            auto canonical = finalPath(current);
            if (canonical.isEmpty())
            {
                canonical = current;
            }
            for (auto it = rest.crbegin(); it != rest.crend(); ++it)
            {
                canonical = QDir(canonical).filePath(*it);
            }
            return QDir::cleanPath(canonical);
        }
        const auto parent = QDir::cleanPath(info.absolutePath());
        if (parent == current)
        {
            break;
        }
        rest.append(info.fileName());
        current = parent;
    }
    return clean(path);
}

QStringList forms(const QString &path)
{
    QStringList out{clean(path)};
    const auto real = resolved(path);
    if (real.compare(out.first(), PATH_CASE) != 0)
    {
        out.append(real);
    }
    return out;
}

bool startsWithDir(const QString &path, const QString &root)
{
    if (path.compare(root, PATH_CASE) == 0)
    {
        return true;
    }
    const auto prefix = root.endsWith(u'/') ? root : root + u'/';
    return path.startsWith(prefix, PATH_CASE);
}

QString foreignComponent(const QString &path)
{
    for (const auto &part : path.split(u'/', Qt::SkipEmptyParts))
    {
        for (const auto &name : foreignFolderNames())
        {
            if (part.compare(name, Qt::CaseInsensitive) == 0)
            {
                return part;
            }
        }
    }
    return {};
}

}  // namespace

QStringList foreignProfileRoots()
{
    QStringList out;
    auto add = [&](const char *variable, std::initializer_list<const char *> subs) {
        const auto base =
            QDir::fromNativeSeparators(qEnvironmentVariable(variable));
        if (base.isEmpty())
        {
            return;
        }
        for (const auto *sub : subs)
        {
            out.append(QDir::cleanPath(base + u'/' + QString::fromUtf8(sub)));
        }
    };

    add("APPDATA", {"Chatterino2", "Chatterino", "Chatterino by Rish",
                    "Chatterino7", "ChatterinoPlus"});
    add("LOCALAPPDATA",
        {"Chatterino2", "Chatterino", "ChatterinoPlus", "Chatterino7",
         "Programs/Chatterino+", "Programs/Chatterino", "Programs/Chatterino2",
         "Programs/Chatterino7"});
    add("ProgramFiles", {"Chatterino", "Chatterino2", "Chatterino7"});
    add("ProgramFiles(x86)", {"Chatterino", "Chatterino2", "Chatterino7"});
#ifndef Q_OS_WIN
    add("HOME", {".local/share/chatterino", ".var/app/com.chatterino.chatterino",
                 "Library/Application Support/chatterino"});
#endif
    return out;
}

bool isInside(const QString &path, const QString &root)
{
    if (path.isEmpty() || root.isEmpty())
    {
        return false;
    }
    for (const auto &p : forms(path))
    {
        for (const auto &r : forms(root))
        {
            if (startsWithDir(p, r))
            {
                return true;
            }
        }
    }
    return false;
}

QString rejectProfileRoot(const QString &root, const QStringList &foreignRoots)
{
    if (root.trimmed().isEmpty())
    {
        return QStringLiteral("The profile directory is empty.");
    }

    for (const auto &form : forms(root))
    {
        const auto part = foreignComponent(form);
        if (!part.isEmpty())
        {
            return QStringLiteral("\"%1\" is inside a folder named \"%2\", which "
                                  "belongs to another Chatterino client.")
                .arg(QDir::toNativeSeparators(form), part);
        }
    }

    for (const auto &foreign : foreignRoots)
    {
        if (isInside(root, foreign))
        {
            return QStringLiteral("\"%1\" lies inside another client's "
                                  "profile \"%2\".")
                .arg(QDir::toNativeSeparators(root),
                     QDir::toNativeSeparators(foreign));
        }
        if (isInside(foreign, root))
        {
            return QStringLiteral("\"%1\" contains another client's "
                                  "profile \"%2\".")
                .arg(QDir::toNativeSeparators(root),
                     QDir::toNativeSeparators(foreign));
        }
    }

    const QDir dir(resolved(root));
    for (const auto *marker : {"chatterino.exe", "Settings/plus_mirror.json"})
    {
        if (QFileInfo::exists(dir.filePath(QString::fromUtf8(marker))))
        {
            return QStringLiteral("\"%1\" holds \"%2\", so it belongs to "
                                  "another Chatterino client.")
                .arg(QDir::toNativeSeparators(root),
                     QString::fromUtf8(marker));
        }
    }

    return {};
}

QString probeWritable(const QString &dir)
{
    QFile file(QDir(dir).filePath(QStringLiteral(".leafyrino-write-probe-%1")
                                      .arg(QCoreApplication::applicationPid())));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return QStringLiteral("Cannot write to \"%1\": %2")
            .arg(QDir::toNativeSeparators(dir), file.errorString());
    }
    const bool written = file.write("leafyrino") == 9 && file.flush();
    const auto error = file.errorString();
    file.close();
    const bool removed = file.remove();
    if (!written)
    {
        return QStringLiteral("Cannot write to \"%1\": %2")
            .arg(QDir::toNativeSeparators(dir), error);
    }
    if (!removed)
    {
        return QStringLiteral("Cannot remove files in \"%1\".")
            .arg(QDir::toNativeSeparators(dir));
    }
    return {};
}

QString confine(const QString &path, const QString &root,
                const QString &fallback)
{
    if (!path.isEmpty() && isInside(path, root))
    {
        return path;
    }
    return fallback;
}

}  // namespace chatterino::isolation
