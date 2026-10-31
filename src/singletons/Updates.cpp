// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Updates.hpp"

#include "Application.hpp"
#include "common/Args.hpp"
#include "common/Literals.hpp"
#include "common/Mist1XVersion.hpp"
#include "common/Modes.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "common/UiLanguage.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/CombinePath.hpp"
#include "util/PostToThread.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStringBuilder>
#include <QTimer>
#include <QtConcurrent>
#include <semver/semver.hpp>

namespace {

using namespace chatterino;
using namespace literals;

/// Releases of Chatterino by Mist1X are published here (a public repository,
/// so the check needs no login; GitHub allows 60 such requests an hour).
const QString RELEASES_API =
    u"https://api.github.com/repos/Mist10X/chatterino-by-mist1x/releases"_s;
const QString RELEASES_PAGE =
    u"https://github.com/Mist10X/chatterino-by-mist1x/releases"_s;

constexpr auto FIRST_CHECK_DELAY = std::chrono::seconds(20);

/// For the update tests only: MIST1X_UPDATE_API points the check at a local
/// copy of the releases API, MIST1X_UPDATE_TEST_INSTALL=1 installs a found
/// update without asking.
QString releasesApi()
{
    const auto test = qEnvironmentVariable("MIST1X_UPDATE_API");
    return test.startsWith(u"http") ? test : RELEASES_API;
}

bool testInstall()
{
    return qEnvironmentVariable("MIST1X_UPDATE_TEST_INSTALL") == u"1";
}
constexpr auto CHECK_INTERVAL = std::chrono::hours(6);
constexpr auto RETRY_AFTER_FAILURE = std::chrono::minutes(10);

/// "v1.2.0" -> "1.2.0"
QString versionOfTag(QString tag)
{
    if (tag.startsWith(u'v') || tag.startsWith(u'V'))
    {
        tag.remove(0, 1);
    }
    return tag.trimmed();
}

bool isNewer(const QString &online, const QString &current)
{
    semver::version a;
    semver::version b;
    if (!a.from_string_noexcept(online.toStdString()) ||
        !b.from_string_noexcept(current.toStdString()))
    {
        qCWarning(chatterinoUpdate)
            << "Unable to compare versions" << online << current;
        return false;
    }
    return a > b;
}

/// The SHA-256 of @a fileName in a SHA256SUMS.txt ("<hash>  <name>" lines).
QString expectedHash(const QByteArray &sums, const QString &fileName)
{
    for (const auto &line : QString::fromUtf8(sums).split(u'\n'))
    {
        const auto parts = line.simplified().split(u' ');
        auto name = parts.size() >= 2 ? parts.last() : QString();
        name.remove(u'*');
        if (parts.size() >= 2 && name == fileName &&
            parts.first().size() == 64)
        {
            return parts.first().toLower();
        }
    }
    return {};
}

NetworkRequest githubRequest(const QString &url)
{
    return NetworkRequest(url)
        .header("Accept", "application/vnd.github+json")
        .header("User-Agent", "ChatterinoByMist1X/" MIST1X_VERSION)
        .followRedirects(true);
}

#ifdef Q_OS_MACOS
/// "<...>/Chatterino by Mist1X.app" when running from a bundle, else empty.
QString runningBundlePath()
{
    // applicationDirPath() is <...>/X.app/Contents/MacOS
    QDir dir(QCoreApplication::applicationDirPath());
    if (!dir.cdUp() || !dir.cdUp())
    {
        return {};
    }
    return dir.absolutePath().endsWith(u".app") ? dir.absolutePath()
                                                : QString();
}

/// Waits for this process to quit, swaps the .app for the one in the DMG
/// (the old one stays until the new one is in place) and starts it.
const char *const MAC_UPDATE_SCRIPT = R"(#!/bin/sh
PID="$1"; DMG="$2"; APP="$3"; LOG="$4"
exec >"$LOG" 2>&1
n=0
while kill -0 "$PID" 2>/dev/null && [ "$n" -lt 120 ]; do sleep 0.5; n=$((n+1)); done
MNT=$(mktemp -d /tmp/mist1x-update.XXXXXX) || exit 1
cleanup() { hdiutil detach "$MNT" -force -quiet; rmdir "$MNT"; }
fail() { echo "update failed: $1"; cleanup; [ -d "$APP" ] && open "$APP"; exit 1; }
hdiutil attach -nobrowse -readonly -noverify -mountpoint "$MNT" "$DMG" || fail attach
NEW=""
for a in "$MNT"/*.app; do NEW="$a"; break; done
[ -d "$NEW" ] || fail "no app in the image"
TMP="$APP.new"; OLD="$APP.old"
rm -rf "$TMP" "$OLD"
ditto "$NEW" "$TMP" || fail copy
xattr -cr "$TMP"
codesign --verify --deep "$TMP" || { rm -rf "$TMP"; fail signature; }
mv "$APP" "$OLD" || { rm -rf "$TMP"; fail replace; }
mv "$TMP" "$APP" || { mv "$OLD" "$APP"; fail replace; }
rm -rf "$OLD"
cleanup
rm -f "$DMG"
open "$APP"
)";

/// Starts the swap above; false when it could not be started.
bool startMacUpdate(const QString &dmgPath, const QString &miscDirectory)
{
    const auto bundle = runningBundlePath();
    if (bundle.isEmpty())
    {
        return false;
    }
    const auto scriptPath = combinePath(miscDirectory, "update-mac.sh");
    QFile script(scriptPath);
    if (!script.open(QIODevice::Truncate | QIODevice::WriteOnly) ||
        script.write(MAC_UPDATE_SCRIPT) < 0)
    {
        return false;
    }
    script.close();
    script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                          QFileDevice::ExeOwner);
    return QProcess::startDetached(
        QStringLiteral("/bin/sh"),
        {scriptPath, QString::number(QCoreApplication::applicationPid()),
         dmgPath, bundle, combinePath(miscDirectory, "update-mac.log")});
}
#endif

}  // namespace

namespace chatterino {

Updates::Updates(const Modes &modes_, const Paths &paths_, Settings &settings)
    : paths(paths_)
    , modes(modes_)
    , currentVersion_(QStringLiteral(MIST1X_VERSION))
    , updateGuideLink_(RELEASES_PAGE)
{
    qCDebug(chatterinoUpdate) << "init UpdateManager";

    // "beta" also takes test versions (pre-releases) into account
    settings.betaUpdates.connect(
        [this] {
            this->checkForUpdates();
        },
        this->managedConnections, false);
}

bool Updates::isDowngradeOf(const QString &online, const QString &current)
{
    semver::version onlineVersion;
    if (!onlineVersion.from_string_noexcept(online.toStdString()))
    {
        qCWarning(chatterinoUpdate) << "Unable to parse online version"
                                    << online << "into a proper semver string";
        return false;
    }

    semver::version currentVersion;
    if (!currentVersion.from_string_noexcept(current.toStdString()))
    {
        qCWarning(chatterinoUpdate) << "Unable to parse current version"
                                    << current << "into a proper semver string";
        return false;
    }

    return onlineVersion < currentVersion;
}

void Updates::deleteOldFiles()
{
    std::ignore = QtConcurrent::run([dir{this->paths.miscDirectory}] {
        for (const auto *name :
             {"Update.exe", "update.zip", "Update.dmg", "update-mac.sh"})
        {
            auto path = combinePath(dir, name);
            if (QFile::exists(path))
            {
                QFile::remove(path);
            }
        }
#ifdef Q_OS_MACOS
        // left over when an update was interrupted after the swap
        const auto bundle = runningBundlePath();
        if (!bundle.isEmpty())
        {
            QDir(bundle + u".old").removeRecursively();
            QDir(bundle + u".new").removeRecursively();
        }
#endif
    });
}

const QString &Updates::getCurrentVersion() const
{
    return this->currentVersion_;
}

const QString &Updates::getOnlineVersion() const
{
    return this->onlineVersion_;
}

const QString &Updates::getReleaseNotes() const
{
    return this->releaseNotes_;
}

const QString &Updates::getReleasePage() const
{
    return this->releasePage_;
}

bool Updates::isInstalledCopy()
{
#ifdef Q_OS_WIN
    // the setup leaves its uninstaller next to the program
    return QFile::exists(
        QDir(QCoreApplication::applicationDirPath()).filePath("unins000.exe"));
#elif defined(Q_OS_MACOS)
    // an app that was moved out of the disk image and can be replaced
    const auto bundle = runningBundlePath();
    return !bundle.isEmpty() && !bundle.startsWith(u"/Volumes/") &&
           QFileInfo(QFileInfo(bundle).absolutePath()).isWritable();
#else
    return false;
#endif
}

void Updates::startPeriodicChecks()
{
    auto *app = tryGetApp();
    if (app == nullptr || app->getArgs().isolationReport ||
        ((app->getArgs().uiSnapshot || app->getArgs().dontSaveSettings) &&
         releasesApi() == RELEASES_API))
    {
        return;  // offline stands, previews and one-off runs
    }
    QTimer::singleShot(FIRST_CHECK_DELAY, [this] {
        this->checkForUpdates();
    });
    auto *timer = new QTimer();  // lives as long as the app
    QObject::connect(timer, &QTimer::timeout, [this] {
        this->checkForUpdates();
    });
    timer->start(CHECK_INTERVAL);
}

void Updates::updateFailed_(Status status, const QString &reason)
{
    qCWarning(chatterinoUpdate) << "Update failed:" << reason;
    this->setStatus_(status);
    const auto page =
        this->releasePage_.isEmpty() ? RELEASES_PAGE : this->releasePage_;
    postToThread([reason, page] {
        auto *box = new QMessageBox(
            QMessageBox::Warning, uiText("Chatterino by Mist1X update"),
            uiText("The update could not be installed:") + "\n" + reason);
        box->setAttribute(Qt::WA_DeleteOnClose);
        auto *open = box->addButton(uiText("Open the download page"),
                                    QMessageBox::AcceptRole);
        box->addButton(QMessageBox::Close);
        QObject::connect(open, &QPushButton::clicked, [page] {
            QDesktopServices::openUrl(QUrl(page));
        });
        box->show();
        box->raise();
    });
}

void Updates::installUpdates()
{
    if (this->status_ != UpdateAvailable && this->status_ != DownloadFailed &&
        this->status_ != WriteFileFailed)
    {
        return;
    }

    // A portable copy updates itself by unpacking the new archive over it;
    // the page has it.
    if (!Updates::isInstalledCopy() || this->updateExe_.isEmpty() ||
        this->updateSums_.isEmpty())
    {
        QDesktopServices::openUrl(QUrl(
            this->releasePage_.isEmpty() ? RELEASES_PAGE : this->releasePage_));
        return;
    }

    this->setStatus_(Downloading);
    const auto setupName = QUrl(this->updateExe_).fileName();

    // First the checksums, then the setup: only a file whose SHA-256 matches
    // the one published with the release is started.
    githubRequest(this->updateSums_)
        .timeout(60000)
        .onError([this](const NetworkResult &result) {
            this->updateFailed_(DownloadFailed, result.formatError());
        })
        .onSuccess([this, setupName](const NetworkResult &sums) {
            const auto expected = expectedHash(sums.getData(), setupName);
            if (expected.isEmpty())
            {
                this->updateFailed_(DownloadFailed,
                                    uiText("The release has no checksum for "
                                           "the setup."));
                return;
            }
            githubRequest(this->updateExe_)
                .timeout(15 * 60 * 1000)
                .onError([this](const NetworkResult &result) {
                    this->updateFailed_(DownloadFailed, result.formatError());
                })
                .onSuccess([this, expected](const NetworkResult &setup) {
                    const auto &data = setup.getData();
                    const auto got =
                        QCryptographicHash::hash(data,
                                                 QCryptographicHash::Sha256)
                            .toHex();
                    if (QString::fromLatin1(got) != expected)
                    {
                        this->updateFailed_(
                            DownloadFailed,
                            uiText("The downloaded file is damaged (its "
                                   "checksum does not match)."));
                        return;
                    }

#ifdef Q_OS_MACOS
                    const auto filePath =
                        combinePath(this->paths.miscDirectory, "Update.dmg");
#else
                    const auto filePath =
                        combinePath(this->paths.miscDirectory, "Update.exe");
#endif
                    QFile file(filePath);
                    if (!file.open(QIODevice::Truncate | QIODevice::WriteOnly) ||
                        file.write(data) != data.size())
                    {
                        this->updateFailed_(
                            WriteFileFailed,
                            uiText("The file could not be saved. An antivirus "
                                   "may be blocking it."));
                        return;
                    }
                    file.close();

                    // The setup shows only its progress, closes this copy if
                    // it is still running, keeps the settings and starts the
                    // new version (AUTOUPDATE, see ChatterinoByMist1X.iss).
#ifdef Q_OS_MACOS
                    // the helper replaces this app after it has quit
                    const bool started =
                        startMacUpdate(filePath, this->paths.miscDirectory);
#else
                    const bool started = QProcess::startDetached(
                        filePath, {"/SILENT", "/SUPPRESSMSGBOXES", "/NORESTART",
                                   "/CLOSEAPPLICATIONS", "/AUTOUPDATE=1"});
#endif
                    if (!started)
                    {
                        this->updateFailed_(
                            RunUpdaterFailed,
                            uiText("The setup could not be started. An "
                                   "antivirus may be blocking it."));
                        return;
                    }
                    QApplication::exit(0);
                })
                .execute();
        })
        .execute();
}

void Updates::checkForUpdates()
{
    if (this->status_ == Downloading)
    {
        return;
    }

    const bool withTests = getSettings()->betaUpdates;
    auto onSuccess = [this, withTests](const NetworkResult &result) {
        QJsonObject release;
        if (withTests)
        {
            // newest first; drafts are not public anyway
            for (const auto &value : result.parseJsonArray())
            {
                const auto object = value.toObject();
                if (!object["draft"].toBool())
                {
                    release = object;
                    break;
                }
            }
        }
        else
        {
            release = result.parseJson();
        }

        const auto version = versionOfTag(release["tag_name"].toString());
        if (version.isEmpty())
        {
            qCDebug(chatterinoUpdate) << "No release found";
            if (this->status_ != UpdateAvailable)
            {
                this->setStatus_(SearchFailed);
            }
            return;
        }

#ifdef Q_OS_MACOS
        const auto setupName =
            u"ChatterinoByMist1X-" % version % u"-macOS-arm64.dmg";
#else
        const auto setupName =
            u"ChatterinoByMist1X-Setup-" % version % u".exe";
#endif
        const auto portableName =
            u"ChatterinoByMist1X-" % version % u"-portable.zip";
        QString setup;
        QString portable;
        QString sums;
        for (const auto &value : release["assets"].toArray())
        {
            const auto asset = value.toObject();
            const auto name = asset["name"].toString();
            const auto url = asset["browser_download_url"].toString();
            if (name == setupName)
            {
                setup = url;
            }
            else if (name == portableName)
            {
                portable = url;
            }
            else if (name == u"SHA256SUMS.txt")
            {
                sums = url;
            }
        }

        this->onlineVersion_ = version;
        this->updateExe_ = setup;
        this->updatePortable_ = portable;
        this->updateSums_ = sums;
        this->releaseNotes_ = release["body"].toString().trimmed();
        this->releasePage_ = release["html_url"].toString();
        this->isDowngrade_ = false;

        qCDebug(chatterinoUpdate)
            << "Newest release" << version << "current" << this->currentVersion_;
        const bool newer = isNewer(version, this->currentVersion_);
        this->setStatus_(newer ? UpdateAvailable : NoUpdateAvailable);
        if (newer && testInstall())
        {
            this->installUpdates();
        }
    };

    githubRequest(withTests ? QString(releasesApi() % u"?per_page=10")
                            : QString(releasesApi() % u"/latest"))
        .timeout(30000)
        .onSuccess(onSuccess)
        .onError([this](const NetworkResult &result) {
            qCDebug(chatterinoUpdate)
                << "Update check failed:" << result.formatError();
            if (this->status_ != UpdateAvailable)
            {
                this->setStatus_(SearchFailed);
            }
            // no internet at startup: try again soon, not in 6 hours
            static bool retryPending = false;
            if (!retryPending)
            {
                retryPending = true;
                QTimer::singleShot(RETRY_AFTER_FAILURE, [this] {
                    retryPending = false;
                    this->checkForUpdates();
                });
            }
        })
        .execute();

    if (this->status_ != UpdateAvailable)
    {
        this->setStatus_(Searching);
    }
}

Updates::Status Updates::getStatus() const
{
    return this->status_;
}

QString Updates::portableUpdaterPath(const Paths &paths)
{
    return combinePath(paths.rootAppDataDirectory,
                       "updater.1/ChatterinoUpdater.exe");
}

bool Updates::shouldShowUpdateButton() const
{
    switch (this->getStatus())
    {
        // not on a failed check: no internet is not worth a red button
        case UpdateAvailable:
        case Downloading:
        case DownloadFailed:
        case WriteFileFailed:
        case RunUpdaterFailed:
            return true;

        default:
            return false;
    }
}

bool Updates::isError() const
{
    switch (this->getStatus())
    {
        case SearchFailed:
        case DownloadFailed:
        case WriteFileFailed:
        case MissingPortableUpdater:
        case RunUpdaterFailed:
            return true;

        default:
            return false;
    }
}

bool Updates::isDowngrade() const
{
    return this->isDowngrade_;
}

QString Updates::buildUpdateAvailableText() const
{
    return uiText("Version %1 is available (you have %2).")
        .arg(this->getOnlineVersion(), this->getCurrentVersion());
}

void Updates::setStatus_(Status status)
{
    if (this->status_ != status)
    {
        this->status_ = status;
        postToThread([this, status] {
            this->statusUpdated.invoke(status);
        });
    }
}

}  // namespace chatterino
