// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/UiLanguage.hpp"
#include "widgets/settingspages/NotificationPage.hpp"

#include "Application.hpp"
#include "controllers/notifications/NotificationController.hpp"
#include "controllers/notifications/NotificationModel.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Toasts.hpp"
#include "util/LayoutCreator.hpp"
#include "util/Twitch.hpp"
#include "widgets/helper/EditableModelView.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QTableView>
#include <QTimer>

namespace chatterino {

NotificationPage::NotificationPage()
{
    LayoutCreator<NotificationPage> layoutCreator(this);
    auto layout = layoutCreator.emplace<QVBoxLayout>().withoutMargin();
    {
        auto tabs = layout.emplace<QTabWidget>();
        {
            auto settings = tabs.appendTab(new QVBoxLayout, uiText("Options"));
            {
                settings.emplace<QLabel>(
                    uiText("You can be informed when certain channels go live. You "
                    "must be logged in to receive live notifications."
                    "\nYou can be informed in multiple ways:"));

                settings.append(this->createCheckBox(
                    uiText("Flash taskbar"), getSettings()->notificationFlashTaskbar));
                settings.append(
                    this->createCheckBox(uiText("Play sound for selected channels"),
                                         getSettings()->notificationPlaySound));
                settings.append(this->createCheckBox(
                    uiText("Play sound for any channel going live"),
                    getSettings()->notificationOnAnyChannel));

                settings.append(this->createCheckBox(
                    uiText("Suppress live notifications on startup"),
                    getSettings()->suppressInitialLiveNotification));
#if defined(Q_OS_WIN) || defined(CHATTERINO_WITH_LIBNOTIFY)
                settings.append(this->createCheckBox(
                    uiText("Show notification"), getSettings()->notificationToast));
#endif
#ifdef Q_OS_WIN
                settings.append(this->createCheckBox(
                    uiText("Create start menu shortcut (requires "
                    "restart)"),
                    getSettings()->createShortcutForToasts,
                    "When enabled, a shortcut will be created inside your "
                    "start menu folder if needed by live notifications."
                    "\n(On portable mode, this is disabled by "
                    "default)"));
#endif
#if defined(Q_OS_WIN) || defined(CHATTERINO_WITH_LIBNOTIFY)
                auto openIn = settings.emplace<QHBoxLayout>().withoutMargin();
                {
                    openIn
                        .emplace<QLabel>(
                            uiText("Action when clicking on a notification:  "))
                        ->setSizePolicy(QSizePolicy::Maximum,
                                        QSizePolicy::Preferred);

                    openIn.append(this->createToastReactionComboBox())
                        ->setSizePolicy(QSizePolicy::Maximum,
                                        QSizePolicy::Preferred);
                }
                openIn->setContentsMargins(40, 0, 0, 0);
                openIn->setSizeConstraint(QLayout::SetMaximumSize);
#endif
                auto customSound =
                    layout.emplace<QHBoxLayout>().withoutMargin();
                {
                    customSound.append(this->createCheckBox(
                        uiText("Custom sound"),
                        getSettings()->notificationCustomSound));
                    auto selectFile = customSound.emplace<QPushButton>(
                        uiText("Select custom sound file"));
                    QObject::connect(
                        selectFile.getElement(), &QPushButton::clicked, this,
                        [this] {
                            auto fileName = QFileDialog::getOpenFileName(
                                this, tr("Open Sound"), "",
                                tr("Audio Files (*.mp3 *.wav)"));
                            getSettings()->notificationPathSound = fileName;
                        });
                }

                settings->addStretch(1);
            }
            auto twitchChannels =
                tabs.appendTab(new QVBoxLayout, uiText("Selected Channels"));
            {
                twitchChannels.emplace<QLabel>(
                    uiText("These are the channels for which you will be informed "
                    "when they go live:"));

                EditableModelView *view =
                    twitchChannels
                        .emplace<EditableModelView>(
                            getApp()->getNotifications()->createModel(
                                nullptr, Platform::Twitch))
                        .getElement();
                view->setTitles({uiText("Twitch channels")});
                view->setValidationRegexp(twitchUserNameRegexp());

                view->getTableView()->horizontalHeader()->setSectionResizeMode(
                    QHeaderView::Fixed);
                view->getTableView()->horizontalHeader()->setSectionResizeMode(
                    0, QHeaderView::Stretch);

                QTimer::singleShot(1, [view] {
                    view->getTableView()->resizeColumnsToContents();
                    view->getTableView()->setColumnWidth(0, 200);
                });

                std::ignore = view->addButtonPressed.connect([] {
                    getApp()->getNotifications()->addChannelNotification(
                        "channel", Platform::Twitch);
                });
            }
        }
    }
}
QComboBox *NotificationPage::createToastReactionComboBox()
{
    QComboBox *toastReactionOptions = new QComboBox();

    for (int i = 0; i <= static_cast<int>(ToastReaction::OpenInCustomPlayer);
         i++)
    {
        toastReactionOptions->insertItem(
            i, Toasts::findStringFromReaction(static_cast<ToastReaction>(i)));
    }

    pajlada::Settings::Setting<int> setting = getSettings()->openFromToast;
    setting.connect(
        [toastReactionOptions](const int &index, auto) {
            toastReactionOptions->setCurrentIndex(index);
        },
        this->managedConnections_);

    QObject::connect(toastReactionOptions,
                     QOverload<int>::of(&QComboBox::currentIndexChanged),
                     [](const int &newValue) {
                         getSettings()->openFromToast.setValue(newValue);
                     });

    return toastReactionOptions;
}
}  // namespace chatterino
