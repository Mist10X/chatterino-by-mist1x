// SPDX-FileCopyrightText: 2016 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "BrowserExtension.hpp"
#include "common/Args.hpp"
#include "common/Credentials.hpp"
#include "common/Env.hpp"
#include "common/Isolation.hpp"
#include "common/Modes.hpp"
#include "common/QLogging.hpp"
#include "common/Version.hpp"
#include "providers/IvrApi.hpp"
#include "providers/NetworkConfigurationProvider.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "RunGui.hpp"
#include "singletons/CrashHandler.hpp"
#include "singletons/FileLogger.hpp"
#include "singletons/NativeMessaging.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Updates.hpp"
#include "util/AttachToConsole.hpp"
#include "util/CombinePath.hpp"
#include "util/IpcQueue.hpp"

#ifdef Q_OS_MACOS
#    include "util/MacOsHelpers.h"
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSaveFile>
#include <QSslSocket>
#include <QStringList>
#include <Qt>
#include <QtCore/QtPlugin>
#ifdef Q_OS_WIN
#    include <shobjidl_core.h>
#endif

#include <rapidjson/document.h>
#include <rapidjson/pointer.h>

#include <cstring>
#include <iostream>
#include <memory>

#ifdef CHATTERINO_WITH_AVIF_PLUGIN
Q_IMPORT_PLUGIN(QAVIFPlugin)
#endif

using namespace chatterino;

namespace {

bool argvRequestsLegacyScaling(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] != nullptr &&
            std::strcmp(argv[i], "--use-old-scaling") == 0)
        {
            return true;
        }
    }
    return false;
}

bool jsonPointerBoolIsTrue(const QString &filePath, const char *jsonPointerPath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
    {
        return false;
    }
    const QByteArray data = f.readAll();
    rapidjson::Document doc;
    if (doc.Parse(data.constData(), data.size()).HasParseError())
    {
        return false;
    }
    const auto *v = rapidjson::Pointer(jsonPointerPath).Get(doc);
    return v != nullptr && v->IsBool() && v->GetBool();
}

// Find possible `settings.json` locations before QApplication is created.
// Only this copy's own profile: never another client's settings.json.
QStringList legacyScalingSettingsJsonCandidates(int argc, char **argv)
{
    const QFileInfo fi(QString::fromLocal8Bit(argc > 0 ? argv[0] : ""));
    const QString appDir = fi.absoluteDir().absolutePath();
    return {combinePath(appDir, QStringLiteral("Settings/settings.json"))};
}

/// --isolation-report: writes where this profile keeps its files and which
/// shared Windows integrations are off, then exits without a window and
/// without saving. Used by the isolation regression tests.
int writeIsolationReport(const QString &file, const chatterino::Modes &modes,
                         const chatterino::Args &args,
                         const chatterino::Paths *paths, const QString &error)
{
    using namespace chatterino;

    QJsonObject report{
        {"ok", paths != nullptr},
        {"applicationName", QCoreApplication::applicationName()},
        {"organizationName", QCoreApplication::organizationName()},
        {"organizationDomain", QCoreApplication::organizationDomain()},
        {"portable", modes.isPortable},
        {"nmQueue", isolation::NM_QUEUE_NAME},
        {"autostartValue", isolation::AUTOSTART_VALUE_NAME},
#ifdef CHATTERINO_DISABLE_UPDATER
        {"legacyUpdater", false},
#else
        {"legacyUpdater", true},
#endif
    };
    if (!error.isEmpty())
    {
        report["error"] = error;
    }

    std::unique_ptr<Settings> settings;
    if (paths != nullptr)
    {
        settings =
            std::make_unique<Settings>(modes, args, paths->settingsDirectory);
        report["root"] = paths->rootAppDataDirectory;
        report["settings"] = paths->settingsDirectory;
        report["cache"] = paths->cacheDirectory();
        report["configuredCache"] = QStringSetting("/cache/path").getValue();
        report["logs"] = paths->messageLogsIn(settings->logPath.getValue());
        report["misc"] = paths->miscDirectory;
        report["plugins"] = paths->pluginsDirectory;
        report["ipc"] = paths->ipcDirectory;
        report["crashes"] = paths->crashdumpDirectory;
        report["themes"] = paths->themesDirectory;
        report["dictionaries"] = paths->dictionariesDirectory;
        report["avatars"] = paths->twitchProfileAvatars;
        report["activationServer"] = guiActivationServerName(*paths);
        report["systemKeyring"] = Credentials::usesSystemKeyring();
        // The real code path: it must refuse and write nothing.
        report["nmRegistered"] = registerNmHost(*paths);

        QJsonArray plugins;
        for (const auto &info :
             QDir(paths->pluginsDirectory)
                 .entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            plugins.append(QJsonObject{
                {"name", info.fileName()},
                {"link", info.isSymLink() || info.isJunction()},
                {"target", info.isJunction() ? info.junctionTarget()
                                             : info.canonicalFilePath()},
            });
        }
        report["pluginFolders"] = plugins;
    }

    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly) ||
        out.write(QJsonDocument(report).toJson()) < 0 || !out.commit())
    {
        return 3;
    }
    return paths != nullptr ? 0 : 2;
}

