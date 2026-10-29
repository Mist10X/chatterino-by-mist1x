// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <pajlada/signals/scoped-connection.hpp>
#include <pajlada/signals/signal.hpp>
#include <QString>

#include <memory>
#include <vector>

namespace chatterino {

class Paths;
class Settings;
class Modes;

class Updates
{
    const Paths &paths;
    const Modes &modes;

public:
    Updates(const Modes &modes_, const Paths &paths_, Settings &settings);

    enum Status {
        None,
        Searching,
        UpdateAvailable,
        NoUpdateAvailable,
        SearchFailed,
        Downloading,
        DownloadFailed,
        WriteFileFailed,
        MissingPortableUpdater,
        RunUpdaterFailed,
    };

    static bool isDowngradeOf(const QString &online, const QString &current);

    void deleteOldFiles();

    /// Asks GitHub for the newest release of Chatterino by Mist1X.
    void checkForUpdates();
    /// Checks shortly after the start and then every few hours.
    void startPeriodicChecks();
    /// Installed with the setup (not a portable copy): the update can be
    /// downloaded and installed from inside the program.
    static bool isInstalledCopy();
    const QString &getReleaseNotes() const;
    const QString &getReleasePage() const;
    const QString &getCurrentVersion() const;
    const QString &getOnlineVersion() const;
    void installUpdates();
    Status getStatus() const;

    static QString portableUpdaterPath(const Paths &paths);

    bool shouldShowUpdateButton() const;
    bool isError() const;
    bool isDowngrade() const;

    /// Generates the string that the update dialog will show.
    QString buildUpdateAvailableText() const;

    pajlada::Signals::Signal<Status> statusUpdated;

private:
    QString currentVersion_;
    QString onlineVersion_;
    Status status_ = None;
    bool isDowngrade_{};

    QString updateExe_;
    QString updatePortable_;
    QString updateGuideLink_;
    QString updateSums_;
    QString releaseNotes_;
    QString releasePage_;

    void updateFailed_(Status status, const QString &reason);

    void setStatus_(Status status);

    std::vector<std::unique_ptr<pajlada::Signals::ScopedConnection>>
        managedConnections;
};

}  // namespace chatterino
