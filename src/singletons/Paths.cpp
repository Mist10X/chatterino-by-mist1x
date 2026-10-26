// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Paths.hpp"

#include "common/Args.hpp"
#include "common/Isolation.hpp"
#include "common/Modes.hpp"
#include "singletons/Settings.hpp"
#include "util/CombinePath.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cassert>
#include <mutex>

using namespace std::literals;

namespace chatterino {

Paths::Paths(const Args &args, const Modes &modes)
{
    this->initAppFilePathHash();

    this->initRootDirectory(args, modes);
    this->initSubDirectories();
}

bool Paths::createFolder(const QString &folderPath)
{
    return QDir().mkpath(folderPath);
}

QString Paths::cacheDirectory() const
{
    static const auto pathSetting = [this] {
        QStringSetting cachePathSetting("/cache/path");

        cachePathSetting.connect([root = this->rootAppDataDirectory](
                                     const auto &newPath) {
            if (!newPath.isEmpty() && isolation::isInside(newPath, root))
            {
                QDir().mkpath(newPath);
            }
        });

        return cachePathSetting;
    }();

    auto path = pathSetting.getValue();

    if (path.isEmpty())
    {
        return this->cacheDirectory_;
    }

    // A custom cache folder is honoured only inside this profile; one copied
    // from another client (e.g. %LOCALAPPDATA%\ChatterinoPlus\Cache) is ignored.
    static std::mutex mutex;
    static QString checkedPath;
    static QString checkedResult;
    std::lock_guard lock(mutex);
    if (path != checkedPath)
    {
        checkedPath = path;
        checkedResult = isolation::confine(path, this->rootAppDataDirectory,
                                           this->cacheDirectory_);
    }
    return checkedResult;
}

QString Paths::messageLogsIn(const QString &configured) const
{
    return isolation::confine(configured, this->rootAppDataDirectory,
                              this->messageLogDirectory);
}

QString Paths::cacheFilePath(const QString &fileName) const
{
    return combinePath(this->cacheDirectory(), fileName);
}

void Paths::initAppFilePathHash()
{
    this->applicationFilePathHash =
        QCryptographicHash::hash(
            QCoreApplication::applicationFilePath().toUtf8(),
            QCryptographicHash::Sha224)
            .toBase64()
            .mid(0, 32)
            .replace("+", "-")
            .replace("/", "x");
}

void Paths::initRootDirectory(const Args &args, const Modes &modes)
{
    // This build is always portable (isolation::FORCE_PORTABLE): the profile
    // is the folder of the executable or --portable-dir. There is no code
    // path to %APPDATA%\Chatterino2 or any other shared location.
    static_assert(isolation::FORCE_PORTABLE);
    assert(modes.isPortable);
    (void)modes;

    QString root = QCoreApplication::applicationDirPath();
    if (args.portableDirectory.has_value())
    {
        root = args.portableDirectory.value();
        if (!QFileInfo(root).isDir())
        {
            throw std::runtime_error(
                "The profile directory \""s +
                QDir::toNativeSeparators(root).toStdString() +
                "\" does not exist. Leafyrino will not create or use "
                "another one.");
        }
    }

    const auto reason =
        isolation::rejectProfileRoot(root, isolation::foreignProfileRoots());
    if (!reason.isEmpty())
    {
        throw std::runtime_error(
            "Leafyrino refused to use this profile directory. "s +
            reason.toStdString());
    }

    this->rootAppDataDirectory = QDir::cleanPath(root);
}

void Paths::initSubDirectories()
{
    assert(!this->rootAppDataDirectory.isEmpty());

    auto makePath = [&](const QString &name) -> QString {
        auto path = combinePath(this->rootAppDataDirectory, name);

        if (!QDir().mkpath(path))
        {
            throw std::runtime_error("Could not create directory \""s +
                                     path.toStdString() + "\"");
        }

        return path;
    };

    makePath("");
    this->settingsDirectory = makePath("Settings");
    for (const auto &dir : {this->rootAppDataDirectory, this->settingsDirectory})
    {
        const auto error = isolation::probeWritable(dir);
        if (!error.isEmpty())
        {
            throw std::runtime_error(error.toStdString());
        }
    }
    this->cacheDirectory_ = makePath("Cache");
    this->messageLogDirectory = makePath("Logs");
    this->miscDirectory = makePath("Misc");
    this->twitchProfileAvatars =
        makePath(combinePath("ProfileAvatars", "twitch"));
    this->pluginsDirectory = makePath("Plugins");
    this->themesDirectory = makePath("Themes");
    this->crashdumpDirectory = makePath("Crashes");
    this->dictionariesDirectory = makePath("Dictionaries");
#ifdef Q_OS_WIN
    this->ipcDirectory = makePath("IPC");
#else

    this->ipcDirectory = "/tmp";
#endif
}

}  // namespace chatterino
