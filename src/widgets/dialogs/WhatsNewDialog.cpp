// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/WhatsNewDialog.hpp"

#include "Application.hpp"
#include "common/Mist1XVersion.hpp"
#include "common/UiLanguage.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Theme.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>
#include <QVersionNumber>

namespace chatterino {

namespace {

/// how many versions back the list goes at most
constexpr int MAX_VERSIONS = 3;

QString versionMarker()
{
    return QDir(getApp()->getPaths().miscDirectory).filePath("last-version");
}

struct Section {
    QVersionNumber version;
    QStringList points;
};

/// "Chatterino by Mist1X 1.2.4" / "=====" / "- point" (continued on indented
/// lines), newest first.
QList<Section> readNotes()
{
    QFile file(QDir(QCoreApplication::applicationDirPath())
                   .filePath(QStringLiteral("Что нового.txt")));
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    static const QRegularExpression heading(
        QStringLiteral(R"(^Chatterino by Mist1X (\d+(?:\.\d+)+)\s*$)"));
    QList<Section> sections;
    for (const auto &raw : QString::fromUtf8(file.readAll()).split(u'\n'))
    {
        const auto line = raw.trimmed();
        if (auto match = heading.match(line); match.hasMatch())
        {
            sections.append({QVersionNumber::fromString(match.captured(1)), {}});
        }
        else if (sections.isEmpty() || line.isEmpty() || line.startsWith(u'='))
        {
            continue;
        }
        else if (line.startsWith(QStringLiteral("- ")))
        {
            sections.last().points.append(line.mid(2));
        }
        else if (!sections.last().points.isEmpty())
        {
            sections.last().points.last() += u' ' + line;
        }
    }
    return sections;
}

}  // namespace

void showWhatsNewDialog(QWidget *parent, const QString &sinceVersion)
{
    const auto current = QVersionNumber::fromString(MIST1X_VERSION);
    const auto since = QVersionNumber::fromString(sinceVersion);
    QList<Section> shown;
    for (const auto &section : readNotes())
    {
        const bool wanted = since.isNull() ? section.version == current
                                           : section.version > since &&
                                                 section.version <= current;
        if (wanted && !section.points.isEmpty() &&
            shown.size() < MAX_VERSIONS)
        {
            shown.append(section);
        }
    }
    if (shown.isEmpty())
    {
        return;
    }

    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(uiText("What's new"));
    dialog->setMinimumWidth(560);
    dialog->resize(600, 520);
    const auto accent = getTheme()->accent.name();
    dialog->setStyleSheet(QStringLiteral(R"(
QDialog, QScrollArea, QWidget#notes { background: #16161c; }
QLabel { color: #e6e5ee; font-size: 13px; }
QLabel#heading { font-size: 20px; font-weight: 600; color: #f4f3fa; }
QLabel#version { font-size: 14px; font-weight: 600; color: %1; }
QLabel#muted { color: #a9a7b6; }
QPushButton { background: #25252e; border: 1px solid #363642; border-radius: 6px;
              padding: 5px 14px; color: #e6e6ee; }
QPushButton:hover { background: #2f2f3a; }
QPushButton#done { background: %1; border-color: %1; color: #16161c;
                   font-weight: 600; }
)")
                              .arg(accent));

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 18);
    layout->setSpacing(6);
    auto *heading = new QLabel(
        uiText("Chatterino by Mist1X has been updated to %1")
            .arg(QStringLiteral(MIST1X_VERSION)));
    heading->setObjectName("heading");
    heading->setWordWrap(true);
    layout->addWidget(heading);
    auto *intro = new QLabel(uiText("Here is what changed:"));
    intro->setObjectName("muted");
    layout->addWidget(intro);
    layout->addSpacing(6);

    auto *notes = new QWidget;
    notes->setObjectName("notes");
    auto *notesLayout = new QVBoxLayout(notes);
    notesLayout->setContentsMargins(0, 0, 8, 0);
    notesLayout->setSpacing(6);
    for (const auto &section : shown)
    {
        if (shown.size() > 1)
        {
            auto *version = new QLabel(
                uiText("Version %1").arg(section.version.toString()));
            version->setObjectName("version");
            notesLayout->addSpacing(4);
            notesLayout->addWidget(version);
        }
        for (const auto &point : section.points)
        {
            auto *row = new QHBoxLayout;
            row->setSpacing(8);
            auto *bullet = new QLabel(QStringLiteral("•"));
            bullet->setStyleSheet(QStringLiteral("color: %1;").arg(accent));
            row->addWidget(bullet, 0, Qt::AlignTop);
            auto *text = new QLabel(point);
            text->setWordWrap(true);
            text->setTextInteractionFlags(Qt::TextSelectableByMouse);
            row->addWidget(text, 1);
            notesLayout->addLayout(row);
        }
    }
    notesLayout->addStretch(1);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(notes);
    layout->addWidget(scroll, 1);

    layout->addSpacing(8);
    auto *buttons = new QHBoxLayout;
    auto *all = new QPushButton(uiText("All changes"));
    QObject::connect(all, &QPushButton::clicked, dialog, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "https://github.com/Mist10X/chatterino-by-mist1x/releases")));
    });
    buttons->addWidget(all);
    buttons->addStretch(1);
    auto *done = new QPushButton(uiText("Got it"));
    done->setObjectName("done");
    done->setDefault(true);
    QObject::connect(done, &QPushButton::clicked, dialog, &QDialog::accept);
    buttons->addWidget(done);
    layout->addLayout(buttons);

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void showWhatsNewIfUpdated(QWidget *parent)
{
    QString previous;
    QFile marker(versionMarker());
    const bool known = marker.open(QIODevice::ReadOnly);
    if (known)
    {
        previous = QString::fromUtf8(marker.readAll()).trimmed();
        marker.close();
    }
    if (previous == QStringLiteral(MIST1X_VERSION))
    {
        return;
    }
    // A copy that ran before this marker existed (1.2.4 and older): it has
    // shown "Getting started" or has someone logged in. A fresh install has
    // neither and gets "Getting started" instead.
    const bool updated =
        known ||
        QFileInfo::exists(QDir(getApp()->getPaths().miscDirectory)
                              .filePath(QStringLiteral("welcome-shown"))) ||
        !getApp()->getAccounts()->twitch.getCurrent()->isAnon();
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        marker.write(MIST1X_VERSION);
        marker.close();
    }
    if (updated)
    {
        showWhatsNewDialog(parent, previous);
    }
}

}  // namespace chatterino
