// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/WelcomeDialog.hpp"

#include "Application.hpp"
#include "common/UiLanguage.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/dialogs/SettingsDialog.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QUrl>
#include <QVBoxLayout>

#include <cstdlib>

namespace chatterino {

namespace {

/// Files of a Chatterino profile that carry the user's setup. Logins are not
/// among them: other clients keep them in the Windows credential store.
const QStringList IMPORTED_FILES{
    QStringLiteral("window-layout.json"),
    QStringLiteral("commands.json"),
    QStringLiteral("user-data.json"),
};

QString shownMarker()
{
    return QDir(getApp()->getPaths().miscDirectory).filePath("welcome-shown");
}

/// Other clients' profiles found on this computer (their Settings folders).
QList<std::pair<QString, QString>> otherProfiles()
{
    QList<std::pair<QString, QString>> out;
    const auto appData = qEnvironmentVariable("APPDATA");
    const std::pair<const char *, const char *> known[]{
        {"Chatterino, Chatterino7, Leafyrino", "Chatterino2"},
        {"Chatterino+", "Chatterino by Rish"},
        {"Chatterino", "Chatterino"},
    };
    for (const auto &[name, folder] : known)
    {
        const auto settings =
            QDir(appData).filePath(QString::fromUtf8(folder) + "/Settings");
        if (QFileInfo::exists(QDir(settings).filePath("settings.json")))
        {
            out.append({QString::fromUtf8(name), settings});
        }
    }
    return out;
}

bool importProfile(QWidget *parent, const QString &from)
{
    const auto own = getApp()->getPaths().settingsDirectory;
    if (QDir(from).absolutePath() == QDir(own).absolutePath())
    {
        return false;
    }
    QFile theirsFile(QDir(from).filePath("settings.json"));
    if (!theirsFile.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(parent, uiText("Move settings"),
                             uiText("There is no settings.json in this folder."));
        return false;
    }
    auto theirs = QJsonDocument::fromJson(theirsFile.readAll()).object();
    if (theirs.isEmpty())
    {
        QMessageBox::warning(parent, uiText("Move settings"),
                             uiText("The settings in this folder could not be "
                                    "read."));
        return false;
    }

    if (QMessageBox::question(
            parent, uiText("Move settings"),
            uiText("Tabs with channels, highlights, ignores, commands, filters "
                   "and the rest of the settings are copied from:\n%1\n\nThe "
                   "current settings are kept in a backup folder. Logging in to "
                   "Twitch has to be done again. The program restarts.")
                .arg(QDir::toNativeSeparators(from))) != QMessageBox::Yes)
    {
        return false;
    }

    // the current settings stay in a backup folder
    const auto backup = QDir(own).filePath(
        "before-import-" +
        QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QDir().mkpath(backup);
    for (const auto &name : IMPORTED_FILES + QStringList{"settings.json"})
    {
        const auto path = QDir(own).filePath(name);
        if (QFileInfo::exists(path))
        {
            QFile::copy(path, QDir(backup).filePath(name));
        }
    }

    // Their settings, but this copy's logins and Archive connection stay:
    // the logins of the other client are not in its settings file anyway.
    QFile ownFile(QDir(own).filePath("settings.json"));
    QJsonObject mine;
    if (ownFile.open(QIODevice::ReadOnly))
    {
        mine = QJsonDocument::fromJson(ownFile.readAll()).object();
        ownFile.close();
    }
    for (const auto *key : {"accounts", "archive"})
    {
        if (mine.contains(QLatin1String(key)))
        {
            theirs.insert(QLatin1String(key), mine.value(QLatin1String(key)));
        }
        else
        {
            theirs.remove(QLatin1String(key));
        }
    }
    QSaveFile out(QDir(own).filePath("settings.json"));
    if (!out.open(QIODevice::WriteOnly) ||
        out.write(QJsonDocument(theirs).toJson()) < 0 || !out.commit())
    {
        QMessageBox::warning(parent, uiText("Move settings"),
                             uiText("The settings could not be saved."));
        return false;
    }
    for (const auto &name : IMPORTED_FILES)
    {
        const auto source = QDir(from).filePath(name);
        if (QFileInfo::exists(source))
        {
            const auto target = QDir(own).filePath(name);
            QFile::remove(target);
            QFile::copy(source, target);
        }
    }

    // Start again without saving the settings in memory over the new ones.
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    std::_Exit(0);
}

void chooseImport(QWidget *parent, QPushButton *button)
{
    auto *menu = new QMenu(parent);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (const auto &[name, path] : otherProfiles())
    {
        QObject::connect(menu->addAction(name), &QAction::triggered, parent,
                         [parent, path] {
                             importProfile(parent, path);
                         });
    }
    if (!menu->isEmpty())
    {
        menu->addSeparator();
    }
    QObject::connect(
        menu->addAction(uiText("Choose the Settings folder…")),
        &QAction::triggered, parent, [parent] {
            const auto dir = QFileDialog::getExistingDirectory(
                parent, uiText("The Settings folder of the other Chatterino"));
            if (!dir.isEmpty())
            {
                importProfile(parent, dir);
            }
        });
    menu->popup(button->mapToGlobal(QPoint(0, button->height())));
}

QWidget *step(int number, const QString &title, const QString &text,
              const QList<QPushButton *> &buttons)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->setSpacing(14);

    auto *circle = new QLabel(QString::number(number));
    circle->setObjectName("welcomeNumber");
    circle->setAlignment(Qt::AlignCenter);
    circle->setFixedSize(28, 28);
    layout->addWidget(circle, 0, Qt::AlignTop);

    auto *texts = new QVBoxLayout;
    texts->setSpacing(3);
    auto *heading = new QLabel(title);
    heading->setObjectName("welcomeTitle");
    texts->addWidget(heading);
    auto *body = new QLabel(text);
    body->setObjectName("welcomeText");
    body->setWordWrap(true);
    texts->addWidget(body);
    layout->addLayout(texts, 1);

    auto *actions = new QVBoxLayout;
    actions->setSpacing(4);
    for (auto *button : buttons)
    {
        actions->addWidget(button);
    }
    actions->addStretch(1);
    layout->addLayout(actions);
    return row;
}

}  // namespace

void showWelcomeDialog(QWidget *parent)
{
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(uiText("Getting started"));
    dialog->setMinimumWidth(640);
    const auto accent = getTheme()->accent.name();
    dialog->setStyleSheet(QStringLiteral(R"(
QDialog { background: #16161c; }
QLabel { color: #e6e5ee; font-size: 13px; }
QLabel#welcomeHeading { font-size: 20px; font-weight: 600; color: #f4f3fa; }
QLabel#welcomeTitle { font-size: 14px; font-weight: 600; color: #f4f3fa; }
QLabel#welcomeText { color: #a9a7b6; }
QLabel#welcomeNumber { background: %1; color: #16161c; border-radius: 14px;
                       font-weight: 700; }
QPushButton { background: #25252e; border: 1px solid #363642; border-radius: 6px;
              padding: 5px 14px; color: #e6e6ee; }
QPushButton:hover { background: #2f2f3a; }
QPushButton#welcomeDone { background: %1; border-color: %1; color: #16161c;
                          font-weight: 600; }
)")
                              .arg(accent));

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(4);
    auto *heading = new QLabel(uiText("Welcome to Chatterino by Mist1X"));
    heading->setObjectName("welcomeHeading");
    layout->addWidget(heading);
    auto *intro = new QLabel(uiText("A few steps to set it up. All of them can "
                                    "be done later from the settings."));
    intro->setObjectName("welcomeText");
    layout->addWidget(intro);
    layout->addSpacing(10);

    auto *window = parent;
    auto settingsButton = [dialog, window](const QString &text,
                                           SettingsDialogPreference tab) {
        auto *button = new QPushButton(text);
        QObject::connect(button, &QPushButton::clicked, dialog,
                         [window, tab] {
                             getApp()->getWindows()->showSettingsDialog(window,
                                                                        tab);
                         });
        return button;
    };
    auto linkButton = [](const QString &text, const QString &url) {
        auto *button = new QPushButton(text);
        QObject::connect(button, &QPushButton::clicked, [url] {
            QDesktopServices::openUrl(QUrl(url));
        });
        return button;
    };

    layout->addWidget(step(
        1, uiText("Log in to Twitch"),
        uiText("Without it the chat can only be read. Settings → Accounts → "
               "Add new account."),
        {settingsButton(uiText("Log in"), SettingsDialogPreference::Accounts)}));
    layout->addWidget(step(
        2, uiText("Open a channel"),
        uiText("Click the empty panel or «+» next to the tabs and type the "
               "channel name. Several channels fit in one tab."),
        {}));
    layout->addWidget(step(
        3, uiText("Channel points, pins, polls and predictions"),
        uiText("They need a second login: Settings → Sign-in for extra "
               "features. The code is confirmed on twitch.tv/activate."),
        {settingsButton(uiText("Set up"),
                        SettingsDialogPreference::Moltorino)}));
    auto *importButton = new QPushButton(uiText("Move…"));
    QObject::connect(importButton, &QPushButton::clicked, dialog,
                     [dialog, importButton] {
                         chooseImport(dialog, importButton);
                     });
    layout->addWidget(step(
        4, uiText("Settings from another Chatterino"),
        uiText("Tabs with channels, highlights, ignores, commands and filters "
               "from Chatterino, Chatterino7, Leafyrino or Chatterino+. "
               "Logging in to Twitch is done again."),
        {importButton}));

    layout->addSpacing(8);
    auto *footer = new QHBoxLayout;
    auto *hint = new QLabel(uiText("The settings are sorted by topic; the "
                                   "search at the top finds any of them."));
    hint->setObjectName("welcomeText");
    hint->setWordWrap(true);
    footer->addWidget(hint, 1);
    const auto guide =
        QDir(QCoreApplication::applicationDirPath()).filePath("Руководство.html");
    if (QFileInfo::exists(guide))
    {
        footer->addWidget(linkButton(uiText("Guide"),
                                     QUrl::fromLocalFile(guide).toString()));
    }
    auto *done = new QPushButton(uiText("Start"));
    done->setObjectName("welcomeDone");
    QObject::connect(done, &QPushButton::clicked, dialog, &QDialog::accept);
    footer->addWidget(done);
    layout->addLayout(footer);

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void showWelcomeDialogIfNew(QWidget *parent)
{
    if (QFileInfo::exists(shownMarker()))
    {
        return;
    }
    QFile marker(shownMarker());
    if (marker.open(QIODevice::WriteOnly))
    {
        marker.write("1");
    }
    // updating from an older version: already set up
    if (!getApp()->getAccounts()->twitch.getCurrent()->isAnon())
    {
        return;
    }
    showWelcomeDialog(parent);
}

}  // namespace chatterino