bool settingsFileRequestsLegacyScaling(int argc, char **argv)
{
    for (const auto &path : legacyScalingSettingsJsonCandidates(argc, argv))
    {
        if (jsonPointerBoolIsTrue(path, "/appearance/useLegacyScaling"))
        {
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argvRequestsLegacyScaling(argc, argv) ||
        settingsFileRequestsLegacyScaling(argc, argv))
    {
        QApplication::setAttribute(Qt::AA_Use96Dpi, true);
    }

    QApplication a(argc, argv);

    // Own names, so nothing Qt derives from them (QSettings, standard paths)
    // can land in another Chatterino client's place.
    QCoreApplication::setApplicationName(isolation::APPLICATION_NAME);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Chatterino by Mist1X"));
    QCoreApplication::setApplicationVersion(CHATTERINO_VERSION);
    QCoreApplication::setOrganizationName(isolation::ORGANIZATION_NAME);
    QCoreApplication::setOrganizationDomain(isolation::ORGANIZATION_DOMAIN);
    // Must match the installed .desktop / Flatpak app-id so the shell
    // associates this window with Leafyrino's icon (not Chatterino's).
    QGuiApplication::setDesktopFileName("com.leafyzito.leafyrino");
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(
        Version::instance().appUserModelID().c_str());
#endif

    const Args args(a);
    const Modes modes(args);
    std::unique_ptr<Paths> paths;

    // Optional logger override that logs to a file
    FileLogger logger;

    try
    {
        paths = std::make_unique<Paths>(args, modes);
    }
    catch (std::runtime_error &error)
    {
        if (args.isolationReport)
        {
            return writeIsolationReport(*args.isolationReport, modes, args,
                                        nullptr,
                                        QString::fromUtf8(error.what()));
        }

        auto errorMessage =
            QString::fromUtf8(error.what()) +
            QStringLiteral(
                "\n\nChatterino by Mist1X keeps its profile only next to Leafyrino.exe "
                "(or in --portable-dir) and never falls back to another "
                "location, so it did not start. Move it to a folder you can "
                "write to.");
        std::cerr << errorMessage.toLocal8Bit().constData() << '\n';
        std::cerr.flush();
        QMessageBox box;
        box.setIcon(QMessageBox::Critical);
        box.setText(errorMessage);
        box.exec();
        return 1;
    }
    ipc::initPaths(paths.get());

#ifdef CHATTERINO_WITH_CRASHPAD
    const auto crashpadHandler = installCrashHandler(args, *paths);
#endif

    if (args.isolationReport)
    {
        return writeIsolationReport(*args.isolationReport, modes, args,
                                    paths.get(), {});
    }

    if (args.shouldRunBrowserExtensionHost)
    {
#ifdef Q_OS_MACOS
        ::chatterinoSetMacOsActivationPolicyProhibited();
#endif
        runBrowserExtensionHost();
    }
    else if (args.printVersion)
    {
        attachToConsole();

        auto version = Version::instance();
        auto versionMessage =
            QString("%1 (commit %2%3)")
                .arg(version.fullVersion())
                .arg(version.commitHash())
                .arg(version.isNightly() ? ", " + version.dateOfBuild() : "");
        std::cout << versionMessage.toLocal8Bit().constData() << '\n';
        std::cout.flush();
    }
    else
    {
        if (args.verbose)
        {
            attachToConsole();
        }

        qCInfo(chatterinoApp).noquote()
            << "Leafyrino Qt SSL library build version:"
            << QSslSocket::sslLibraryBuildVersionString();
        qCInfo(chatterinoApp).noquote()
            << "Leafyrino Qt SSL library version:"
            << QSslSocket::sslLibraryVersionString();
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
        qCInfo(chatterinoApp).noquote()
            << "Leafyrino Qt SSL active backend:" << QSslSocket::activeBackend()
            << "of" << QSslSocket::availableBackends().join(", ");
#    if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        qCInfo(chatterinoApp) << "Leafyrino Qt SSL active backend features:"
                              << QSslSocket::supportedFeatures();
#    endif
        qCInfo(chatterinoApp) << "Leafyrino Qt SSL active backend protocols:"
                              << QSslSocket::supportedProtocols();
#endif

        Settings settings(modes, args, paths->settingsDirectory);
#ifndef Q_OS_MACOS
        if (!args.remoteRestart && !args.isFramelessEmbed &&
            settings.trayHideOnClose.getValue() &&
            activateExistingGuiInstance(*paths))
        {
            return 0;
        }
#endif

        Updates updates(modes, *paths, settings);

        NetworkConfigurationProvider::applyFromEnv(Env::get());

        IvrApi::initialize();
        Helix::initialize();

        runGui(a, modes, *paths, settings, args, updates);
    }
    return 0;
}
